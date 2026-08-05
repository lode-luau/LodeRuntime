// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "ModuleLoader.hpp"
#include "Lode/Compiler.hpp"
#include "Platform/Platform.hpp"
#include "Luau/Require.h"
#include "Luau/Compiler.h"
#include "Luau/Config.h"
#include "Luau/LuauConfig.h"
#include "nlohmann/json.hpp"

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;

namespace Lode
{

fs::path FindLodeJson(const fs::path& startPath)
{
    fs::path current = startPath;
    if (fs::is_regular_file(current))
        current = current.parent_path();
        
    while (current.has_parent_path())
    {
        if (fs::exists(current / "lode.json") || fs::exists(current / "init.luau"))
            return current;
        current = current.parent_path();
    }
    return "";
}

struct LodeNavigationContext
{
    NativeModuleRegistry* registry = nullptr;
    std::vector<std::string> modulePaths;
    fs::path currentPath;
    fs::path rootPath;
    // Root directory of the current package (the folder that contains init.luau or lode.json).
    // Used to resolve @self aliases to the package's own internal files.
    fs::path packagePath;
};

typedef int (*LodeModuleInitFn)(lua_State* L);

static luarequire_WriteResult WriteBuffer(const std::string& str, char* buffer, size_t bufferSize, size_t* sizeOut)
{
    size_t requiredSize = str.size() + 1;
    if (bufferSize < requiredSize)
    {
        *sizeOut = requiredSize;
        return luarequire_WriteResult::WRITE_BUFFER_TOO_SMALL;
    }
    *sizeOut = requiredSize;
    memcpy(buffer, str.c_str(), requiredSize);
    return luarequire_WriteResult::WRITE_SUCCESS;
}

static bool is_require_allowed(lua_State* L, void* ctx, const char* requirer_chunkname)
{
    return true;
}

static luarequire_NavigateResult reset(lua_State* L, void* ctx, const char* requirer_chunkname)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    std::string chunkStr = requirer_chunkname ? requirer_chunkname : "";

    if (!chunkStr.empty() && (chunkStr[0] == '@' || chunkStr[0] == '='))
    {
        chunkStr = chunkStr.substr(1);
    }

    try
    {
        fs::path p(chunkStr);
        if (p.is_relative())
        {
            p = fs::current_path() / p;
        }

        fs::path canonicalP = fs::weakly_canonical(p);

        if (fs::is_regular_file(canonicalP))
        {
            if (canonicalP.filename() == "init.luau")
            {
                nav->currentPath = canonicalP.parent_path().parent_path();
                nav->packagePath = canonicalP.parent_path();
            }
            else
            {
                nav->currentPath = canonicalP.parent_path();
                nav->packagePath = canonicalP.parent_path();
            }
        }
        else
        {
            nav->packagePath = FindLodeJson(p);
            nav->currentPath = p.parent_path();
        }

        nav->rootPath = nav->currentPath;
        return NAVIGATE_SUCCESS;
    }
    catch (...)
    {
        return NAVIGATE_NOT_FOUND;
    }
}

