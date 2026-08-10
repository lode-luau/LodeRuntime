// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#define NOMINMAX
#include "FileSystem/FsStatic.hpp"
#include "FileSystem/FsHelpers.hpp"
#include "Lode/Task.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Logger.hpp"
#include "Lode/Numeric.hpp"
#include <uv.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <cstring>

namespace fs = std::filesystem;

namespace lodefs {

// ============================================================================
// FS WORK CONTEXT & WORKER THREAD
// ============================================================================
enum class FsOp {
    ReadFile, WriteFile, AppendFile, CopyFile, Rename,
    Exists, Mkdir, ReadDir, Stat, Realpath, Rm, ReadToBuffer
};

struct FsWorkContext {
    uv_work_t req;
    lua_State* L;
    FsOp op;
    bool isYield;
    Lode::Value callback;
    Lode::Coroutine coroutine;
    
    std::string targetPath;
    std::string destPath;     // For copy/rename
    std::string writeData;    // For write/append
    bool isBuffer = false;    // For readFile returning buffer
    
    bool recursive = false;   // For Rm
    
    // For readToBuffer
    Lode::Value userBuffer;
    void* userBufferPtr = nullptr;
    size_t userBufferSize = 0;
    size_t bufferOffset = 0;
    
    // Results
    bool success = false;
    std::string errorMsg;
    std::string readDataStr;
    std::vector<std::string> dirFiles;
    
    // Stat results
    double statSize = 0;
    double statMtime = 0;
    double statAtime = 0;
    double statCtime = 0;
    bool statIsDirectory = false;
    bool statIsFile = false;
};

static void LodeuvFsWork(uv_work_t* req) {
    FsWorkContext* ctx = static_cast<FsWorkContext*>(req->data);
    try {
        fs::path p(ctx->targetPath);
        
        switch (ctx->op) {
            case FsOp::ReadFile:
            case FsOp::ReadToBuffer: {
                if (!fs::exists(p)) throw std::runtime_error("File not found: " + ctx->targetPath);
                if (!fs::is_regular_file(p)) throw std::runtime_error("Not a regular file: " + ctx->targetPath);
                
                std::ifstream file(p, std::ios::binary | std::ios::ate);
                if (!file) throw std::runtime_error("Failed to open file for reading: " + ctx->targetPath);
                
                std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);
                
                if (ctx->op == FsOp::ReadToBuffer) {
                    if (ctx->bufferOffset >= ctx->userBufferSize) {
                        ctx->statSize = 0; // Bytes read
                        ctx->success = true;
                        break;
                    }
                    size_t availableSpace = ctx->userBufferSize - ctx->bufferOffset;
                    size_t toRead = static_cast<size_t>(size) < availableSpace ? static_cast<size_t>(size) : availableSpace;
                    
                    if (toRead > 0) {
                        file.read(static_cast<char*>(ctx->userBufferPtr) + ctx->bufferOffset, toRead);
                        ctx->statSize = file.gcount();
                    } else {
                        ctx->statSize = 0;
                    }
                } else {
                    ctx->readDataStr.resize(size);
                    if (file.read(ctx->readDataStr.data(), size)) {
                        ctx->success = true;
                    } else {
                        throw std::runtime_error("Error reading file contents: " + ctx->targetPath);
                    }
                }
                ctx->success = true;
                break;
            }
            case FsOp::WriteFile:
            case FsOp::AppendFile: {
                auto mode = (ctx->op == FsOp::AppendFile) ? (std::ios::binary | std::ios::app) : (std::ios::binary | std::ios::trunc);
                std::ofstream file(p, mode);
                if (!file) throw std::runtime_error("Failed to open file for writing: " + ctx->targetPath);
                file.write(ctx->writeData.data(), ctx->writeData.size());
                if (!file) throw std::runtime_error("Error writing to file: " + ctx->targetPath);
                ctx->success = true;
                break;
            }
            case FsOp::CopyFile: {
                fs::copy(p, fs::path(ctx->destPath), fs::copy_options::overwrite_existing);
                ctx->success = true;
                break;
            }
            case FsOp::Rename: {
                fs::rename(p, fs::path(ctx->destPath));
                ctx->success = true;
                break;
            }
            case FsOp::Exists: {
                ctx->statIsFile = fs::exists(p); // We'll return this bool
                ctx->success = true;
                break;
            }
            case FsOp::Mkdir: {
                fs::create_directories(p);
                ctx->success = true;
                break;
            }
            case FsOp::ReadDir: {
                if (!fs::exists(p) || !fs::is_directory(p)) throw std::runtime_error("Not a directory: " + ctx->targetPath);
                for (const auto& entry : fs::directory_iterator(p)) {
                    ctx->dirFiles.push_back(entry.path().filename().generic_string());
                }
                ctx->success = true;
                break;
            }
            case FsOp::Stat: {
                if (!fs::exists(p)) throw std::runtime_error("Path does not exist: " + ctx->targetPath);
                ctx->statIsDirectory = fs::is_directory(p);
                ctx->statIsFile = fs::is_regular_file(p);
                if (ctx->statIsFile) ctx->statSize = static_cast<double>(fs::file_size(p));
                
                auto ftime = fs::last_write_time(p);
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now()
                );
                ctx->statMtime = static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                ctx->statAtime = ctx->statMtime;
                ctx->statCtime = ctx->statMtime;
                
                ctx->success = true;
                break;
            }
            case FsOp::Realpath: {
                ctx->readDataStr = fs::absolute(p).generic_string();
                ctx->success = true;
                break;
            }
            case FsOp::Rm: {
                if (ctx->recursive) {
                    fs::remove_all(p);
                } else {
                    fs::remove(p);
                }
                ctx->success = true;
                break;
            }
        }
    } catch (const fs::filesystem_error& e) {
        ctx->success = false;
        ctx->errorMsg = e.what();
    } catch (const std::exception& e) {
        ctx->success = false;
        ctx->errorMsg = e.what();
    }
}

