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

#include "Path.hpp"

namespace lodefs::path
{

void BindPathMethods(Lode::Exports& exports)
{
    exports.Function("resolve", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        fs::path resolved = fs::current_path();
        if (!args.empty())
        {
            if (!args[0].IsString())
            {
                vm.RaiseError("path.resolve: path must be a string");
                return Lode::Value();
            }
            resolved = ResolveImpl(vm, args[0].AsString());
            for (size_t i = 1; i < args.Size(); ++i)
            {
                if (!args[i].IsString())
                {
                    vm.RaiseError("path.resolve: path must be a string");
                    return Lode::Value();
                }
                resolved /= args[i].AsString();
            }
        }
        return Lode::Value(resolved.lexically_normal().string());
    });

    exports.Function("join", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty()) return Lode::Value("");
        for (const auto a : args)
        {
            if (!a.IsString())
            {
                vm.RaiseError("path.join: path must be a string");
                return Lode::Value();
            }
        }
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
    });

    exports.Function("basename", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty()) return Lode::Value("");
        if (!args[0].IsString())
        {
            vm.RaiseError("path.basename: path must be a string");
            return Lode::Value();
        }
        fs::path p = args[0].AsString();
        return Lode::Value(p.filename().string());
    });

    exports.Function("dirname", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty()) return Lode::Value("");
        if (!args[0].IsString())
        {
            vm.RaiseError("path.dirname: path must be a string");
            return Lode::Value();
        }
        fs::path p = args[0].AsString();
        return Lode::Value(p.parent_path().string());
    });

    exports.Function("extname", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty()) return Lode::Value("");
        if (!args[0].IsString())
        {
            vm.RaiseError("path.extname: path must be a string");
            return Lode::Value();
        }
        fs::path p = args[0].AsString();
        return Lode::Value(p.extension().string());
    });

    exports.Function("isAbsolute", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty() || !args[0].IsString())
        {
            vm.RaiseError("path.isAbsolute: path must be a string");
            return Lode::Value();
        }
        fs::path p = args[0].AsString();
        return Lode::Value(p.is_absolute());
    });

    exports.Function("normalize", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty() || !args[0].IsString())
        {
            vm.RaiseError("path.normalize: path must be a string");
            return Lode::Value();
        }
        fs::path p = args[0].AsString();
        // Purely lexical; collapses "." and ".." and duplicate separators.
        return Lode::Value(p.lexically_normal().string());
    });

    exports.Function("relative", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() < 2 || !args[0].IsString() || !args[1].IsString())
        {
            vm.RaiseError("path.relative: expected (from: string, to: string)");
            return Lode::Value();
        }
        fs::path from = args[0].AsString();
        fs::path to = args[1].AsString();
        if (from.empty()) from = fs::current_path();
        if (!from.is_absolute()) from = fs::current_path() / from;
        if (to.empty()) to = fs::current_path();
        if (!to.is_absolute()) to = fs::current_path() / to;
        return Lode::Value(fs::relative(to, from).string());
    });

    exports.Function("parse", [](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.empty() || !args[0].IsString())
        {
            vm.RaiseError("path.parse: path must be a string");
            return Lode::Value();
        }
        fs::path p = args[0].AsString();
        std::string name = p.filename().string();
        std::string ext = p.extension().string();
        std::string base = name;
        if (!ext.empty() && base.size() >= ext.size())
            base.resize(base.size() - ext.size());
        Lode::Table result = vm.CreateTable();
        result.Set("root", Lode::Value(p.root_name().string() + p.root_directory().string()));
        result.Set("dir", Lode::Value(p.parent_path().string()));
        result.Set("base", Lode::Value(name));
        result.Set("name", Lode::Value(base));
        result.Set("ext", Lode::Value(ext));
        return Lode::Value(result);
    });

    exports.SetValue("sep", Lode::Value(std::string(1, static_cast<char>(fs::path::preferred_separator))));

}

} // namespace lodefs::path