static std::optional<fs::path> ResolveAliasRecursively(const fs::path& startDir, const std::string& aliasName)
{
    fs::path current = startDir;
    while (true)
    {
        fs::path luauConfigPath = current / ".config.luau";
        fs::path luaurcPath = current / ".luaurc";
        
        std::string configPathToUse;
        bool isLuau = false;
        
        if (fs::exists(luauConfigPath))
        {
            configPathToUse = luauConfigPath.string();
            isLuau = true;
        }
        else if (fs::exists(luaurcPath))
        {
            configPathToUse = luaurcPath.string();
        }
        
        if (!configPathToUse.empty())
        {
            std::ifstream file(configPathToUse);
            if (file.is_open())
            {
                std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                Luau::Config result;
                Luau::ConfigOptions::AliasOptions aliasOpts;
                aliasOpts.configLocation = configPathToUse;
                aliasOpts.overwriteAliases = true;
                
                if (isLuau)
                {
                    Luau::InterruptCallbacks callbacks{};
                    std::optional<std::string> err = Luau::extractLuauConfig(contents, result, aliasOpts, std::move(callbacks));
                    if (err)
                    {
                        Lode::Logger::Info("Failed to extract config in " + configPathToUse + ": " + *err);
                    }
                }
                else
                {
                    Luau::ConfigOptions opts;
                    opts.aliasOptions = std::move(aliasOpts);
                    Luau::parseConfig(contents, result, opts);
                }
                
                std::string lowerAlias = aliasName;
                for (char& c : lowerAlias)
                {
                    if (c >= 'A' && c <= 'Z')
                        c += ('a' - 'A');
                }

                auto it = result.aliases.find(lowerAlias);
                if (it != nullptr)
                {
                    return fs::path(std::string(it->configLocation)).parent_path() / it->value;
                }
            }
        }
        
        if (!current.has_parent_path() || current.parent_path() == current)
            break;
            
        current = current.parent_path();
    }
    
    return std::nullopt;
}

static luarequire_NavigateResult jump_to_alias(lua_State* L, void* ctx, const char* path)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    std::string aliasStr = path ? path : "";
    try
    {
        if (aliasStr == "@self" || aliasStr == "self")
        {
            // @self always resolves to the package's own directory (the folder containing
            // init.luau or lode.json), allowing native and Luau modules to require their
            // own internal files regardless of where the caller is located.
            if (!nav->packagePath.empty())
            {
                nav->currentPath = nav->packagePath;
            }
            return NAVIGATE_SUCCESS;
        }
        
        std::string lookupName = aliasStr;
        if (!lookupName.empty() && lookupName[0] == '@')
            lookupName = lookupName.substr(1);
            
        auto resolvedAlias = ResolveAliasRecursively(nav->rootPath, lookupName);
        if (resolvedAlias)
        {
            nav->currentPath = fs::weakly_canonical(*resolvedAlias);
            return NAVIGATE_SUCCESS;
        }

        fs::path p(aliasStr);
        if (p.is_relative())
        {
            p = nav->rootPath / p;
        }
        nav->currentPath = fs::weakly_canonical(p);
        return NAVIGATE_SUCCESS;
    }
    catch (...)
    {
        return NAVIGATE_NOT_FOUND;
    }
}

static luarequire_NavigateResult to_parent(lua_State* L, void* ctx)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    if (nav->currentPath.has_parent_path())
    {
        nav->currentPath = nav->currentPath.parent_path();
        return NAVIGATE_SUCCESS;
    }
    return NAVIGATE_NOT_FOUND;
}

static luarequire_NavigateResult to_child(lua_State* L, void* ctx, const char* name)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    std::string childName = name ? name : "";
    nav->currentPath /= childName;
    try
    {
        nav->currentPath = fs::weakly_canonical(nav->currentPath);
    }
    catch (...) {}
    return NAVIGATE_SUCCESS;
}

static bool check_path_exists(const fs::path& p)
{
    if (fs::is_regular_file(p)) return true;
    if (fs::is_regular_file(p.string() + ".luau")) return true;
    if (fs::is_regular_file(p / "init.luau")) return true;
    if (fs::is_regular_file(p / "lode.json")) return true;
    return false;
}

static bool is_module_present(lua_State* L, void* ctx)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    fs::path p = nav->currentPath;

    if (check_path_exists(p))
        return true;

    for (const auto& searchDir : nav->modulePaths)
    {
        fs::path searchPath = fs::path(searchDir) / p.filename();
        if (check_path_exists(searchPath))
        {
            nav->currentPath = fs::weakly_canonical(searchPath);
            return true;
        }
    }

    return false;
}

static luarequire_WriteResult get_chunkname(lua_State* L, void* ctx, char* buffer, size_t buffer_size, size_t* size_out)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    return WriteBuffer("@" + nav->currentPath.string(), buffer, buffer_size, size_out);
}

static luarequire_WriteResult get_loadname(lua_State* L, void* ctx, char* buffer, size_t buffer_size, size_t* size_out)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    return WriteBuffer(nav->currentPath.string(), buffer, buffer_size, size_out);
}

