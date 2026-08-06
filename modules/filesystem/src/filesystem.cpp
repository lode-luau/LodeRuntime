#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Task.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Logger.hpp"
#include "ModuleLoader.hpp"
#include <uv.h>
#include <filesystem>
#include <string>
#include <memory>
#include <vector>
#include <fstream>
#include <sstream>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstring>

namespace fs = std::filesystem;

// ============================================================================
// 1. PATH MODULE IMPLEMENTATION (Embedded)
// ============================================================================
static fs::path ResolveImpl(Lode::State& vm, const std::string& rawPathStr)
{
    if (rawPathStr.empty()) return fs::current_path();

    std::string requirerChunknameStr = Lode::GetCallerChunkName(vm.GetLuaState());
    fs::path callerPath;
    if (!requirerChunknameStr.empty() && (requirerChunknameStr[0] == '@' || requirerChunknameStr[0] == '=')) {
        callerPath = requirerChunknameStr.substr(1);
    } else {
        callerPath = requirerChunknameStr;
    }

    fs::path packagePath;
    fs::path currentPath = fs::current_path();

    if (!callerPath.empty()) {
        fs::path canonicalCaller = fs::weakly_canonical(callerPath);
        if (fs::is_regular_file(canonicalCaller)) {
            if (canonicalCaller.filename() == "init.luau" || canonicalCaller.filename() == "init.lua") {
                currentPath = canonicalCaller.parent_path().parent_path();
                packagePath = canonicalCaller.parent_path();
            } else {
                currentPath = canonicalCaller.parent_path();
                packagePath = canonicalCaller.parent_path();
            }
        } else {
            packagePath = Lode::FindLodeJson(callerPath);
            currentPath = callerPath.parent_path();
        }
    }

    std::string relPath = rawPathStr;
    if (!relPath.empty() && relPath[0] == '@') {
        size_t slashPos = relPath.find('/', 1);
        std::string aliasName = (slashPos != std::string::npos) ? relPath.substr(0, slashPos) : relPath;
        std::string remainder = (slashPos != std::string::npos) ? relPath.substr(slashPos + 1) : "";

        if (aliasName == "@self" || aliasName == "self") {
            currentPath = packagePath.empty() ? currentPath : packagePath;
        } else {
            fs::path p(aliasName);
            if (p.is_relative()) p = fs::current_path() / p;
            currentPath = fs::weakly_canonical(p);
        }

        if (!remainder.empty()) currentPath /= remainder;
    } else {
        currentPath /= relPath;
    }
    return fs::weakly_canonical(currentPath);
}

