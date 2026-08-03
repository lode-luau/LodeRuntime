// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Compiler.hpp"
#include "Luau/Compiler.h"
#include "Luau/ParseOptions.h"
#include "Luau/Config.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <fstream>
#include <filesystem>

#include "Luau/Parser.h"
#include "Luau/Scope.h"
#include "Luau/Frontend.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/BytecodeBuilder.h"

namespace fs = std::filesystem;

namespace
{
    class SimpleFileResolver : public Luau::FileResolver
    {
    public:
        SimpleFileResolver(std::string_view fileName, std::string_view source)
            : fileName_(fileName), source_(source)
        {}

        std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override
        {
            if (name == fileName_)
            {
                Luau::SourceCode sc;
                sc.source = source_;
                sc.type = Luau::SourceCode::Script;
                return sc;
            }
            return std::nullopt;
        }

        std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* expr, const Luau::TypeCheckLimits& limits) override
        {
            return std::nullopt;
        }

        std::string getHumanReadableModuleName(const Luau::ModuleName& name) const override
        {
            return name;
        }

    private:
        std::string fileName_;
        std::string source_;
    };

    class SimpleConfigResolver : public Luau::ConfigResolver
    {
    public:
        SimpleConfigResolver(std::string_view source)
        {
            Luau::Allocator allocator;
            Luau::AstNameTable names(allocator);
            Luau::ParseOptions parseOptions;
            Luau::ParseResult parseResult = Luau::Parser::parse(source.data(), source.size(), names, allocator, parseOptions);

            config_.mode = Luau::parseMode(parseResult.hotcomments).value_or(Luau::Mode::Nonstrict);
        }

        const Luau::Config& getConfig(const Luau::ModuleName& name, const Luau::TypeCheckLimits& limits) const override
        {
            return config_;
        }

    private:
        Luau::Config config_;
    };
}

namespace Lode
{

lua_CompileOptions Compiler::GetDefaultOptions()
{
    lua_CompileOptions options{};
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    options.typeInfoLevel = 0;
    options.coverageLevel = 0;
    options.vectorPrecision = 0;
    return options;
}

lua_CompileOptions Compiler::ParseOptionsFromSource(std::string_view source, std::string_view filePath)
{
    lua_CompileOptions opts = GetDefaultOptions();

    // Analisar comentários de diretivas de cabeçalho (--!native, --!optimize, --!debug)
    std::istringstream stream((std::string(source)));
    std::string line;
    while (std::getline(stream, line))
    {
        // Strip leading whitespaces
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Directive comments start with --!
        if (line.rfind("--!", 0) != 0)
        {
            // Stop parsing header comment directives after non-comment line or normal comment
            if (line.rfind("--", 0) != 0)
            {
                break;
            }
            continue;
        }

        std::string directive = line.substr(3);
        size_t end = directive.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) directive = directive.substr(0, end + 1);

        if (directive == "native")
        {
            opts.typeInfoLevel = 1;
        }
        else if (directive.rfind("optimize ", 0) == 0)
        {
            int level = std::atoi(directive.substr(9).c_str());
            if (level >= 0 && level <= 2)
            {
                opts.optimizationLevel = level;
            }
        }
        else if (directive.rfind("debug ", 0) == 0)
        {
            int level = std::atoi(directive.substr(6).c_str());
            if (level >= 0 && level <= 2)
            {
                opts.debugLevel = level;
            }
        }
    }

    return opts;
}

std::string Compiler::Compile(std::string_view source, const lua_CompileOptions* options, std::string_view filePath)
{
    lua_CompileOptions parsedOpts;
    if (!options)
    {
        parsedOpts = ParseOptionsFromSource(source, filePath);
        options = &parsedOpts;
    }

    size_t bytecodeSize = 0;
    char* compiled = luau_compile(source.data(), source.size(), const_cast<lua_CompileOptions*>(options), &bytecodeSize);
    if (!compiled)
    {
        return "";
    }

    std::string result(compiled, bytecodeSize);
    free(compiled);
    return result;
}