static luarequire_WriteResult get_cache_key(lua_State* L, void* ctx, char* buffer, size_t buffer_size, size_t* size_out)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    return WriteBuffer(nav->currentPath.string(), buffer, buffer_size, size_out);
}

static luarequire_ConfigStatus get_config_status(lua_State* L, void* ctx)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    fs::path luaurc = nav->currentPath / ".luaurc";
    fs::path luauConfig = nav->currentPath / ".config.luau";

    if (fs::exists(luaurc) && fs::exists(luauConfig))
        return CONFIG_AMBIGUOUS;
    if (fs::exists(luauConfig))
        return CONFIG_PRESENT_LUAU;
    if (fs::exists(luaurc))
        return CONFIG_PRESENT_JSON;

    return CONFIG_ABSENT;
}

static luarequire_WriteResult get_config(lua_State* L, void* ctx, char* buffer, size_t buffer_size, size_t* size_out)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    fs::path luaurc = nav->currentPath / ".luaurc";
    fs::path luauConfig = nav->currentPath / ".config.luau";

    fs::path target;
    if (fs::exists(luauConfig)) target = luauConfig;
    else if (fs::exists(luaurc)) target = luaurc;

    if (!target.empty())
    {
        std::ifstream f(target, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return WriteBuffer(content, buffer, buffer_size, size_out);
    }
    return luarequire_WriteResult::WRITE_FAILURE;
}

