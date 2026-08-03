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

namespace fs = std::filesystem;

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

    // Se o cache não existe ou expirou, compila e salva no cache
    std::string compiledBytecode = Compile(source, options, filePath);
    if (compiledBytecode.empty())
    {
        return "";
    }

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

    return compiledBytecode;
}

} // namespace Lode