static bool SubmitWork(Lode::State& vmOuter, FsWorkContext* ctx) {
    auto loop = vmOuter.GetEventLoop().GetUVLoop();
    ctx->req.data = ctx;
    

    
    int queueStatus = uv_queue_work(loop, &ctx->req, LodeuvFsWork, [](uv_work_t* req, int status) {
        FsWorkContext* ctx = static_cast<FsWorkContext*>(req->data);
        Lode::State vm(ctx->L);

        std::vector<Lode::Value> args;
        
        if (ctx->success) {
            if (ctx->op == FsOp::ReadToBuffer) {
                args.push_back(Lode::Value(static_cast<double>(ctx->statSize))); // Bytes read
            } else if (ctx->op == FsOp::ReadFile) {
                if (ctx->isBuffer) {
                    Lode::Value buf = vm.CreateBuffer(ctx->readDataStr.size());
                    void* ptr = buf.AsBuffer(nullptr);
                    if (ptr) std::memcpy(ptr, ctx->readDataStr.data(), ctx->readDataStr.size());
                    args.push_back(buf);
                } else {
                    args.push_back(Lode::Value(ctx->readDataStr));
                }
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
        }
        
        delete ctx;
    });

    if (queueStatus != 0) {
        delete ctx;
        return false;
    }
    return true;
}

static Lode::Value CreateAsyncMethod(Lode::State& vmOuter, FsOp op, bool isBuffer = false) {
    lua_State* mainL = vmOuter.GetMainThread();
    return vmOuter.CreateFunction([op, isBuffer, mainL](Lode::State& vm, const std::vector<Lode::Value>& args) -> Lode::Value {
        if (args.empty() || !args[0].IsString()) {
            vm.RaiseError("fs: expected string path as first argument");
            return Lode::Value();
        }

        FsWorkContext* ctx = new FsWorkContext();
        ctx->L = mainL;
        ctx->op = op;
        ctx->isYield = true;
        ctx->isBuffer = isBuffer;
        ctx->targetPath = args[0].AsString();
        
        if (op == FsOp::WriteFile || op == FsOp::AppendFile) {
            if (args.size() > 1) {
                if (args[1].IsString()) {
                    ctx->writeData = args[1].AsString();
                } else if (args[1].IsBuffer()) {
                    size_t sz = 0;
                    void* ptr = args[1].AsBuffer(&sz);
                    if (ptr) ctx->writeData.assign(static_cast<const char*>(ptr), sz);
                }
            }
        } else if (op == FsOp::CopyFile || op == FsOp::Rename) {
            if (args.size() > 1 && args[1].IsString()) {
                ctx->destPath = args[1].AsString();
            } else {
                delete ctx; vm.RaiseError("fs: expected string destination path as second argument"); return Lode::Value();
            }
        } else if (op == FsOp::ReadToBuffer) {
            if (args.size() > 1 && args[1].IsBuffer()) {
                ctx->userBuffer = args[1];
                ctx->userBufferPtr = ctx->userBuffer.AsBuffer(&ctx->userBufferSize);
                if (args.size() > 2 && args[2].IsNumber()) {
                    auto offsetResult = Lode::Numeric::ToSize(args[2].AsNumber(), "buffer offset");
                    if (offsetResult.IsError())
                    {
                        delete ctx;
                        vm.RaiseError(offsetResult.GetError().ErrorMessage());
                        return Lode::Value();
                    }
                    ctx->bufferOffset = offsetResult.GetValue();
                }
            } else {
                delete ctx; vm.RaiseError("fs: expected buffer as second argument"); return Lode::Value();
            }
        } else if (op == FsOp::Rm) {
            int recIdx = 1;
            if (args.size() > (size_t)recIdx && args[recIdx].IsBoolean()) {
                ctx->recursive = args[recIdx].AsBoolean();
            }
        }

        ctx->coroutine = Lode::Coroutine(vm.GetLuaState());
        if (!SubmitWork(vm, ctx)) {
            vm.RaiseError("fs: failed to queue filesystem operation");
            return Lode::Value();
        }
        vm.YieldThread();
        return Lode::Value();
    });
}

void BindStaticMethods(Lode::State& vm, Lode::Exports& exports)
{
    exports.SetValue("ReadFile", CreateAsyncMethod(vm, FsOp::ReadFile, false));
    exports.SetValue("ReadFileBuffer", CreateAsyncMethod(vm, FsOp::ReadFile, true));
    exports.SetValue("ReadToBuffer", CreateAsyncMethod(vm, FsOp::ReadToBuffer, false));
    exports.SetValue("WriteFile", CreateAsyncMethod(vm, FsOp::WriteFile, false));
    exports.SetValue("AppendFile", CreateAsyncMethod(vm, FsOp::AppendFile, false));
    exports.SetValue("CopyFile", CreateAsyncMethod(vm, FsOp::CopyFile, false));
    exports.SetValue("Rename", CreateAsyncMethod(vm, FsOp::Rename, false));
    exports.SetValue("Exists", CreateAsyncMethod(vm, FsOp::Exists, false));
    exports.SetValue("CreateDirectory", CreateAsyncMethod(vm, FsOp::Mkdir, false));
    exports.SetValue("ReadDirectory", CreateAsyncMethod(vm, FsOp::ReadDir, false));
    exports.SetValue("Stat", CreateAsyncMethod(vm, FsOp::Stat, false));
    exports.SetValue("RealPath", CreateAsyncMethod(vm, FsOp::Realpath, false));
    exports.SetValue("RemoveFile", CreateAsyncMethod(vm, FsOp::Rm, false));
    exports.SetValue("RemoveDirectory", CreateAsyncMethod(vm, FsOp::Rm, false));
}

} // namespace lodefs