static int LoadModuleImpl(lua_State* L, void* ctx, const char* path, const char* chunkname, const char* loadname)
{
    State vm(L);
    LodeNavigationContext* loaderCtx = static_cast<LodeNavigationContext*>(ctx);
    std::string rawPathStr(path ? path : "");
    std::string loadNameStr(loadname ? loadname : "");

    // 1. Check for Static Module Registration (Production Bundling / iOS / App Store Sandbox)
    if (loaderCtx && loaderCtx->registry)
    {
        if (loaderCtx->registry->HasStaticModule(rawPathStr))
        {
            auto initFn = loaderCtx->registry->GetStaticModule(rawPathStr);
            if (initFn) return initFn(L);
        }
        if (!loadNameStr.empty() && loaderCtx->registry->HasStaticModule(loadNameStr))
        {
            auto initFn = loaderCtx->registry->GetStaticModule(loadNameStr);
            if (initFn) return initFn(L);
        }
    }
    if (NativeModuleRegistry::GetGlobalRegistry().HasStaticModule(rawPathStr))
    {
        auto initFn = NativeModuleRegistry::GetGlobalRegistry().GetStaticModule(rawPathStr);
        if (initFn) return initFn(L);
    }
    if (!loadNameStr.empty() && NativeModuleRegistry::GetGlobalRegistry().HasStaticModule(loadNameStr))
    {
        auto initFn = NativeModuleRegistry::GetGlobalRegistry().GetStaticModule(loadNameStr);
        if (initFn) return initFn(L);
    }

    fs::path targetPath(loadname ? loadname : (path ? path : ""));

    // Resolve via the search directories registered through State::AddModulePath
    // when the module does not exist relative to the requirer. The direct path
    // always wins: the fallback only runs when check_path_exists fails.
    if (loaderCtx && !check_path_exists(targetPath))
    {
        for (const auto& searchDir : loaderCtx->modulePaths)
        {
            fs::path candidate = fs::path(searchDir) / targetPath.filename();
            if (check_path_exists(candidate))
            {
                targetPath = candidate;
                break;
            }
        }
    }

    fs::path dirPath = targetPath;
    if (fs::is_directory(targetPath))
    {
        dirPath = targetPath;
    }
    else if (fs::is_regular_file(targetPath))
    {
        dirPath = targetPath.parent_path();
    }

    // Check for lode.json
    fs::path lodeJsonPath = dirPath / "lode.json";
    if (fs::exists(lodeJsonPath))
    {
        nlohmann::json jsonDoc;
        try
        {
            std::ifstream f(lodeJsonPath);
            jsonDoc = nlohmann::json::parse(f);
        }
        catch (const std::exception& e)
        {
            vm.RaiseError("Failed to parse lode.json in " + dirPath.string() + ": " + e.what());
            return 0;
        }

        if (jsonDoc.contains("libraries") && jsonDoc["libraries"].is_object())
        {
            std::string platform = std::string(Platform::GetOSName());
            std::string arch = std::string(Platform::GetArchitectureName());

            if (jsonDoc["libraries"].contains(platform) && jsonDoc["libraries"][platform].contains(arch))
            {
                std::string relLibPath = jsonDoc["libraries"][platform][arch];
                fs::path fullLibPath = dirPath / relLibPath;

                // Prefer the module binary next to the executable: the build drops a
                // config-matched copy there (Debug or Release), while the lode.json
                // "libs/" copy is overwritten by whichever configuration was built
                // last. Loading a module built with a different CRT/std::string ABI
                // than the running exe corrupts cross-boundary strings and crashes.
                std::string exePath = Platform::GetExecutablePath();
                if (!exePath.empty())
                {
                    fs::path exeDirCandidate = fs::path(exePath).parent_path() / fullLibPath.filename();
                    if (fs::exists(exeDirCandidate))
                        fullLibPath = exeDirCandidate;
                }

                auto libResult = Platform::DynamicLibrary::Open(fullLibPath.string());
                if (libResult.IsOk())
                {
                    auto lib = libResult.GetValue();
                    if (loaderCtx && loaderCtx->registry)
                    {
                        loaderCtx->registry->RegisterModule(loadname ? loadname : path, lib);
                    }

                    auto symResult = lib->GetSymbol("LodeModuleInit");
                    if (symResult.IsOk())
                    {
                        LodeModuleInitFn initFn = reinterpret_cast<LodeModuleInitFn>(symResult.GetValue());

                        lua_pushstring(L, dirPath.string().c_str());
                        lua_setfield(L, LUA_REGISTRYINDEX, "_LODE_NATIVE_MODULE_PATH");

                        int nret = initFn(L);

                        lua_pushnil(L);
                        lua_setfield(L, LUA_REGISTRYINDEX, "_LODE_NATIVE_MODULE_PATH");

                        return nret;
                    }
                    else
                    {
                        vm.RaiseError("Failed to find LodeModuleInit symbol in native library " + fullLibPath.string());
                        return 0;
                    }
                }
                else
                {
                    vm.RaiseError(libResult.GetError().ErrorMessage());
                    return 0;
                }
            }
        }
    }

    // Pure Luau module resolution. Always try the ".luau" variant: fs::path
    // treats "sanity.config" as having extension ".config", so a has_extension()
    // check would wrongly skip "sanity.config.luau" and fall through to init.luau.
    fs::path scriptToLoad;
    fs::path initLuauPath = dirPath / "init.luau";
    fs::path exactFile = targetPath;
    fs::path luauVariant(targetPath.string() + ".luau");

    if (fs::is_regular_file(exactFile))
    {
        scriptToLoad = exactFile;
    }
    else if (fs::is_regular_file(luauVariant))
    {
        scriptToLoad = luauVariant;
    }
    else if (fs::is_regular_file(initLuauPath))
    {
        scriptToLoad = initLuauPath;
    }
    else
    {
        vm.RaiseError("Module not found at path: " + targetPath.string());
        return 0;
    }

    std::ifstream srcFile(scriptToLoad, std::ios::binary);
    if (!srcFile.is_open())
    {
        vm.RaiseError("Could not open module file: " + scriptToLoad.string());
        return 0;
    }
    std::string source((std::istreambuf_iterator<char>(srcFile)), std::istreambuf_iterator<char>());

    std::string bytecode = Compiler::CompileWithCache(source, scriptToLoad.string());
    if (bytecode.empty())
    {
        vm.RaiseError("Failed to compile module: " + scriptToLoad.string());
        return 0;
    }

    std::string modChunkName = "@" + scriptToLoad.string();
    auto execResult = vm.ExecuteBytecodeWithResults(bytecode, modChunkName);

    if (execResult.IsError())
    {
        vm.RaiseError(execResult.GetError().ErrorMessage());
        return 0;
    }

    return execResult.GetValue();
}

