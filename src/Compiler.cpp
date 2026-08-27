// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "Lode/Compiler.hpp"
#include "Lode/Logger.hpp"
#include "ModuleLoader.hpp"
#include "PathUtil.hpp"
#include "Sha256.hpp"
#include "Luau/Compiler.h"
#include "Luau/ParseOptions.h"
#include "Luau/Config.h"
#include "Luau/LuauConfig.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <cstddef>
#include <limits>
#include <random>
#include <thread>

#include "Luau/Parser.h"
#include "Luau/Scope.h"
#include "Luau/Frontend.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/BytecodeBuilder.h"

namespace fs = std::filesystem;

namespace
{
    using Lode::Detail::PathFromUtf8;
    using Lode::Detail::Sha256;
    using Lode::Detail::ToHex;
    using Lode::Detail::PathToUtf8;

    // Resolves a module path to a regular file: prefers the exact path, then the
    // .luau variant, then init.luau in the directory. Canonicalizes the result.
    static fs::path ResolveModuleFile(fs::path resolvedPath)
    {
        fs::path finalPath = resolvedPath;
        fs::path luauPath = finalPath;
        luauPath += ".luau";
        if (!fs::is_regular_file(finalPath) && fs::is_regular_file(luauPath))
            finalPath = luauPath;
        else if (fs::is_regular_file(resolvedPath / "init.luau"))
            finalPath = resolvedPath / "init.luau";

        try { finalPath = fs::weakly_canonical(finalPath); } catch (...) {}
        return finalPath;
    }

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
            
            std::ifstream file(PathFromUtf8(name));
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
                        // @self resolves to the module's own directory (the folder
                        // containing init.luau or a package manifest), mirroring the runtime.
                        fs::path requirerPath = PathFromUtf8(context ? context->name : "");
                        fs::path pkgDir;
                        if (requirerPath.filename() == "init.luau")
                        {
                            pkgDir = requirerPath.parent_path();
                        }
                        else
                        {
                            pkgDir = Lode::FindPackageRoot(requirerPath);
                            if (pkgDir.empty())
                                pkgDir = requirerPath.parent_path();
                        }

                        fs::path resolvedPath = pkgDir;
                        if (!remainder.empty())
                            resolvedPath /= remainder;