std::string Compiler::CompileWithResult(std::string_view source, std::vector<Diagnostic>& outDiagnostics, const lua_CompileOptions* options, std::string_view filePath)
{
    std::string moduleName = filePath.empty() ? "main" : std::string(filePath);

    SimpleFileResolver fileResolver(moduleName, source);
    SimpleConfigResolver configResolver(source);

    Luau::FrontendOptions frontendOptions;
    frontendOptions.runLintChecks = true;

    Luau::Frontend frontend(&fileResolver, &configResolver, frontendOptions);
    Luau::registerBuiltinGlobals(frontend, frontend.globals);
    Luau::freeze(frontend.globals.globalTypes);

    // Carregar fonte diretamente na Frontend
    Luau::SourceModule* sm = frontend.getSourceModule(moduleName);
    if (!sm)
    {
        Luau::Allocator allocator;
        Luau::AstNameTable names(allocator);
        Luau::ParseOptions parseOptions;
        Luau::ParseResult parseResult = Luau::Parser::parse(source.data(), source.size(), names, allocator, parseOptions);

        if (!parseResult.errors.empty())
        {
            for (const auto& err : parseResult.errors)
            {
                outDiagnostics.push_back(Logger::FromParseError(err, filePath));
            }
            return "";
        }
    }

    Luau::CheckResult checkResult = frontend.check(moduleName);
    bool hasErrors = false;

    // Coletar Type Errors do Frontend do Luau (--!strict, etc.)
    for (const auto& err : checkResult.errors)
    {
        if (const Luau::SyntaxError* syntaxError = Luau::get_if<Luau::SyntaxError>(&err.data))
        {
            Diagnostic diag;
            diag.filePath = std::string(filePath);
            diag.line = err.location.begin.line + 1;
            diag.column = err.location.begin.column + 1;
            diag.length = (err.location.end.column > err.location.begin.column) ? (err.location.end.column - err.location.begin.column) : 1;
            diag.message = syntaxError->message;
            diag.code = "SyntaxError";
            outDiagnostics.push_back(diag);
            hasErrors = true;
        }
        else
        {
            outDiagnostics.push_back(Logger::FromTypeError(err, filePath, &fileResolver));
            hasErrors = true;
        }
    }

    // Coletar Lint Warnings do Frontend
    for (const auto& warn : checkResult.lintResult.warnings)
    {
        outDiagnostics.push_back(Logger::FromLintWarning(warn, filePath));
    }
    for (const auto& errWarn : checkResult.lintResult.errors)
    {
        outDiagnostics.push_back(Logger::FromLintWarning(errWarn, filePath));
    }

    if (hasErrors)
    {
        return "";
    }

    return Compile(source, options, filePath);
}

std::string Compiler::GetCacheDirectory()
{
    fs::path tempDir = fs::temp_directory_path();
    fs::path lodeCacheDir = tempDir / "lode_cache";
    std::error_code ec;
    fs::create_directories(lodeCacheDir, ec);
    return lodeCacheDir.string();
}

std::string Compiler::CompileWithCache(std::string_view source, std::string_view filePath, const lua_CompileOptions* options)
{
    if (filePath.empty())
    {
        return Compile(source, options, filePath);
    }

    std::error_code ec;
    fs::path srcPath = fs::absolute(filePath, ec);
    if (ec || !fs::exists(srcPath))
    {
        return Compile(source, options, filePath);
    }

    auto lastWriteTime = fs::last_write_time(srcPath, ec);
    if (ec)
    {
        return Compile(source, options, filePath);
    }

    uint64_t fileMtime = static_cast<uint64_t>(lastWriteTime.time_since_epoch().count());

    // Gerar hash simples do caminho absoluto para nomear o arquivo de cache
    std::size_t pathHash = std::hash<std::string>{}(srcPath.string());
    std::string cacheFileName = srcPath.stem().string() + "_" + std::to_string(pathHash) + ".luac";
    fs::path cachePath = fs::path(GetCacheDirectory()) / cacheFileName;

    // Estrutura do cabeçalho binário do cache
    struct CacheHeader
    {
        char magic[4];       // "LODE"
        uint64_t mtime;     // Timestamp da última modificação
        uint32_t size;      // Tamanho do bytecode
    };

    // Tentar ler do cache
    if (fs::exists(cachePath))
    {
        std::ifstream cacheFile(cachePath, std::ios::binary);
        if (cacheFile.is_open())
        {
            CacheHeader header{};
            cacheFile.read(reinterpret_cast<char*>(&header), sizeof(CacheHeader));

            if (cacheFile.gcount() == sizeof(CacheHeader) &&
                std::memcmp(header.magic, "LODE", 4) == 0 &&
                header.mtime == fileMtime)
            {
                std::string cachedBytecode(header.size, '\0');
                cacheFile.read(cachedBytecode.data(), header.size);
                if (cacheFile.gcount() == static_cast<std::streamsize>(header.size))
                {
                    return cachedBytecode;
                }
            }
        }
    }

    // Se o cache não existe ou expirou, compila e salva no cache (apenas se for bytecode válido)
    std::string compiledBytecode = Compile(source, options, filePath);
    if (compiledBytecode.empty())
    {
        return "";
    }

    // Não grava cache para bytecodes de erro (bytecode[0] == 0)
    if (compiledBytecode[0] != 0)
    {
        std::ofstream cacheOut(cachePath, std::ios::binary);
        if (cacheOut.is_open())
        {
            CacheHeader header{};
            std::memcpy(header.magic, "LODE", 4);
            header.mtime = fileMtime;
            header.size = static_cast<uint32_t>(compiledBytecode.size());

            cacheOut.write(reinterpret_cast<const char*>(&header), sizeof(CacheHeader));
            cacheOut.write(compiledBytecode.data(), compiledBytecode.size());
        }
    }

    return compiledBytecode;
}

} // namespace Lode