std::string GetCallerChunkName(lua_State* L)
{
    lua_Debug ar;
    std::string requirerChunkname;

    lua_getfield(L, LUA_REGISTRYINDEX, "_LODE_NATIVE_MODULE_PATH");
    if (lua_isstring(L, -1))
    {
        requirerChunkname = "@" + (fs::path(lua_tostring(L, -1)) / "init.luau").string();
    }
    lua_pop(L, 1);

    if (requirerChunkname.empty())
    {
        for (int level = 1; level <= 10; ++level)
        {
            if (!lua_getinfo(L, level, "s", &ar)) break;
            if (ar.what[0] != 'C')
            {
                requirerChunkname = ar.source;
                break;
            }
        }
    }
    return requirerChunkname;
}

static int MultiReturnLodeRequire(lua_State* L)
{
    std::string requirerChunknameStr = GetCallerChunkName(L);
    const char* requirerChunkname = requirerChunknameStr.c_str();

    const char* pathStr = luaL_checkstring(L, 1);
    luarequire_Configuration* config = static_cast<luarequire_Configuration*>(lua_touserdata(L, lua_upvalueindex(1)));
    void* ctx = lua_touserdata(L, lua_upvalueindex(2));

    // Reset navigation to requirer context
    if (config->reset(L, ctx, requirerChunkname) != NAVIGATE_SUCCESS)
    {
        luaL_error(L, "Failed to navigate to requirer context: %s", requirerChunkname);
        return 0;
    }

    // Navigate to the target module, handling three distinct path forms:
    //   "@alias/sub/path" — jump to the alias root, then descend into the remainder
    //   "./relative"      — descend relative to the requirer's base directory
    //   "plain/name"      — descend from the requirer's base directory as-is
    std::string relPath(pathStr);
    if (!relPath.empty() && relPath[0] == '@')
    {
        // Split "@alias" from "sub/path" at the first slash after the '@'.
        size_t slashPos = relPath.find('/', 1);
        std::string aliasName = (slashPos != std::string::npos)
            ? relPath.substr(0, slashPos)  // e.g. "@self"
            : relPath;                      // e.g. "@self" with no sub-path
        std::string remainder = (slashPos != std::string::npos)
            ? relPath.substr(slashPos + 1) // e.g. "utils" or "sub/module"
            : "";

        if (config->jump_to_alias(L, ctx, aliasName.c_str()) != NAVIGATE_SUCCESS)
        {
            luaL_error(L, "Unknown module alias: %s", aliasName.c_str());
            return 0;
        }

        if (!remainder.empty())
        {
            config->to_child(L, ctx, remainder.c_str());
        }
    }
    else
    {
        if (relPath.rfind("./", 0) == 0)
        {
            relPath = relPath.substr(2);
        }
        config->to_child(L, ctx, relPath.c_str());
    }

    char loadnameBuf[1024];
    size_t loadnameSize = 0;
    config->get_loadname(L, ctx, loadnameBuf, sizeof(loadnameBuf), &loadnameSize);

    char chunknameBuf[1024];
    size_t chunknameSize = 0;
    config->get_chunkname(L, ctx, chunknameBuf, sizeof(chunknameBuf), &chunknameSize);

    char cacheKeyBuf[1024];
    size_t cacheKeySize = 0;
    config->get_cache_key(L, ctx, cacheKeyBuf, sizeof(cacheKeyBuf), &cacheKeySize);

    std::string cacheKey(cacheKeyBuf);

    // 1. Get or create _LODE_MULTI_CACHE in registry
    lua_getfield(L, LUA_REGISTRYINDEX, "_LODE_MULTI_CACHE");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_LODE_MULTI_CACHE");
    }

    // Stack: (1) pathStr, (2) cacheTable
    lua_getfield(L, 2, cacheKey.c_str()); // Stack: (1) pathStr, (2) cacheTable, (3) cachedTuple
    if (!lua_isnil(L, -1))
    {
        if (lua_istable(L, -1))
        {
            lua_getfield(L, -1, "_nresults");
            int nres = static_cast<int>(lua_tonumber(L, -1));
            lua_pop(L, 1);

            for (int i = 1; i <= nres; ++i)
            {
                lua_rawgeti(L, 3, i);
            }
            lua_remove(L, 3); // remove cachedTuple
            lua_remove(L, 2); // remove cacheTable
            return nres;
        }
    }
    lua_pop(L, 1); // pop nil

    // 2. Load module (pushes N results onto stack above slot 2)
    int nresults = config->load(L, ctx, pathStr, chunknameBuf, loadnameBuf);

    if (nresults <= 0)
    {
        lua_remove(L, 2); // remove cacheTable
        return 0;
    }

    // 3. Store tuple table in _LODE_MULTI_CACHE[cacheKey]
    lua_newtable(L); // tupleTable pushed at slot (3 + nresults)
    lua_pushinteger(L, nresults);
    lua_setfield(L, -2, "_nresults");

    for (int i = 1; i <= nresults; ++i)
    {
        lua_pushvalue(L, 2 + i);
        lua_rawseti(L, -2, i);
    }

    lua_setfield(L, 2, cacheKey.c_str()); // pops tupleTable

    lua_remove(L, 2); // remove cacheTable from stack
    return nresults; // Return all N results directly to Luau caller!
}