                        return Luau::ModuleInfo{PathToUtf8(ResolveModuleFile(resolvedPath))};
                    }

                    if (configResolver_)
                    {
                        const Luau::Config& config = configResolver_->getConfig(context->name, limits);
                        auto it = config.aliases.find(aliasName);
                        if (it != nullptr)
                        {
                            fs::path resolvedPath = PathFromUtf8(std::string(it->configLocation)).parent_path() / it->value;
                            if (!remainder.empty())
                                resolvedPath /= remainder;

                            return Luau::ModuleInfo{PathToUtf8(ResolveModuleFile(resolvedPath))};
                        }
                    }
                }
                else
                {
                    std::string relPath = path;
                    if (relPath.rfind("./", 0) == 0)
                        relPath = relPath.substr(2);
                    
                    fs::path ctxPath = PathFromUtf8(context->name);
                    fs::path dirPath = ctxPath.has_parent_path() ? ctxPath.parent_path() : ctxPath;
                    fs::path resolvedPath = dirPath / relPath;
                    fs::path candidate = ResolveModuleFile(resolvedPath);

                    if (!fs::exists(candidate) && dirPath.has_parent_path())
                    {
                        fs::path siblingPath = dirPath.parent_path() / relPath;
                        fs::path siblingCandidate = ResolveModuleFile(siblingPath);
                        if (fs::exists(siblingCandidate))
                        {
                            return Luau::ModuleInfo{PathToUtf8(siblingCandidate)};
                        }
                    }

                    return Luau::ModuleInfo{PathToUtf8(candidate)};
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
                fs::path scriptDir = fs::absolute(PathFromUtf8(filePath), ec).parent_path();
                if (!ec)
                    scriptDir_ = PathToUtf8(scriptDir);
            }
        }

        const Luau::Config& getConfig(const Luau::ModuleName& name, const Luau::TypeCheckLimits& limits) const override
        {
            // Determine the directory of the requested module
            std::string dir;
            if (!name.empty())
            {
                std::error_code ec;
                fs::path p = fs::absolute(PathFromUtf8(name), ec);
                if (!ec && fs::exists(p, ec))
                    dir = PathToUtf8(p.parent_path());
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
            fs::path dirPath = PathFromUtf8(dir);
            fs::path parentPath = dirPath.parent_path();
            bool hasParent = (parentPath != dirPath) && !parentPath.empty();

            // Inherit config from the parent (recursive)
            Luau::Config result = hasParent ? readConfigRec(PathToUtf8(parentPath)) : defaultConfig_;

            fs::path luaurcPath = dirPath / Luau::kConfigName;       // .luaurc
            fs::path luauConfigPath = dirPath / Luau::kLuauConfigName; // .config.luau

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
                    aliasOpts.configLocation = PathToUtf8(luaurcPath);
                    aliasOpts.overwriteAliases = true;
                    Luau::ConfigOptions opts;
                    opts.aliasOptions = std::move(aliasOpts);
                    if (std::optional<std::string> error = Luau::parseConfig(*contents, result, opts))
                        Lode::Logger::Warn("Ignoring invalid .luaurc at " + PathToUtf8(luaurcPath) + ": " + *error);
                }
            }
            else if (luauConfigExists)
            {
                if (std::optional<std::string> contents = readTextFile(luauConfigPath))
                {
                    Luau::ConfigOptions::AliasOptions aliasOpts;
                    aliasOpts.configLocation = PathToUtf8(luauConfigPath);
                    aliasOpts.overwriteAliases = true;
                    Luau::InterruptCallbacks callbacks{};
                    if (std::optional<std::string> error =
                            Luau::extractLuauConfig(*contents, result, aliasOpts, std::move(callbacks)))
                        Lode::Logger::Warn("Ignoring invalid .config.luau at " + PathToUtf8(luauConfigPath) + ": " + *error);
                }
            }

            return configCache_[dir] = result;
        }
    };

    static std::array<uint8_t, 32> HmacSha256(std::string_view key, std::string_view data)
    {
        std::array<uint8_t, 64> paddedKey{};
        if (key.size() > paddedKey.size())
        {
            auto keyHash = Sha256(key);
            std::copy(keyHash.begin(), keyHash.end(), paddedKey.begin());
        }
        else
        {
            std::copy(key.begin(), key.end(), paddedKey.begin());
        }

        std::string inner(paddedKey.size(), '\0');
        for (size_t i = 0; i < paddedKey.size(); ++i)
            inner[i] = static_cast<char>(paddedKey[i] ^ 0x36);
        inner.append(data);
        auto innerHash = Sha256(inner);

        std::string outer(paddedKey.size(), '\0');
        for (size_t i = 0; i < paddedKey.size(); ++i)
            outer[i] = static_cast<char>(paddedKey[i] ^ 0x5c);
        outer.append(reinterpret_cast<const char*>(innerHash.data()), innerHash.size());
        return Sha256(outer);
    }

    static std::string GetCacheSecret(const fs::path& cacheDir)
    {
        fs::path secretPath = cacheDir / "cache.key";
        std::ifstream secretInput(secretPath, std::ios::binary);
        if (secretInput.is_open())
        {
            std::string existing((std::istreambuf_iterator<char>(secretInput)), std::istreambuf_iterator<char>());
            if (existing.size() == 32)
                return existing;
        }

        std::string secret(32, '\0');
        std::random_device random;
        for (char& byte : secret)
            byte = static_cast<char>(random());

        std::ofstream output(secretPath, std::ios::binary | std::ios::trunc);
        if (output.is_open())
        {
            output.write(secret.data(), secret.size());
            output.close();

            std::error_code ec;
            fs::permissions(secretPath,
                fs::perms::owner_read | fs::perms::owner_write,
                fs::perm_options::replace, ec);
        }
        return secret;
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
    fs::path cacheBase;
#ifdef _WIN32
    if (const char* localAppData = std::getenv("LOCALAPPDATA"))
        cacheBase = fs::path(localAppData) / "LodeRuntime";
#else
    if (const char* home = std::getenv("HOME"))
        cacheBase = fs::path(home) / ".cache" / "lode-runtime";
#endif
    if (cacheBase.empty())
        cacheBase = fs::temp_directory_path() / "lode-runtime";

    fs::path lodeCacheDir = cacheBase / "cache";
    std::error_code ec;
    fs::create_directories(lodeCacheDir, ec);
    fs::permissions(lodeCacheDir,
        fs::perms::owner_all,
        fs::perm_options::replace, ec);
    // Evict entries older than 30 days the first time the cache is touched.
    PruneCache(30);
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

// Fingerprint of the resolved Luau config that affects diagnostics. A change to
// .luaurc / .config.luau (e.g. enabling typeErrors, toggling strict mode, or
// enabling a lint rule) produces a different fingerprint, which invalidates the
// cache so the next compile re-runs the type checker under the new config (see
// issue #11). Uses the same FileConfigResolver CompileWithResult uses so the key
// matches the config the compiler would actually apply.
static std::array<uint8_t, 32> ConfigFingerprint(std::string_view source, std::string_view filePath)
{
    std::optional<Luau::Mode> explicitMode;
    FileConfigResolver resolver(source, filePath, explicitMode);

    // Trigger the directory walk (readConfigRec) so the script dir's config is
    // populated, then read the resolved config for the script.
    if (!filePath.empty())
    {
        std::error_code ec;
        fs::path p = fs::absolute(PathFromUtf8(filePath), ec);
        if (!ec)
        {
            Luau::TypeCheckLimits limits;
            resolver.getConfig(PathToUtf8(p), limits);
        }
    }
    const Luau::Config& config = resolver.getScriptConfig();

    // Hash the fields that change diagnostic output. The serialization only needs
    // to be self-consistent (stable across runs) and sensitive to changes.
    std::string bytes;
    bytes.reserve(32);
    bytes.push_back(static_cast<char>(config.mode) & 0xff);
    bytes.push_back(config.typeErrors ? 1 : 0);
    bytes.push_back(config.lintErrors ? 1 : 0);
    bytes.push_back(config.parseOptions.allowDeclarationSyntax ? 1 : 0);
    bytes.push_back(config.parseOptions.captureComments ? 1 : 0);
    bytes.push_back(config.parseOptions.storeCstData ? 1 : 0);
    bytes.push_back(config.parseOptions.noErrorLimit ? 1 : 0);
    for (uint64_t mask : { config.enabledLint.warningMask, config.fatalLint.warningMask })
    {
        for (int i = 0; i < 8; ++i)
            bytes.push_back(static_cast<char>((mask >> (i * 8)) & 0xff));
    }
    for (const auto& g : config.globals)
    {
        bytes.append(g);
        bytes.push_back('\0');
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

    // The key is derived from the source CONTENT (SHA-256), the compile options
    // fingerprint, AND a fingerprint of the resolved Luau config. Editing the
    // module, toggling its directives, or changing its .luaurc / .config.luau
    // (typeErrors, strict mode, lints...) all invalidate the cache so diagnostics
    // reflect the effective config rather than stale cached state. The Luau bytecode
    // version is also included so upgrading the vendored Luau invalidates old
    // entries instead of loading incompatible bytecode (issue #11).
    std::array<uint8_t, 32> contentHash = Sha256(source);
    std::array<uint8_t, 32> optionsFp = OptionsFingerprint(resolvedOptions);
    std::array<uint8_t, 32> configFp = ConfigFingerprint(source, filePath);

    std::string keyMaterial;
    keyMaterial.append(reinterpret_cast<const char*>(contentHash.data()), contentHash.size());
    keyMaterial.append(reinterpret_cast<const char*>(optionsFp.data()), optionsFp.size());
    keyMaterial.append(reinterpret_cast<const char*>(configFp.data()), configFp.size());
    {
        // Bytecode version: runtime supports [MIN, MAX], compiler emits TARGET.
        uint32_t v = LuauBytecodeTag::LBC_VERSION_TARGET;
        for (int i = 0; i < 4; ++i)
            keyMaterial.push_back(static_cast<char>((v >> (i * 8)) & 0xff));
    }
    std::array<uint8_t, 32> keyHash = Sha256(keyMaterial);

    std::error_code ec;
    fs::path srcPath = fs::absolute(PathFromUtf8(filePath), ec);
    if (ec)
        srcPath = PathFromUtf8(filePath);

    std::string cacheFileName = PathToUtf8(srcPath.stem()) + "_" + ToHex(keyHash) + ".luac";
    fs::path cacheDir = fs::path(GetCacheDirectory());
    fs::path cachePath = cacheDir / cacheFileName;
    std::string cacheSecret = GetCacheSecret(cacheDir);

    struct CacheHeader
    {
        char magic[4];           // "LODE" file marker
        uint8_t version;         // Luau bytecode version (LBC_VERSION_TARGET) at write time
        uint8_t contentHash[32]; // SHA-256 of the source (extra validation)
        uint32_t size;           // Bytecode size
        uint8_t authTag[32];     // HMAC-SHA256 over the header and bytecode
    };

    // Try reading from the cache first. The filename encodes the content hash,
    // options + config fingerprints, and bytecode version; the header revalidates
    // the content hash and version, so a stale, corrupt, or version-skewed entry
    // falls through to a fresh compile instead of running.
    if (fs::exists(cachePath))
    {
        std::ifstream cacheFile(cachePath, std::ios::binary);
        if (cacheFile.is_open())
        {
            cacheFile.seekg(0, std::ios::end);
            std::streamoff fileSize = cacheFile.tellg();
            cacheFile.seekg(0, std::ios::beg);
            CacheHeader header{};
            cacheFile.read(reinterpret_cast<char*>(&header), sizeof(CacheHeader));
            if (cacheFile.gcount() == sizeof(CacheHeader) &&
                std::memcmp(header.magic, "LODE", 4) == 0 &&
                header.version == static_cast<uint8_t>(LuauBytecodeTag::LBC_VERSION_TARGET) &&
                std::memcmp(header.contentHash, contentHash.data(), 32) == 0 &&
                fileSize >= static_cast<std::streamoff>(sizeof(CacheHeader)) &&
                header.size == static_cast<uint64_t>(fileSize - sizeof(CacheHeader)))
            {
                std::string cachedBytecode(header.size, '\0');
                cacheFile.read(cachedBytecode.data(), header.size);
                std::string authData(reinterpret_cast<const char*>(&header), offsetof(CacheHeader, authTag));
                authData.append(cachedBytecode);
                auto expectedTag = HmacSha256(cacheSecret, authData);
                if (cacheFile.gcount() == static_cast<std::streamsize>(header.size) &&
                    std::memcmp(header.authTag, expectedTag.data(), expectedTag.size()) == 0)
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

    // Do not cache error bytecodes: luau_compile emits a leading null byte to
    // signal a failed compile, so those must be re-validated on every run.
    if (compiledBytecode[0] != 0)
    {
        if (compiledBytecode.size() <= std::numeric_limits<uint32_t>::max())
        {
            CacheHeader header{};
            std::memcpy(header.magic, "LODE", 4);
            header.version = static_cast<uint8_t>(LuauBytecodeTag::LBC_VERSION_TARGET);
            std::memcpy(header.contentHash, contentHash.data(), 32);
            header.size = static_cast<uint32_t>(compiledBytecode.size());

            std::string authData(reinterpret_cast<const char*>(&header), offsetof(CacheHeader, authTag));
            authData.append(compiledBytecode);
            auto authTag = HmacSha256(cacheSecret, authData);
            std::memcpy(header.authTag, authTag.data(), authTag.size());

            fs::path temporaryPath = cachePath;
            temporaryPath += ".tmp." + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            temporaryPath += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

            std::ofstream temporaryOut(temporaryPath, std::ios::binary | std::ios::trunc);
            if (temporaryOut.is_open())
            {
                temporaryOut.write(reinterpret_cast<const char*>(&header), sizeof(CacheHeader));
                temporaryOut.write(compiledBytecode.data(), compiledBytecode.size());
                temporaryOut.close();

                std::error_code renameError;
                fs::rename(temporaryPath, cachePath, renameError);
                if (renameError)
                    fs::remove(temporaryPath, renameError);
            }
        }
    }

    return compiledBytecode;
}

// Evicts cache entries older than `ttlDays` days. The OS temp dir is shared and
// never otherwise cleaned, so without this the cache grows without bound (one file
// per unique source + options + config combination — see issue #11). Called once
// per process via GetCacheDirectory(); the static guard keeps it to a single pass.
void Compiler::PruneCache(int ttlDays)
{
    static bool s_pruned = false;
    if (s_pruned)
        return;
    s_pruned = true;

    fs::path cacheDir = fs::path(GetCacheDirectory());
    std::error_code ec;
    if (!fs::exists(cacheDir, ec))
        return;

    // file_clock has an unspecified epoch, so convert each entry's write time to
    // system_clock (whose epoch is well-defined) before comparing against the cutoff.
    auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24 * ttlDays);

    for (auto it = fs::directory_iterator(cacheDir, ec); it != fs::directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        const auto& entry = *it;
        if (!entry.is_regular_file(ec))
            continue;
        auto lastWrite = entry.last_write_time(ec);
        if (ec)
            continue;
        auto sysWrite = std::chrono::clock_cast<std::chrono::system_clock>(lastWrite);
        if (sysWrite < cutoff)
            fs::remove(entry.path(), ec);
    }
}

} // namespace Lode