static Lode::Table CreatePathTable(Lode::State& vm)
{
    Lode::Table exports = vm.CreateTable();
    exports.Set("resolve", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        fs::path resolved = fs::current_path();
        if (args.Size() > 0) {
            resolved = ResolveImpl(vm, args[0].AsString());
            for (size_t i = 1; i < args.Size(); ++i) resolved /= args[i].AsString();
        }
        return Lode::Value(fs::weakly_canonical(resolved).string());
    }));

    exports.Set("join", vm.CreateFastFunction([](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() == 0) return Lode::Value("");
        fs::path p = args[0].AsString();
        for (size_t i = 1; i < args.Size(); ++i) p /= args[i].AsString();
        return Lode::Value(fs::weakly_canonical(p).string());
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
    return exports;
}

// ============================================================================
// 2. FILESYSTEM MODULE IMPLEMENTATION (LibUV based)
// ============================================================================

enum class FsOp {
    ReadFile, ReadToBuffer, WriteFile, AppendFile, Exists, Mkdir, ReadDir, Stat, Rm, CopyFile, Rename, Realpath
};

struct FsWorkState {
    std::mutex mutex;
    std::condition_variable condition;
    uv_work_t* request = nullptr;
    bool pending = true;
    bool shuttingDown = false;
};

struct FsWorkContext {
    uv_work_t req;
    lua_State* L = nullptr;
    Lode::Coroutine coroutine;
    Lode::Value callback;
    bool isYield = false;
    bool isBuffer = false;
    
    FsOp op;
    fs::path targetPath;
    fs::path destPath;
    Lode::Value userBuffer;
    void* userBufferPtr = nullptr;
    size_t userBufferSize = 0;
    size_t bufferOffset = 0;
    std::string writeData;
    bool recursive = false;
    
    bool success = false;
    std::string errorMsg;
    
    std::string readDataStr;
    std::vector<std::string> dirFiles;
    uint64_t statSize = 0;
    double statMtime = 0;
    double statAtime = 0;
    double statCtime = 0;
    bool statIsDirectory = false;
    bool statIsFile = false;
    std::shared_ptr<FsWorkState> lifecycle;
};

static void ShutdownWork(const std::shared_ptr<FsWorkState>& lifecycle, uv_loop_t* loop)
{
    if (!lifecycle) return;

    {
        std::lock_guard<std::mutex> lock(lifecycle->mutex);
        lifecycle->shuttingDown = true;
        if (lifecycle->request)
            uv_cancel(reinterpret_cast<uv_req_t*>(lifecycle->request));
    }

    while (true) {
        {
            std::unique_lock<std::mutex> lock(lifecycle->mutex);
            if (!lifecycle->pending)
                break;
        }

        if (loop)
            uv_run(loop, UV_RUN_NOWAIT);

        std::unique_lock<std::mutex> lock(lifecycle->mutex);
        lifecycle->condition.wait_for(lock, std::chrono::milliseconds(1));
    }
}

static bool SubmitWork(Lode::State& vm, FsWorkContext* ctx) {
    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();
    if (!loop) {
        delete ctx;
        return false;
    }
    ctx->req.data = ctx;
    ctx->lifecycle = std::make_shared<FsWorkState>();
    ctx->lifecycle->request = &ctx->req;
    auto lifecycle = ctx->lifecycle;

    int queueStatus = uv_queue_work(loop, &ctx->req, [](uv_work_t* req) {
        FsWorkContext* ctx = static_cast<FsWorkContext*>(req->data);
        try {
            if (ctx->op == FsOp::ReadFile) {
                std::ifstream file(ctx->targetPath, std::ios::binary);
                if (!file) {
                    ctx->success = false;
                    ctx->errorMsg = "ENOENT: no such file or directory, open '" + ctx->targetPath.string() + "'";
                } else {
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    ctx->readDataStr = buffer.str();
                    ctx->success = true;
                }
            } else if (ctx->op == FsOp::ReadToBuffer) {
                std::ifstream file(ctx->targetPath, std::ios::binary);
                if (!file) {
                    ctx->success = false;
                    ctx->errorMsg = "ENOENT: no such file or directory, open '" + ctx->targetPath.string() + "'";
                } else {
                    size_t bufSize = ctx->userBufferSize;
                    if (ctx->bufferOffset <= bufSize) {
                        ctx->readDataStr.resize(bufSize - ctx->bufferOffset);
                        file.read(ctx->readDataStr.data(), static_cast<std::streamsize>(ctx->readDataStr.size()));
                        ctx->statSize = static_cast<uint64_t>(file.gcount());
                        ctx->readDataStr.resize(static_cast<size_t>(ctx->statSize));
                        ctx->success = true;
                    } else {
                        ctx->success = false;
                        ctx->errorMsg = "Invalid buffer or offset out of bounds";
                    }
                }
            } else if (ctx->op == FsOp::WriteFile) {
                std::ofstream file(ctx->targetPath, std::ios::binary);
                if (!file) {
                    ctx->success = false;
                    ctx->errorMsg = "EACCES: permission denied, open '" + ctx->targetPath.string() + "'";
                } else {
                    file.write(ctx->writeData.data(), ctx->writeData.size());
                    ctx->success = true;
                }
            } else if (ctx->op == FsOp::AppendFile) {
                std::ofstream file(ctx->targetPath, std::ios::binary | std::ios::app);
                if (!file) {
                    ctx->success = false;
                    ctx->errorMsg = "EACCES: permission denied, open '" + ctx->targetPath.string() + "'";
                } else {
                    file.write(ctx->writeData.data(), ctx->writeData.size());
                    ctx->success = true;
                }
            } else if (ctx->op == FsOp::CopyFile) {
                std::error_code ec;
                fs::copy_file(ctx->targetPath, ctx->destPath, fs::copy_options::overwrite_existing, ec);
                if (ec) { ctx->success = false; ctx->errorMsg = ec.message(); }
                else { ctx->success = true; }
            } else if (ctx->op == FsOp::Rename) {
                std::error_code ec;
                fs::rename(ctx->targetPath, ctx->destPath, ec);
                if (ec) { ctx->success = false; ctx->errorMsg = ec.message(); }
                else { ctx->success = true; }
            } else if (ctx->op == FsOp::Realpath) {
                std::error_code ec;
                auto res = fs::canonical(ctx->targetPath, ec);
                if (ec) { ctx->success = false; ctx->errorMsg = ec.message(); }
                else { ctx->success = true; ctx->readDataStr = res.string(); }
            } else if (ctx->op == FsOp::Exists) {
                ctx->success = true;
                ctx->statIsFile = fs::exists(ctx->targetPath); 
            } else if (ctx->op == FsOp::Mkdir) {
                std::error_code ec;
                if (fs::create_directories(ctx->targetPath, ec)) {
                    ctx->success = true;
                } else {
                    if (!ec) ctx->success = true; // Already existed
                    else {
                        ctx->success = false;
                        ctx->errorMsg = ec.message();
                    }
                }
            } else if (ctx->op == FsOp::ReadDir) {
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(ctx->targetPath, ec)) {
                    ctx->dirFiles.push_back(entry.path().filename().string());
                }
                if (ec) {
                    ctx->success = false;
                    ctx->errorMsg = ec.message();
                } else {
                    ctx->success = true;
                }
            } else if (ctx->op == FsOp::Stat) {
                std::error_code ec;
                auto st = fs::status(ctx->targetPath, ec);
                if (ec) {
                    ctx->success = false;
                    ctx->errorMsg = ec.message();
                } else {
                    ctx->success = true;
                    ctx->statSize = fs::file_size(ctx->targetPath, ec);
                    ctx->statIsDirectory = fs::is_directory(st);
                    ctx->statIsFile = fs::is_regular_file(st);
                    
                    uv_fs_t statReq;
                    int r = uv_fs_stat(nullptr, &statReq, ctx->targetPath.string().c_str(), nullptr);
                    if (r == 0) {
                        ctx->statMtime = statReq.statbuf.st_mtim.tv_sec + statReq.statbuf.st_mtim.tv_nsec / 1e9;
                        ctx->statAtime = statReq.statbuf.st_atim.tv_sec + statReq.statbuf.st_atim.tv_nsec / 1e9;
                        ctx->statCtime = statReq.statbuf.st_ctim.tv_sec + statReq.statbuf.st_ctim.tv_nsec / 1e9;
                    }
                    uv_fs_req_cleanup(&statReq);
                }
            } else if (ctx->op == FsOp::Rm) {
                std::error_code ec;
                if (ctx->recursive) {
                    fs::remove_all(ctx->targetPath, ec);
                } else {
                    fs::remove(ctx->targetPath, ec);
                }
                if (ec) {
                    ctx->success = false;
                    ctx->errorMsg = ec.message();
                } else {
                    ctx->success = true;
                }
            }
        } catch (const std::exception& e) {
            ctx->success = false;
            ctx->errorMsg = e.what();
        }
    }, [](uv_work_t* req, int status) {
        FsWorkContext* ctx = static_cast<FsWorkContext*>(req->data);
        bool shuttingDown = false;
        if (ctx->lifecycle) {
            std::lock_guard<std::mutex> lock(ctx->lifecycle->mutex);
            shuttingDown = ctx->lifecycle->shuttingDown;
            ctx->lifecycle->request = nullptr;
            ctx->lifecycle->pending = false;
            ctx->lifecycle->condition.notify_all();
        }
        if (shuttingDown) {
            delete ctx;
            return;
        }
        if (status != 0) {
            ctx->success = false;
            ctx->errorMsg = uv_strerror(status);
        }
        lua_State* L = ctx->L;
        Lode::State vm(L);
        
        std::vector<Lode::Value> args;
        if (!ctx->isYield) {
            args.push_back(Lode::Value(ctx->success));
        }
        
        if (!ctx->success) {
            if (!ctx->isYield) args.push_back(Lode::Value(ctx->errorMsg));
        } else {
            if (ctx->op == FsOp::ReadFile) {
                if (ctx->isBuffer) {
                    Lode::Value bufVal = vm.CreateBuffer(ctx->readDataStr.size());
                    void* ptr = bufVal.AsBuffer(nullptr);
                    if (ptr) std::memcpy(ptr, ctx->readDataStr.data(), ctx->readDataStr.size());
                    args.push_back(bufVal);
                } else {
                    args.push_back(Lode::Value(ctx->readDataStr));
                }
            } else if (ctx->op == FsOp::ReadToBuffer) {
                size_t size = 0;
                void* ptr = ctx->userBuffer.AsBuffer(&size);
                size_t available = size > ctx->bufferOffset ? size - ctx->bufferOffset : 0;
                size_t copySize = static_cast<size_t>(ctx->statSize) < available ? static_cast<size_t>(ctx->statSize) : available;
                if (ptr && copySize > 0)
                    std::memcpy(static_cast<uint8_t*>(ptr) + ctx->bufferOffset, ctx->readDataStr.data(), copySize);
                args.push_back(Lode::Value(static_cast<double>(ctx->statSize)));
            } else if (ctx->op == FsOp::Realpath) {
                args.push_back(Lode::Value(ctx->readDataStr));
            } else if (ctx->op == FsOp::Exists) {
                args.push_back(Lode::Value(ctx->statIsFile)); // We stored bool in statIsFile
            } else if (ctx->op == FsOp::ReadDir) {
                Lode::Table t = vm.CreateTable();
                for (size_t i = 0; i < ctx->dirFiles.size(); ++i) {
                    t.Set(static_cast<int>(i + 1), Lode::Value(ctx->dirFiles[i]));
                }
                args.push_back(Lode::Value(t));
            } else if (ctx->op == FsOp::Stat) {
                Lode::Table t = vm.CreateTable();
                t.Set("size", Lode::Value(static_cast<double>(ctx->statSize)));
                t.Set("mtime", Lode::Value(ctx->statMtime));
                t.Set("atime", Lode::Value(ctx->statAtime));
                t.Set("ctime", Lode::Value(ctx->statCtime));
                t.Set("isDirectory", Lode::Value(ctx->statIsDirectory));
                t.Set("isFile", Lode::Value(ctx->statIsFile));
                args.push_back(Lode::Value(t));
            }
        }
        
        if (ctx->isYield) {
            if (!ctx->success) {
                auto result = ctx->coroutine.ResumeError(ctx->errorMsg);
                if (result.IsError())
                {
                    if (Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                        Lode::Task::SetMainThreadError(vm, result.GetError().ErrorMessage());
                    else
                        Lode::Logger::Error("Unhandled filesystem error: " + result.GetError().ErrorMessage());
                }
            } else {
                auto result = ctx->coroutine.Resume(args);
                if (result.IsError() && Lode::Task::IsMainThread(vm, ctx->coroutine.GetThreadState()))
                    Lode::Task::SetMainThreadError(vm, result.GetError().ErrorMessage());
            }
        } else if (!ctx->isYield && ctx->callback.IsFunction()) {
            auto result = ctx->callback.Call(vm, args);
            if (result.IsError())
                Lode::Logger::Error("Unhandled filesystem callback error: " + result.GetError().ErrorMessage());
        }
        
        delete ctx;
    });

    if (queueStatus != 0) {
        delete ctx;
        return false;
    }

    Lode::Task::RegisterShutdownHook(vm, [lifecycle, loop]() {
        ShutdownWork(lifecycle, loop);
    });
    return true;
}

// Wrapper for returning Luau functions that yield or take callbacks
static Lode::Value CreateAsyncMethod(Lode::State& vmOuter, FsOp op, bool isYield, bool isBuffer = false) {
    lua_State* mainL = vmOuter.GetMainThread();
    return vmOuter.CreateFastFunction([op, isYield, isBuffer, mainL](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (args.Size() == 0 || !args[0].IsString()) {
            if (isYield) vm.RaiseError("Expected string path as first argument");
            return Lode::Value();
        }

        FsWorkContext* ctx = new FsWorkContext();
        ctx->L = mainL;
        ctx->op = op;
        ctx->isYield = isYield;
        ctx->isBuffer = isBuffer;
        ctx->targetPath = args[0].AsString();
        
        if (op == FsOp::WriteFile || op == FsOp::AppendFile) {
            if (args.Size() > 1) {
                if (args[1].IsString()) {
                    ctx->writeData = args[1].AsString();
                } else if (args[1].IsBuffer()) {
                    size_t sz = 0;
                    void* ptr = args[1].AsBuffer(&sz);
                    if (ptr) ctx->writeData.assign(static_cast<const char*>(ptr), sz);
                }
            }
        } else if (op == FsOp::CopyFile || op == FsOp::Rename) {
            if (args.Size() > 1 && args[1].IsString()) {
                ctx->destPath = args[1].AsString();
            } else {
                if (isYield) { delete ctx; vm.RaiseError("Expected string destination path as second argument"); return Lode::Value(); }
            }
        } else if (op == FsOp::ReadToBuffer) {
            if (args.Size() > 1 && args[1].IsBuffer()) {
                ctx->userBuffer = args[1].ToValue();
                ctx->userBufferPtr = ctx->userBuffer.AsBuffer(&ctx->userBufferSize);
                int nextArgIdx = 2;
                if (!isYield && args.Size() > 2 && args[2].IsFunction()) nextArgIdx = 3; 
                // offset is after callback if callback is present, or after buffer if yield
                if (args.Size() > nextArgIdx && args[nextArgIdx].IsNumber()) {
                    auto offsetResult = Lode::Numeric::ToSize(args[nextArgIdx].AsNumber(), "buffer offset");
                    if (offsetResult.IsError())
                    {
                        delete ctx;
                        vm.RaiseError(offsetResult.GetError().ErrorMessage());
                        return Lode::Value();
                    }
                    ctx->bufferOffset = offsetResult.GetValue();
                }
            } else {
                if (isYield) { delete ctx; vm.RaiseError("Expected buffer as second argument"); return Lode::Value(); }
            }
        } else if (op == FsOp::Rm) {
            int recIdx = isYield ? 1 : 2;
            if (args.Size() > recIdx && args[recIdx].IsBoolean()) {
                ctx->recursive = args[recIdx].AsBoolean();
            } else if (args.Size() > recIdx && !isYield && args[1].IsBoolean()) {
                // If cb is at 2, recursive might be at 1
                ctx->recursive = args[1].AsBoolean();
            }
        }

        if (!isYield) {
            // Find callback: user requested callback BEFORE optionals.
            // Optionals for ReadToBuffer is at idx 3. Callback at idx 2.
            // Optionals for Rm is at idx 2. Callback at idx 1.
            int expectedCbIdx = 1;
            if (op == FsOp::WriteFile || op == FsOp::AppendFile || op == FsOp::CopyFile || op == FsOp::Rename || op == FsOp::ReadToBuffer) {
                expectedCbIdx = 2;
            }
            if (args.Size() > expectedCbIdx && args[expectedCbIdx].IsFunction()) {
                ctx->callback = args[expectedCbIdx].ToValue();
            } else if (args.Size() > 1 && args[args.Size() - 1].IsFunction()) {
                ctx->callback = args[args.Size() - 1].ToValue();
            }
        }

        if (isYield) {
            ctx->coroutine = Lode::Coroutine(vm.GetLuaState());
            if (!SubmitWork(vm, ctx)) {
                vm.RaiseError("Failed to queue filesystem operation");
                return Lode::Value();
            }
            vm.YieldThread();
            return Lode::Value();
        } else {
            if (!SubmitWork(vm, ctx)) {
                return Lode::Value();
            }
            return Lode::Value();
        }
    });
}

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();

    exports.Set("path", Lode::Value(CreatePathTable(vm)));

    exports.Set("readFile", CreateAsyncMethod(vm, FsOp::ReadFile, true, false));
    exports.Set("readFileBuffer", CreateAsyncMethod(vm, FsOp::ReadFile, true, true));
    exports.Set("readToBuffer", CreateAsyncMethod(vm, FsOp::ReadToBuffer, true));
    exports.Set("writeFile", CreateAsyncMethod(vm, FsOp::WriteFile, true));
    exports.Set("appendFile", CreateAsyncMethod(vm, FsOp::AppendFile, true));
    exports.Set("copyFile", CreateAsyncMethod(vm, FsOp::CopyFile, true));
    exports.Set("rename", CreateAsyncMethod(vm, FsOp::Rename, true));
    exports.Set("exists", CreateAsyncMethod(vm, FsOp::Exists, true));
    exports.Set("mkdir", CreateAsyncMethod(vm, FsOp::Mkdir, true));
    exports.Set("readDir", CreateAsyncMethod(vm, FsOp::ReadDir, true));
    exports.Set("stat", CreateAsyncMethod(vm, FsOp::Stat, true));
    exports.Set("realpath", CreateAsyncMethod(vm, FsOp::Realpath, true));
    exports.Set("rm", CreateAsyncMethod(vm, FsOp::Rm, true));

    // Callbacks API
    exports.Set("readFileWithCallback", CreateAsyncMethod(vm, FsOp::ReadFile, false, false));
    exports.Set("readFileBufferWithCallback", CreateAsyncMethod(vm, FsOp::ReadFile, false, true));
    exports.Set("readToBufferWithCallback", CreateAsyncMethod(vm, FsOp::ReadToBuffer, false));
    exports.Set("writeFileWithCallback", CreateAsyncMethod(vm, FsOp::WriteFile, false));
    exports.Set("appendFileWithCallback", CreateAsyncMethod(vm, FsOp::AppendFile, false));
    exports.Set("copyFileWithCallback", CreateAsyncMethod(vm, FsOp::CopyFile, false));
    exports.Set("renameWithCallback", CreateAsyncMethod(vm, FsOp::Rename, false));
    exports.Set("existsWithCallback", CreateAsyncMethod(vm, FsOp::Exists, false));
    exports.Set("mkdirWithCallback", CreateAsyncMethod(vm, FsOp::Mkdir, false));
    exports.Set("readDirWithCallback", CreateAsyncMethod(vm, FsOp::ReadDir, false));
    exports.Set("statWithCallback", CreateAsyncMethod(vm, FsOp::Stat, false));
    exports.Set("realpathWithCallback", CreateAsyncMethod(vm, FsOp::Realpath, false));
    exports.Set("rmWithCallback", CreateAsyncMethod(vm, FsOp::Rm, false));

    return { Lode::Value(exports) };
}