void SetupModuleLoader(lua_State* L, NativeModuleRegistry* registry, const std::vector<std::string>& modulePaths)
{
    // Per-State navigation context. Owned by a GC-tracked full userdata so its
    // std::vectors are destroyed when the VM is collected. It is also referenced
    // from the registry so UpdateModulePaths can reach it and the require closure
    // can safely hold it as an upvalue for the whole State lifetime.
    auto* ctxMem = static_cast<LodeNavigationContext*>(lua_newuserdata(L, sizeof(LodeNavigationContext)));
    auto* ctx = new (ctxMem) LodeNavigationContext();
    ctx->registry = registry;
    ctx->modulePaths = modulePaths;

    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* L) -> int {
        auto* c = static_cast<LodeNavigationContext*>(lua_touserdata(L, 1));
        if (c) c->~LodeNavigationContext();
        return 0;
    }, "__gc");
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);

    // Keep a registry reference so the context lives as long as the State and is
    // reachable by UpdateModulePaths. The registry is per-lua_State, so two State
    // instances no longer share (and clobber) a single navigation context.
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "_LODE_NAVIGATION_CTX");

    void* ud = (lua_newuserdata(L, sizeof(luarequire_Configuration)));
    luarequire_Configuration* config = new (ud) luarequire_Configuration{};

    config->is_require_allowed = is_require_allowed;
    config->reset = reset;
    config->jump_to_alias = jump_to_alias;
    config->to_parent = to_parent;
    config->to_child = to_child;
    config->is_module_present = is_module_present;
    config->get_config_status = get_config_status;
    config->get_chunkname = get_chunkname;
    config->get_loadname = get_loadname;
    config->get_cache_key = get_cache_key;
    config->get_config = get_config;
    config->load = LoadModuleImpl;

    // The context userdata is at -2 (upvalue 2), the config userdata at -1 (upvalue 1).
    lua_pushvalue(L, -2);
    lua_pushcclosure(L, MultiReturnLodeRequire, "require", 2);
    lua_setglobal(L, "require");
    lua_pop(L, 1); // pop the original context userdata
}

void UpdateModulePaths(lua_State* L, const std::vector<std::string>& modulePaths)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "_LODE_NAVIGATION_CTX");
    if (auto* ctx = static_cast<LodeNavigationContext*>(lua_touserdata(L, -1)))
    {
        ctx->modulePaths = modulePaths;
    }
    lua_pop(L, 1);
}

} // namespace Lode
