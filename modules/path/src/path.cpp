#include "Lode/State.hpp"
#include "Lode/Module.hpp"
#include "Lode/Logger.hpp"
#include "ModuleLoader.hpp"
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static fs::path ResolveImpl(Lode::State& vm, const std::string& rawPathStr)
{
    if (rawPathStr.empty()) return fs::current_path();

    std::string requirerChunknameStr = Lode::GetCallerChunkName(vm.GetLuaState());
    fs::path callerPath;
    if (!requirerChunknameStr.empty() && (requirerChunknameStr[0] == '@' || requirerChunknameStr[0] == '='))
    {
        callerPath = requirerChunknameStr.substr(1);
    }
    else
    {
        callerPath = requirerChunknameStr;
    }

    fs::path packagePath;
    fs::path currentPath = fs::current_path();

    if (!callerPath.empty())
    {
        if (callerPath.is_absolute())
        {
            // Purely lexical caller detection: the runtime always loads
            // modules with an "@<absolute path>" chunkname, so the caller's
            // directory is known without touching the filesystem. The
            // previous implementation canonicalized the caller path and
            // stat()ed it on every call (per-component disk I/O on Windows).
            packagePath = callerPath.parent_path();
            if (callerPath.filename() == "init.luau" || callerPath.filename() == "init.lua")
                currentPath = packagePath.parent_path();
            else
                currentPath = packagePath;
        }
        else
        {
            // Non-file caller (e.g. "=stdin"): fall back to the package root
            // search. This path is cold; only the absolute case is hot.
            packagePath = Lode::FindLodeJson(callerPath);
            currentPath = callerPath.parent_path();
            if (currentPath.empty())
                currentPath = fs::current_path();
        }
    }

    std::string relPath = rawPathStr;
    if (!relPath.empty() && relPath[0] == '@')
    {
        size_t slashPos = relPath.find('/', 1);
        std::string aliasName = (slashPos != std::string::npos) ? relPath.substr(0, slashPos) : relPath;
        std::string remainder = (slashPos != std::string::npos) ? relPath.substr(slashPos + 1) : "";

        if (aliasName == "@self" || aliasName == "self")
        {
            currentPath = packagePath.empty() ? currentPath : packagePath;
        }
        else
        {
            fs::path p(aliasName);
            if (p.is_relative()) p = fs::current_path() / p;
            currentPath = fs::weakly_canonical(p);
        }

        if (!remainder.empty())
        {
            currentPath /= remainder;
        }
    }
    else
    {
        currentPath /= relPath;
    }

    return currentPath.lexically_normal();
}

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    exports.Set("resolve", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        fs::path resolved = fs::current_path();
        if (args.Size() > 0)
        {
            resolved = ResolveImpl(vm, args[0].AsString());
            for (size_t i = 1; i < args.Size(); ++i)
            {
                resolved /= args[i].AsString();
            }
        }
        return Lode::Value(resolved.lexically_normal().string());
    }));

    exports.Set("join", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() == 0) return Lode::Value("");
        fs::path p = args[0].AsString();
        for (size_t i = 1; i < args.Size(); ++i)
        {
            p /= args[i].AsString();
        }
        // Pure lexical normalization: collapses "." and ".." without touching
        // the filesystem. weakly_canonical used to be applied here, which made
        // every join perform per-component disk I/O (CreateFile +
        // GetFinalPathNameByHandle per component on Windows).
        return Lode::Value(p.lexically_normal().string());
    }));

    exports.Set("basename", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() == 0) return Lode::Value("");
        fs::path p = args[0].AsString();
        return Lode::Value(p.filename().string());
    }));

    exports.Set("dirname", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() == 0) return Lode::Value("");
        fs::path p = args[0].AsString();
        return Lode::Value(p.parent_path().string());
    }));

    exports.Set("extname", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() == 0) return Lode::Value("");
        fs::path p = args[0].AsString();
        return Lode::Value(p.extension().string());
    }));

    return { Lode::Value(exports) };
}
