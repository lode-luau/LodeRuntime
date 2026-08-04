// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Compiler.hpp"
#include "ModuleLoader.hpp"
#include "Luau/Compiler.h"
#include "Luau/ParseOptions.h"
#include "Luau/Config.h"
#include "Luau/LuauConfig.h"
#include <cstdlib>
#include <cstring>
#include <array>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
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
        SimpleFileResolver(std::string_view fileName, std::string_view source, Luau::ConfigResolver* configResolver = nullptr)
            : fileName_(fileName), source_(source), configResolver_(configResolver)
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
            
            std::ifstream file(name);
            if (file.is_open())
            {
                std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                Luau::SourceCode sc;
                sc.source = std::move(contents);
                sc.type = Luau::SourceCode::Module;
                return sc;
            }

            return std::nullopt;
        }

        std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* context, Luau::AstExpr* expr, const Luau::TypeCheckLimits& limits) override
        {
            if (Luau::AstExprConstantString* exprString = expr->as<Luau::AstExprConstantString>())
            {
                std::string path{exprString->value.data, exprString->value.size};

                if (!path.empty() && path[0] == '@')
                {
                    size_t slashPos = path.find('/', 1);
                    std::string aliasName = (slashPos != std::string::npos) ? path.substr(1, slashPos - 1) : path.substr(1);
                    std::string remainder = (slashPos != std::string::npos) ? path.substr(slashPos + 1) : "";

                    if (aliasName == "self")
                    {
                        // @self resolves to the package's own directory (the folder
                        // containing init.luau or lode.json), mirroring the runtime.
                        fs::path requirerPath(context ? context->name : "");
                        fs::path pkgDir = Lode::FindLodeJson(requirerPath);
                        if (pkgDir.empty())
                            pkgDir = requirerPath.parent_path();

                        fs::path resolvedPath = pkgDir;
                        if (!remainder.empty())
                            resolvedPath /= remainder;

                        std::string finalPath = resolvedPath.string();
                        if (!fs::is_regular_file(finalPath) && fs::is_regular_file(finalPath + ".luau"))
                            finalPath += ".luau";
                        else if (fs::is_regular_file(resolvedPath / "init.luau"))
                            finalPath = (resolvedPath / "init.luau").string();

                        try { finalPath = fs::weakly_canonical(finalPath).string(); } catch(...) {}
                        return Luau::ModuleInfo{finalPath};
                    }

                    if (configResolver_)
                    {
                        const Luau::Config& config = configResolver_->getConfig(context->name, limits);
                        auto it = config.aliases.find(aliasName);
                        if (it != nullptr)
                        {
                            fs::path resolvedPath = fs::path(std::string(it->configLocation)).parent_path() / it->value;
                            if (!remainder.empty())
                                resolvedPath /= remainder;
                            
                            std::string finalPath = resolvedPath.string();
                            if (!fs::is_regular_file(finalPath) && fs::is_regular_file(finalPath + ".luau"))
                                finalPath += ".luau";
                            else if (fs::is_regular_file(resolvedPath / "init.luau"))
                                finalPath = (resolvedPath / "init.luau").string();

                            try { finalPath = fs::weakly_canonical(finalPath).string(); } catch(...) {}
                            return Luau::ModuleInfo{finalPath};
                        }
                    }
                }
                else
                {
                    std::string relPath = path;
                    if (relPath.rfind("./", 0) == 0)
                        relPath = relPath.substr(2);
                    
                    fs::path ctxPath(context->name);
                    fs::path dirPath = ctxPath.has_parent_path() ? ctxPath.parent_path() : ctxPath;
                    if (ctxPath.filename() == "init.luau" || ctxPath.filename() == "init.lua")
                    {
                        if (dirPath.has_parent_path())
                            dirPath = dirPath.parent_path();
                    }
                    fs::path resolvedPath = dirPath / relPath;
                    
                    std::string finalPath = resolvedPath.string();
                    if (!fs::is_regular_file(finalPath) && fs::is_regular_file(finalPath + ".luau"))
                        finalPath += ".luau";
                    else if (fs::is_regular_file(resolvedPath / "init.luau"))
                        finalPath = (resolvedPath / "init.luau").string();

                    try { finalPath = fs::weakly_canonical(finalPath).string(); } catch(...) {}
                    return Luau::ModuleInfo{finalPath};
                }
            }
            return std::nullopt;
        }

        std::string getHumanReadableModuleName(const Luau::ModuleName& name) const override
        {
            return name;
        }

    private:
        std::string fileName_;
        std::string source_;
        Luau::ConfigResolver* configResolver_;
    };

    // Reads the entire contents of a file into a string. Returns nullopt on failure.
    static std::optional<std::string> readTextFile(const fs::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    // ConfigResolver that walks up the directory tree searching for .luaurc / .config.luau,
    // mirroring the behavior of the official luau-analyze CLI.
    class FileConfigResolver : public Luau::ConfigResolver
    {
    public:
        // outExplicitMode: mode declared via hotcomment in the script (nullopt = no hotcomment)
        FileConfigResolver(std::string_view source, std::string_view filePath, std::optional<Luau::Mode>& outExplicitMode)
        {
            // Parse the hotcomment mode from the script source
            Luau::Allocator allocator;
            Luau::AstNameTable names(allocator);
            Luau::ParseOptions parseOptions;
            Luau::ParseResult parseResult = Luau::Parser::parse(source.data(), source.size(), names, allocator, parseOptions);
            outExplicitMode = Luau::parseMode(parseResult.hotcomments);

            // Default config follows the user's runtime preference (typeErrors off by default).
            // A .luaurc or .config.luau found in the directory tree can override both.
            defaultConfig_.typeErrors = false;
            defaultConfig_.mode = Luau::Mode::Nonstrict;

            // Resolve the script's directory to start the upward config search
            if (!filePath.empty())
            {
                std::error_code ec;
                fs::path scriptDir = fs::absolute(fs::path(filePath), ec).parent_path();
                if (!ec)
                    scriptDir_ = scriptDir.string();
            }
        }

        const Luau::Config& getConfig(const Luau::ModuleName& name, const Luau::TypeCheckLimits& limits) const override
        {
            // Determine the directory of the requested module
            std::string dir;
            if (!name.empty())
            {
                std::error_code ec;
                fs::path p = fs::absolute(fs::path(name), ec);
                if (!ec && fs::exists(p, ec))
                    dir = p.parent_path().string();
            }
            if (dir.empty())
                dir = scriptDir_;

            if (dir.empty())
                return defaultConfig_;

            return readConfigRec(dir);
        }

        // Returns the effective config for the script file (valid after frontend.check() has
        // populated the cache via getConfig).
        const Luau::Config& getScriptConfig() const
        {
            if (!scriptDir_.empty())
            {
                auto it = configCache_.find(scriptDir_);
                if (it != configCache_.end())
                    return it->second;
            }
            return defaultConfig_;
        }

    private:
        std::string scriptDir_;
        Luau::Config defaultConfig_;
        mutable std::unordered_map<std::string, Luau::Config> configCache_;

        const Luau::Config& readConfigRec(const std::string& dir) const
        {
            auto it = configCache_.find(dir);
            if (it != configCache_.end())
                return it->second;

            // Walk up to the parent directory
            std::error_code ec;
            fs::path parentPath = fs::path(dir).parent_path();
            bool hasParent = (parentPath != fs::path(dir)) && !parentPath.empty();

            // Inherit config from the parent (recursive)
            Luau::Config result = hasParent ? readConfigRec(parentPath.string()) : defaultConfig_;

            fs::path luaurcPath = fs::path(dir) / Luau::kConfigName;       // .luaurc
            fs::path luauConfigPath = fs::path(dir) / Luau::kLuauConfigName; // .config.luau

            bool luaurcExists = fs::exists(luaurcPath, ec) && !ec;
            bool luauConfigExists = fs::exists(luauConfigPath, ec) && !ec;

            if (luaurcExists && luauConfigExists)
            {
                // Ambiguity: both files exist — skip both (mirrors official CLI behavior)
            }
            else if (luaurcExists)
            {
                if (std::optional<std::string> contents = readTextFile(luaurcPath))
                {
                    Luau::ConfigOptions::AliasOptions aliasOpts;
                    aliasOpts.configLocation = luaurcPath.string();
                    aliasOpts.overwriteAliases = true;
                    Luau::ConfigOptions opts;
                    opts.aliasOptions = std::move(aliasOpts);
                    Luau::parseConfig(*contents, result, opts); // silently ignore parse errors
                }
            }
            else if (luauConfigExists)
            {
                if (std::optional<std::string> contents = readTextFile(luauConfigPath))
                {
                    Luau::ConfigOptions::AliasOptions aliasOpts;
                    aliasOpts.configLocation = luauConfigPath.string();
                    aliasOpts.overwriteAliases = true;
                    Luau::InterruptCallbacks callbacks{};
                    Luau::extractLuauConfig(*contents, result, aliasOpts, std::move(callbacks));
                }
            }

            return configCache_[dir] = result;
        }
    };

    // ---------------------------------------------------------------------------
    // SHA-256 (FIPS 180-4) used to key the compile cache by source content.
    // ---------------------------------------------------------------------------
    static void Sha256Transform(uint32_t state[8], const uint8_t block[64])
    {
        static const uint32_t K[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
                   (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
        for (int i = 16; i < 64; ++i)
        {
            uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i)
        {
            uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + s1 + ch + K[i] + w[i];
            uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    static std::array<uint8_t, 32> Sha256(std::string_view data)
    {
        uint32_t state[8] = {
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        };

        std::vector<uint8_t> msg(data.begin(), data.end());
        msg.push_back(0x80);
        while (msg.size() % 64 != 56)
            msg.push_back(0);
        uint64_t bitLen = uint64_t(data.size()) * 8;
        for (int i = 7; i >= 0; --i)
            msg.push_back(uint8_t((bitLen >> (i * 8)) & 0xff));

        for (size_t off = 0; off < msg.size(); off += 64)
            Sha256Transform(state, msg.data() + off);

        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i)
        {
            out[i * 4]     = uint8_t(state[i] >> 24);
            out[i * 4 + 1] = uint8_t(state[i] >> 16);
            out[i * 4 + 2] = uint8_t(state[i] >> 8);
            out[i * 4 + 3] = uint8_t(state[i]);
        }
        return out;
    }

    static std::string ToHex(const std::array<uint8_t, 32>& bytes)
    {
        static const char hex[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (uint8_t b : bytes)
        {
            out.push_back(hex[b >> 4]);
            out.push_back(hex[b & 0x0f]);
        }
        return out;
    }
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

    // Parse header directive hotcomments (--!native, --!optimize N, --!debug N)
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

    // explicitMode: mode declared via hotcomment in the script (nullopt = no hotcomment)
    std::optional<Luau::Mode> explicitMode;
    FileConfigResolver configResolver(source, filePath, explicitMode);
    SimpleFileResolver fileResolver(moduleName, source, &configResolver);

    Luau::FrontendOptions frontendOptions;
    frontendOptions.runLintChecks = true;

    Luau::Frontend frontend(&fileResolver, &configResolver, frontendOptions);
    Luau::registerBuiltinGlobals(frontend, frontend.globals);
    Luau::freeze(frontend.globals.globalTypes);

    // Check for parse errors before running the frontend
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
                outDiagnostics.push_back(Logger::FromParseError(err, filePath));
            return "";
        }
    }

    Luau::CheckResult checkResult = frontend.check(moduleName);
    bool hasErrors = false;

    // -------------------------------------------------------------------------
    // Mode resolution and type error emission rules:
    //
    //  Priority: hotcomment > config languageMode > Nonstrict (default)
    //
    //  - Strict   -> type errors BLOCK execution  (error[], red)
    //  - Nonstrict -> type errors do NOT block     (warning[], yellow), script still runs
    //  - NoCheck  -> type errors suppressed entirely
    //
    //  config.typeErrors (default: false) enables emission in Nonstrict mode.
    //  --!strict always enables and blocks; --!nocheck always suppresses.
    // -------------------------------------------------------------------------
    const Luau::Config& scriptConfig = configResolver.getScriptConfig();
    Luau::Mode effectiveMode = explicitMode.value_or(scriptConfig.mode);
    bool isStrictMode = (effectiveMode == Luau::Mode::Strict);

    bool emitTypeErrors;
    if (effectiveMode == Luau::Mode::NoCheck)
        emitTypeErrors = false;                                      // nocheck: suppress all type errors
    else if (explicitMode.has_value() && *explicitMode == Luau::Mode::Strict)
        emitTypeErrors = true;                                       // --!strict: always emit and block
    else
        emitTypeErrors = scriptConfig.typeErrors;                    // otherwise: follow config (default: false)

    for (const auto& err : checkResult.errors)
    {
        if (const Luau::SyntaxError* syntaxError = Luau::get_if<Luau::SyntaxError>(&err.data))
        {
            // Syntax errors always block — without valid parse there is no bytecode
            Diagnostic diag;
            if (!err.moduleName.empty())
                diag.filePath = err.moduleName;
            else
                diag.filePath = std::string(filePath);
            diag.line = err.location.begin.line + 1;
            diag.column = err.location.begin.column + 1;
            diag.length = (err.location.end.column > err.location.begin.column)
                ? (err.location.end.column - err.location.begin.column) : 1;
            diag.message = syntaxError->message;
            diag.code = "SyntaxError";
            diag.isWarning = false;
            outDiagnostics.push_back(diag);
            hasErrors = true;
        }
        else
        {
            Luau::Mode errMode = effectiveMode;
            Luau::ModuleName modName = err.moduleName.empty() ? std::string(filePath) : err.moduleName;
            
            if (!err.moduleName.empty())
            {
                if (Luau::SourceModule* sm = frontend.getSourceModule(err.moduleName))
                {
                    if (sm->mode.has_value())
                        errMode = *sm->mode;
                }
            }

            bool errIsStrictMode = (errMode == Luau::Mode::Strict);
            bool errEmitTypeErrors = false;
            
            if (errMode == Luau::Mode::NoCheck)
                errEmitTypeErrors = false;
            else if (errMode == Luau::Mode::Strict)
                errEmitTypeErrors = true;
            else
            {
                // Check if the specific module has typeErrors enabled via config
                const Luau::Config& errConfig = configResolver.getConfig(modName, Luau::TypeCheckLimits{});
                errEmitTypeErrors = errConfig.typeErrors;
            }

            if (errEmitTypeErrors)
            {
                Diagnostic diag = Logger::FromTypeError(err, filePath, &fileResolver);
                diag.isWarning = !errIsStrictMode;
                outDiagnostics.push_back(diag);
                if (errIsStrictMode)
                    hasErrors = true;
            }
        }
    }

    // Lint warnings: never block execution
    for (const auto& warn : checkResult.lintResult.warnings)
        outDiagnostics.push_back(Logger::FromLintWarning(warn, filePath));
    for (const auto& errWarn : checkResult.lintResult.errors)
        outDiagnostics.push_back(Logger::FromLintWarning(errWarn, filePath));

    if (hasErrors)
        return "";

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

// Fingerprint of the compile options that affect the generated bytecode. Any
// change here (e.g. a new --!native / --!optimize / --!debug directive) must
// produce a different cache entry.
static std::array<uint8_t, 32> OptionsFingerprint(const lua_CompileOptions& o)
{
    int fields[5] = { o.optimizationLevel, o.debugLevel, o.typeInfoLevel, o.coverageLevel, o.vectorPrecision };
    std::string bytes;
    bytes.reserve(sizeof(fields));
    for (int v : fields)
    {
        bytes.push_back(char(v & 0xff));
        bytes.push_back(char((v >> 8) & 0xff));
        bytes.push_back(char((v >> 16) & 0xff));
        bytes.push_back(char((v >> 24) & 0xff));
    }
    return Sha256(bytes);
}

std::string Compiler::CompileWithCache(std::string_view source, std::string_view filePath, const lua_CompileOptions* options, std::vector<Diagnostic>* outDiagnostics)
{
    // No file path -> nothing to key the cache on.
    if (filePath.empty())
    {
        return outDiagnostics ? CompileWithResult(source, *outDiagnostics, options, filePath) : Compile(source, options, filePath);
    }

    // Resolve the same effective options the compiler would use so the cache key
    // reflects --!native/@native/--!optimize/--!debug directives from the source.
    lua_CompileOptions resolvedOptions = options ? *options : ParseOptionsFromSource(source, filePath);

    // The key is derived from the source CONTENT (SHA-256) plus the compile
    // options fingerprint, so editing a module or toggling its directives
    // invalidates the cache deterministically without relying on mtimes.
    std::array<uint8_t, 32> contentHash = Sha256(source);
    std::array<uint8_t, 32> optionsFp = OptionsFingerprint(resolvedOptions);

    std::string keyMaterial;
    keyMaterial.append(reinterpret_cast<const char*>(contentHash.data()), contentHash.size());
    keyMaterial.append(reinterpret_cast<const char*>(optionsFp.data()), optionsFp.size());
    std::array<uint8_t, 32> keyHash = Sha256(keyMaterial);

    std::error_code ec;
    fs::path srcPath = fs::absolute(filePath, ec);
    if (ec)
        srcPath = fs::path(filePath);

    std::string cacheFileName = srcPath.stem().string() + "_" + ToHex(keyHash) + ".luac";
    fs::path cachePath = fs::path(GetCacheDirectory()) / cacheFileName;

    struct CacheHeader
    {
        char magic[4];          // "LODE"
        uint8_t contentHash[32]; // SHA-256 do source (validação extra)
        uint32_t size;          // Tamanho do bytecode
    };

    // Tentar ler do cache (o nome do arquivo já codifica o hash do conteúdo)
    if (fs::exists(cachePath))
    {
        std::ifstream cacheFile(cachePath, std::ios::binary);
        if (cacheFile.is_open())
        {
            CacheHeader header{};
            cacheFile.read(reinterpret_cast<char*>(&header), sizeof(CacheHeader));
            if (cacheFile.gcount() == sizeof(CacheHeader) &&
                std::memcmp(header.magic, "LODE", 4) == 0 &&
                std::memcmp(header.contentHash, contentHash.data(), 32) == 0)
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

    // Cache miss: type-check + compile when diagnostics were requested, otherwise
    // plain-compile. The bytecode is cached so subsequent runs skip all of this.
    std::string compiledBytecode = outDiagnostics
        ? CompileWithResult(source, *outDiagnostics, &resolvedOptions, filePath)
        : Compile(source, &resolvedOptions, filePath);
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
            std::memcpy(header.contentHash, contentHash.data(), 32);
            header.size = static_cast<uint32_t>(compiledBytecode.size());

            cacheOut.write(reinterpret_cast<const char*>(&header), sizeof(CacheHeader));
            cacheOut.write(compiledBytecode.data(), compiledBytecode.size());
        }
    }

    return compiledBytecode;
}

} // namespace Lode
