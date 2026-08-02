#include "ModuleLoader.hpp"
#include "Platform/Platform.hpp"
#include "Luau/Require.h"
#include "Luau/Compiler.h"
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

struct LodeNavigationContext
{
    NativeModuleRegistry* registry = nullptr;
    std::vector<std::string> modulePaths;
    fs::path currentPath;
    fs::path rootPath;
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
            nav->currentPath = canonicalP.parent_path();
        }
        else
        {
            nav->currentPath = canonicalP;
        }

        nav->rootPath = nav->currentPath;
        return NAVIGATE_SUCCESS;
    }
    catch (...)
    {
        return NAVIGATE_NOT_FOUND;
    }
}

static luarequire_NavigateResult jump_to_alias(lua_State* L, void* ctx, const char* path)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    try
    {
        fs::path p(path ? path : "");
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
    return NAVIGATE_SUCCESS;
}

static bool is_module_present(lua_State* L, void* ctx)
{
    LodeNavigationContext* nav = static_cast<LodeNavigationContext*>(ctx);
    fs::path p = nav->currentPath;

    if (fs::is_regular_file(p))
        return true;

    fs::path withLuau = p;
    withLuau += ".luau";
    if (fs::is_regular_file(withLuau))
        return true;

    fs::path initLuau = p / "init.luau";
    if (fs::is_regular_file(initLuau))
        return true;

    fs::path lodeJson = p / "lode.json";
    if (fs::is_regular_file(lodeJson))
        return true;

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
    fs::path targetPath(loadname ? loadname : (path ? path : ""));

    fs::path dirPath = targetPath;
    if (fs::is_regular_file(targetPath))
    {
        dirPath = targetPath.parent_path();
    }

    // Check for lode.json
    fs::path lodeJsonPath = dirPath / "lode.json";
    if (fs::exists(lodeJsonPath))
    {
        try
        {
            std::ifstream f(lodeJsonPath);
            nlohmann::json jsonDoc = nlohmann::json::parse(f);

            if (jsonDoc.contains("libraries") && jsonDoc["libraries"].is_object())
            {
                std::string platform = std::string(Platform::GetOSName());
                std::string arch = std::string(Platform::GetArchitectureName());

                if (jsonDoc["libraries"].contains(platform) && jsonDoc["libraries"][platform].contains(arch))
                {
                    std::string relLibPath = jsonDoc["libraries"][platform][arch];
                    fs::path fullLibPath = dirPath / relLibPath;

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
                            return initFn(L);
                        }
                        else
                        {
                            vm.RaiseError("Failed to find LodeModuleInit symbol in native library " + fullLibPath.string());
                            return 0;
                        }
                    }
                    else
                    {
                        vm.RaiseError(libResult.GetError().GetMessage());
                        return 0;
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            vm.RaiseError("Failed to parse lode.json in " + dirPath.string() + ": " + e.what());
            return 0;
        }
    }

    // Pure Luau module resolution
    fs::path scriptToLoad;
    fs::path initLuauPath = dirPath / "init.luau";
    fs::path directFile = targetPath;
    if (!directFile.has_extension())
    {
        directFile += ".luau";
    }

    if (fs::is_regular_file(targetPath))
    {
        scriptToLoad = targetPath;
    }
    else if (fs::is_regular_file(directFile))
    {
        scriptToLoad = directFile;
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

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source.c_str(), source.length(), nullptr, &bytecodeSize);
    if (!bytecode)
    {
        vm.RaiseError("Failed to compile module: " + scriptToLoad.string());
        return 0;
    }

    std::string modChunkName = "@" + scriptToLoad.string();
    std::string_view bcView(bytecode, bytecodeSize);
    auto execResult = vm.ExecuteBytecodeWithResults(bcView, modChunkName);
    free(bytecode);

    if (execResult.IsError())
    {
        vm.RaiseError(execResult.GetError().GetMessage());
        return 0;
    }

    return execResult.GetValue();
}

static int MultiReturnLodeRequire(lua_State* L)
{
    lua_Debug ar;
    const char* requirerChunkname = "";
    if (lua_getinfo(L, 1, "s", &ar) && ar.what[0] != 'C')
    {
        requirerChunkname = ar.source;
    }

    const char* pathStr = luaL_checkstring(L, 1);
    luarequire_Configuration* config = static_cast<luarequire_Configuration*>(lua_touserdata(L, lua_upvalueindex(1)));
    void* ctx = lua_tolightuserdata(L, lua_upvalueindex(2));

    // Reset navigation to requirer context
    if (config->reset(L, ctx, requirerChunkname) != NAVIGATE_SUCCESS)
    {
        luaL_error(L, "Failed to navigate to requirer context: %s", requirerChunkname);
        return 0;
    }

    // Process relative path components
    std::string relPath(pathStr);
    if (relPath.rfind("./", 0) == 0)
    {
        relPath = relPath.substr(2);
    }

    config->to_child(L, ctx, relPath.c_str());

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
    static LodeNavigationContext ctx;
    ctx.registry = registry;
    ctx.modulePaths = modulePaths;

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

    lua_pushlightuserdata(L, &ctx);
    lua_pushcclosure(L, MultiReturnLodeRequire, "require", 2);
    lua_setglobal(L, "require");
}

} // namespace Lode
