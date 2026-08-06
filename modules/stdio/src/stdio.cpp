#ifdef _WIN32
#define NOMINMAX
#endif

#include <uv.h>

#ifdef _WIN32
#undef GetMessage
#endif

#include "Lode/Module.hpp"
#include "Lode/State.hpp"
#include "Lode/Table.hpp"
#include "Lode/Value.hpp"
#include "Lode/ClassBuilder.hpp"
#include "Lode/EventLoop.hpp"
#include "Lode/Task.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/Numeric.hpp"
#include "Lode/Compiler.hpp"
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace {
    void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
        buf->base = new char[suggested_size];
        buf->len = suggested_size;
    }
}

class WritableStream {
public:
    uv_stream_t* stream = nullptr;
    uv_loop_t* loop = nullptr;
    uv_file file = -1;
    bool fileMode = false;

    WritableStream() = default;
    WritableStream(uv_loop_t* l, uv_stream_t* s) : stream(s), loop(l) {}
    WritableStream(uv_loop_t* l, uv_file f) : loop(l), file(f), fileMode(true) {}

    void Shutdown() {
        if (!stream) return;
        uv_stream_t* closing = stream;
        stream = nullptr;
        uv_close(reinterpret_cast<uv_handle_t*>(closing), [](uv_handle_t* handle) {
            if (handle->type == UV_TTY)
                delete reinterpret_cast<uv_tty_t*>(handle);
            else
                delete reinterpret_cast<uv_pipe_t*>(handle);
        });
    }

    void writeData(const char* data, size_t len) {
        if (len == 0) return;

        if (fileMode) {
            struct FileWriteReq {
                uv_fs_t req;
                uv_buf_t buf;
            };

            auto* wr = new FileWriteReq;
            wr->buf.base = new char[len];
            wr->buf.len = len;
            std::memcpy(wr->buf.base, data, len);
            int result = uv_fs_write(loop, &wr->req, file, &wr->buf, 1, -1, [](uv_fs_t* req) {
                auto* wr = reinterpret_cast<FileWriteReq*>(req);
                delete[] wr->buf.base;
                uv_fs_req_cleanup(req);
                delete wr;
            });
            if (result < 0) {
                delete[] wr->buf.base;
                delete wr;
            }
            return;
        }

        if (!stream) return;
        
        struct WriteReq {
            uv_write_t req;
            uv_buf_t buf;
        };
        
        WriteReq* wr = new WriteReq;
        wr->buf.base = new char[len];
        wr->buf.len = len;
        std::memcpy(wr->buf.base, data, len);
        
        uv_write(&wr->req, stream, &wr->buf, 1, [](uv_write_t* req, int status) {
            WriteReq* wr = reinterpret_cast<WriteReq*>(req);
            delete[] wr->buf.base;
            delete wr;
        });
    }

    bool IsAvailable() const { return fileMode || stream != nullptr; }

    Lode::Value write(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!IsAvailable()) { vm.RaiseError("stdio writable stream is unavailable"); return Lode::Value(); }
        if (args.empty()) return Lode::Value();
        
        if (args[0].IsString()) {
            std::string s = args[0].AsString();
            writeData(s.data(), s.size());
        } else if (args[0].IsBuffer()) {
            size_t size = 0;
            void* ptr = args[0].AsBuffer(&size);
            if (ptr) writeData(static_cast<const char*>(ptr), size);
        }
        return Lode::Value();
    }

    Lode::Value writeLine(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!IsAvailable()) { vm.RaiseError("stdio writable stream is unavailable"); return Lode::Value(); }
        if (args.empty()) return Lode::Value();
        
        if (args[0].IsString()) {
            std::string s = args[0].AsString();
            s += "\n";
            writeData(s.data(), s.size());
        } else if (args[0].IsBuffer()) {
            size_t size = 0;
            void* ptr = args[0].AsBuffer(&size);
            if (ptr) {
                std::string s(static_cast<const char*>(ptr), size);
                s += "\n";
                writeData(s.data(), s.size());
            }
        }
        return Lode::Value();
    }

    Lode::Value isTTY(Lode::State& vm, const std::vector<Lode::Value>& args) const {
        return Lode::Value(stream && stream->type == UV_TTY);
    }

    Lode::Value getWindowSize(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream || stream->type != UV_TTY) {
            Lode::Table t = vm.CreateTable();
            t.Set(1, Lode::Value(0));
            t.Set(2, Lode::Value(0));
            return Lode::Value(t);
        }
        
        int width = 0, height = 0;
        uv_tty_get_winsize(reinterpret_cast<uv_tty_t*>(stream), &width, &height);
        
        Lode::Table t = vm.CreateTable();
        t.Set(1, Lode::Value(width));
        t.Set(2, Lode::Value(height));
        return Lode::Value(t);
    }
};

class ReadableStream {
public:
    uv_stream_t* stream = nullptr;
    uv_loop_t* loop = nullptr;
    uv_file file = -1;
    bool fileMode = false;
    int64_t fileOffset = 0;
    std::vector<uint8_t> buffer;
    bool reading = false;
    lua_State* mainL = nullptr;
    bool shuttingDown = false;

    struct PendingRead {
        bool isLine = false;
        bool isBuffer = false;
        bool isInto = false;
        size_t requestedBytes = 0;
        
        bool isCallback = false;
        Lode::Value callback;
        
        bool isYield = false;
        Lode::Coroutine coroutine;
        
        Lode::Value targetBufferValue;
        size_t offset = 0;
    };
    
    std::deque<PendingRead> pendingQueue;

    ReadableStream() = default;
    ReadableStream(uv_loop_t* l, uv_stream_t* s) : stream(s), loop(l) {}
    ReadableStream(uv_loop_t* l, uv_file f) : loop(l), file(f), fileMode(true) {}

    void Shutdown() {
        shuttingDown = true;
        mainL = nullptr;
        pendingQueue.clear();
        buffer.clear();
        if (stream) {
            uv_stream_t* closing = stream;
            stream = nullptr;
            if (reading) {
                uv_read_stop(closing);
                reading = false;
            }
            uv_close(reinterpret_cast<uv_handle_t*>(closing), [](uv_handle_t* handle) {
                if (handle->type == UV_TTY)
                    delete reinterpret_cast<uv_tty_t*>(handle);
                else
                    delete reinterpret_cast<uv_pipe_t*>(handle);
            });
        }
    }

    void startFileRead() {
        struct FileReadReq {
            uv_fs_t req;
            uv_buf_t buf;
            ReadableStream* owner;
        };

        auto* rr = new FileReadReq;
        rr->owner = this;
        rr->buf.base = new char[4096];
        rr->buf.len = 4096;
        reading = true;
        int result = uv_fs_read(loop, &rr->req, file, &rr->buf, 1, fileOffset, [](uv_fs_t* req) {
            auto* rr = reinterpret_cast<FileReadReq*>(req);
            ReadableStream* self = rr->owner;
            ssize_t nread = req->result;
            if (nread > 0) {
                self->fileOffset += nread;
                self->buffer.insert(self->buffer.end(), rr->buf.base, rr->buf.base + nread);
            }
            self->reading = false;
            uv_fs_req_cleanup(req);
            delete[] rr->buf.base;
            delete rr;

            if (nread > 0)
                self->processQueue();
            else
                self->processQueue(true);

            if (!self->shuttingDown && !self->pendingQueue.empty() && nread > 0)
                self->startFileRead();
        });
        if (result < 0) {
            reading = false;
            delete[] rr->buf.base;
            delete rr;
            processQueue(true);
        }
    }

    void startReadIfNeeded() {
        if (shuttingDown || reading) return;
        if (fileMode) {
            startFileRead();
            return;
        }
        if (!stream) return;
        reading = true;
        stream->data = this;
        uv_read_start(stream, alloc_buffer, [](uv_stream_t* s, ssize_t nread, const uv_buf_t* buf) {
            ReadableStream* self = static_cast<ReadableStream*>(s->data);
            if (nread > 0) {
                self->buffer.insert(self->buffer.end(), buf->base, buf->base + nread);
                self->processQueue();
            } else if (nread < 0) {
                uv_read_stop(s);
                self->reading = false;
                self->processQueue(true); // flush with EOF
            }
            if (buf->base) delete[] buf->base;
        });
    }

    bool tryResolve(Lode::State& vm, PendingRead& req, Lode::Value& resultValue, bool eof = false) {
        bool satisfied = false;
        size_t bytesToConsume = 0;
        
        if (req.isLine) {
            auto it = std::find(buffer.begin(), buffer.end(), '\n');
            if (it != buffer.end()) {
                bytesToConsume = std::distance(buffer.begin(), it) + 1;
                std::string line(buffer.begin(), buffer.begin() + bytesToConsume - 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                resultValue = Lode::Value(line);
                satisfied = true;
            } else if (eof && !buffer.empty()) {
                bytesToConsume = buffer.size();
                std::string line(buffer.begin(), buffer.end());
                resultValue = Lode::Value(line);
                satisfied = true;
            } else if (eof) {
                satisfied = true;
            }
        } else {
            size_t targetSize = req.requestedBytes > 0 ? req.requestedBytes : (buffer.empty() ? 0 : buffer.size());
            if (targetSize > 0 && buffer.size() >= targetSize) {
                bytesToConsume = targetSize;
                satisfied = true;
            } else if (req.requestedBytes == 0 && !buffer.empty()) {
                bytesToConsume = buffer.size();
                satisfied = true;
            } else if (eof) {
                bytesToConsume = buffer.size();
                satisfied = true;
            }

            if (satisfied && bytesToConsume > 0) {
                if (req.isInto) {
                    size_t bSize = 0;
                    void* ptr = req.targetBufferValue.AsBuffer(&bSize);
                    if (ptr && req.offset < bSize) {
                        size_t copyLen = (std::min)(bytesToConsume, bSize - req.offset);
                        std::memcpy(static_cast<uint8_t*>(ptr) + req.offset, buffer.data(), copyLen);
                        bytesToConsume = copyLen;
                        resultValue = Lode::Value(static_cast<double>(copyLen));
                    } else {
                        resultValue = Lode::Value(0.0);
                    }
                } else if (req.isBuffer) {
                    Lode::Value bufVal = vm.CreateBuffer(bytesToConsume);
                    size_t bSize = 0;
                    void* ptr = bufVal.AsBuffer(&bSize);
                    if (ptr) std::memcpy(ptr, buffer.data(), bytesToConsume);
                    resultValue = bufVal;
                } else {
                    std::string data(buffer.begin(), buffer.begin() + bytesToConsume);
                    resultValue = Lode::Value(data);
                }
            } else if (satisfied && bytesToConsume == 0) {
                if (req.isInto) resultValue = Lode::Value(0.0);
                else if (req.isBuffer) resultValue = vm.CreateBuffer(0);
                else resultValue = Lode::Value(std::string(""));
            }
        }
        
        if (satisfied && bytesToConsume > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + bytesToConsume);
        }
        return satisfied;
    }

    void processQueue(bool eof = false) {
        if (shuttingDown || !mainL) return;
        Lode::State vm(mainL);
        
        while (!pendingQueue.empty()) {
            auto& req = pendingQueue.front();
            Lode::Value resultValue;
            
            if (tryResolve(vm, req, resultValue, eof)) {
                auto currentReq = req;
                pendingQueue.pop_front();
                
                if (currentReq.isYield && currentReq.coroutine.IsValid()) {
                    std::vector<Lode::Value> resArgs;
                    if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) {
                        resArgs.push_back(resultValue);
                    }
                    auto result = currentReq.coroutine.Resume(resArgs);
                    if (result.IsError() && Lode::Task::IsMainThread(vm, currentReq.coroutine.GetThreadState()))
                        Lode::Task::SetMainThreadError(vm, result.GetError().ErrorMessage());
                } else if (currentReq.isCallback && currentReq.callback.IsFunction()) {
                    std::vector<Lode::Value> cbArgs;
                    if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) {
                        cbArgs.push_back(resultValue);
                    }
                    Lode::Task::Spawn(vm, currentReq.callback, cbArgs);
                }
            } else {
                break; // Needs more data from libuv
            }
        }
        
        if (pendingQueue.empty() && reading && !fileMode) {
            uv_read_stop(stream);
            reading = false;
        }
    }

    void queueRequest(PendingRead req) {
        pendingQueue.push_back(req);
        startReadIfNeeded();
    }

    Lode::Value read(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream && !fileMode) { vm.RaiseError("stdio readable stream is unavailable"); return Lode::Value(); }
        mainL = vm.GetMainThread();
        PendingRead req;
        if (args.size() > 0 && args[0].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[0].AsNumber(), "read length");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            req.requestedBytes = result.GetValue();
        }
        
        Lode::Value resultValue;
        if (tryResolve(vm, req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }
    
    Lode::Value readBuffer(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream && !fileMode) { vm.RaiseError("stdio readable stream is unavailable"); return Lode::Value(); }
        mainL = vm.GetMainThread();
        PendingRead req;
        req.isBuffer = true;
        if (args.size() > 0 && args[0].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[0].AsNumber(), "read length");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            req.requestedBytes = result.GetValue();
        }
        
        Lode::Value resultValue;
        if (tryResolve(vm, req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }
    
    Lode::Value readLine(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream && !fileMode) { vm.RaiseError("stdio readable stream is unavailable"); return Lode::Value(); }
        mainL = vm.GetMainThread();
        PendingRead req;
        req.isLine = true;
        
        Lode::Value resultValue;
        if (tryResolve(vm, req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }

    Lode::Value readInto(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream && !fileMode) { vm.RaiseError("stdio readable stream is unavailable"); return Lode::Value(); }
        mainL = vm.GetMainThread();
        if (args.empty() || !args[0].IsBuffer()) return Lode::Value(0.0);
        
        PendingRead req;
        req.isInto = true;
        req.targetBufferValue = args[0];
        if (args.size() > 1 && args[1].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[1].AsNumber(), "buffer offset");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            req.offset = result.GetValue();
        }
        size_t bSize = 0;
        (void)args[0].AsBuffer(&bSize);
        size_t maxAvailable = (bSize > req.offset) ? (bSize - req.offset) : 0;
        size_t length = maxAvailable;
        if (args.size() > 2 && args[2].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[2].AsNumber(), "read length");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            length = result.GetValue();
        }
        req.requestedBytes = (std::min)(length, maxAvailable);
        
        Lode::Value resultValue;
        if (tryResolve(vm, req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }

    Lode::Value readAsync(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream && !fileMode) { vm.RaiseError("stdio readable stream is unavailable"); return Lode::Value(); }
        mainL = vm.GetMainThread();
        if (args.empty() || !args[0].IsFunction()) return Lode::Value();
        
        PendingRead req;
        req.isCallback = true;
        req.callback = args[0];
        if (args.size() > 1 && args[1].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[1].AsNumber(), "read length");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            req.requestedBytes = result.GetValue();
        }
        
        Lode::Value resultValue;
        if (tryResolve(vm, req, resultValue)) {
            std::vector<Lode::Value> cbArgs;
            if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) cbArgs.push_back(resultValue);
            Lode::Task::Spawn(vm, req.callback, cbArgs);
            return Lode::Value();
        }
        
        queueRequest(req);
        return Lode::Value();
    }

    Lode::Value readBufferAsync(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream && !fileMode) { vm.RaiseError("stdio readable stream is unavailable"); return Lode::Value(); }
        mainL = vm.GetMainThread();
        if (args.empty() || !args[0].IsFunction()) return Lode::Value();
        
        PendingRead req;
        req.isCallback = true;
        req.callback = args[0];
        req.isBuffer = true;
        if (args.size() > 1 && args[1].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[1].AsNumber(), "read length");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            req.requestedBytes = result.GetValue();
        }
        
        Lode::Value resultValue;
        if (tryResolve(vm, req, resultValue)) {
            std::vector<Lode::Value> cbArgs;
            if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) cbArgs.push_back(resultValue);
            Lode::Task::Spawn(vm, req.callback, cbArgs);
            return Lode::Value();
        }
        
        queueRequest(req);
        return Lode::Value();
    }

    Lode::Value readIntoAsync(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream && !fileMode) { vm.RaiseError("stdio readable stream is unavailable"); return Lode::Value(); }
        mainL = vm.GetMainThread();
        if (args.size() < 2 || !args[0].IsBuffer() || !args[1].IsFunction()) return Lode::Value();
        
        PendingRead req;
        req.isCallback = true;
        req.targetBufferValue = args[0];
        req.callback = args[1];
        req.isInto = true;
        if (args.size() > 2 && args[2].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[2].AsNumber(), "buffer offset");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            req.offset = result.GetValue();
        }
        size_t bSize = 0;
        (void)args[0].AsBuffer(&bSize);
        size_t maxAvailable = (bSize > req.offset) ? (bSize - req.offset) : 0;
        size_t length = maxAvailable;
        if (args.size() > 3 && args[3].IsNumber()) {
            auto result = Lode::Numeric::ToSize(args[3].AsNumber(), "read length");
            if (result.IsError()) { vm.RaiseError(result.GetError().ErrorMessage()); return Lode::Value(); }
            length = result.GetValue();
        }
        req.requestedBytes = (std::min)(length, maxAvailable);
        
        Lode::Value resultValue;
        if (tryResolve(vm, req, resultValue)) {
            std::vector<Lode::Value> cbArgs;
            if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) cbArgs.push_back(resultValue);
            Lode::Task::Spawn(vm, req.callback, cbArgs);
            return Lode::Value();
        }
        
        queueRequest(req);
        return Lode::Value();
    }

    Lode::Value isTTY(Lode::State& vm, const std::vector<Lode::Value>& args) const {
        return Lode::Value(stream && stream->type == UV_TTY);
    }

    Lode::Value setRawMode(Lode::State& vm, const std::vector<Lode::Value>& args) {
        if (!stream || stream->type != UV_TTY) return Lode::Value();
        bool enable = args.empty() ? false : args[0].AsBoolean();
        uv_tty_set_mode(reinterpret_cast<uv_tty_t*>(stream), enable ? UV_TTY_MODE_RAW : UV_TTY_MODE_NORMAL);
        return Lode::Value();
    }
};

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();
    uv_loop_t* loop = vm.GetEventLoop().GetUVLoop();

    auto createWrite = [loop](int fd) {
        uv_handle_type type = uv_guess_handle(fd);
        uv_stream_t* stream = nullptr;
        if (type == UV_TTY) {
            uv_tty_t* tty = new uv_tty_t;
            if (uv_tty_init(loop, tty, fd, 0) < 0) {
                delete tty;
                return std::make_shared<WritableStream>();
            }
            stream = reinterpret_cast<uv_stream_t*>(tty);
        } else if (type == UV_NAMED_PIPE) {
            uv_pipe_t* pipe = new uv_pipe_t;
            if (uv_pipe_init(loop, pipe, 0) < 0 || uv_pipe_open(pipe, fd) < 0) {
                delete pipe;
                return std::make_shared<WritableStream>();
            }
            stream = reinterpret_cast<uv_stream_t*>(pipe);
        } else if (type == UV_FILE) {
            return std::make_shared<WritableStream>(loop, static_cast<uv_file>(fd));
        }
        return std::make_shared<WritableStream>(loop, stream);
    };

    auto createRead = [loop](int fd) {
        uv_handle_type type = uv_guess_handle(fd);
        uv_stream_t* stream = nullptr;
        if (type == UV_TTY) {
            uv_tty_t* tty = new uv_tty_t;
            if (uv_tty_init(loop, tty, fd, 1) < 0) {
                delete tty;
                return std::make_shared<ReadableStream>();
            }
            stream = reinterpret_cast<uv_stream_t*>(tty);
        } else if (type == UV_NAMED_PIPE) {
            uv_pipe_t* pipe = new uv_pipe_t;
            if (uv_pipe_init(loop, pipe, 0) < 0 || uv_pipe_open(pipe, fd) < 0) {
                delete pipe;
                return std::make_shared<ReadableStream>();
            }
            stream = reinterpret_cast<uv_stream_t*>(pipe);
        } else if (type == UV_FILE) {
            return std::make_shared<ReadableStream>(loop, static_cast<uv_file>(fd));
        }
        return std::make_shared<ReadableStream>(loop, stream);
    };

    auto cppStdin = createRead(0);
    auto cppStdout = createWrite(1);
    auto cppStderr = createWrite(2);

    Lode::Task::RegisterShutdownHook(vm, [cppStdin, cppStdout, cppStderr]() {
        cppStdin->Shutdown();
        cppStdout->Shutdown();
        cppStderr->Shutdown();
    });

    Lode::ClassBuilder<ReadableStream> readableBuilder(vm, "ReadableStream");
    readableBuilder.CustomConstructor([cppStdin](Lode::State&, const std::vector<Lode::Value>&) {
        return cppStdin;
    });
    
    // Bind member functions directly using the new ClassBuilder overloads
    readableBuilder.Method("read", &ReadableStream::read);
    readableBuilder.Method("readBuffer", &ReadableStream::readBuffer);
    readableBuilder.Method("readLine", &ReadableStream::readLine);
    readableBuilder.Method("readInto", &ReadableStream::readInto);
    readableBuilder.Method("readAsync", &ReadableStream::readAsync);
    readableBuilder.Method("readBufferAsync", &ReadableStream::readBufferAsync);
    readableBuilder.Method("readIntoAsync", &ReadableStream::readIntoAsync);
    readableBuilder.Method("isTTY", &ReadableStream::isTTY);
    readableBuilder.Method("setRawMode", &ReadableStream::setRawMode);

    Lode::ClassBuilder<WritableStream> writableBuilder(vm, "WritableStream");
    writableBuilder.CustomConstructor([cppStdout, cppStderr](Lode::State&, const std::vector<Lode::Value>& args) {
        int fd = (args.size() > 0 && args[0].IsNumber()) ? static_cast<int>(args[0].AsNumber()) : 1;
        if (fd == 2) return cppStderr;
        return cppStdout;
    });
    
    writableBuilder.Method("write", &WritableStream::write);
    writableBuilder.Method("writeLine", &WritableStream::writeLine);
    writableBuilder.Method("isTTY", &WritableStream::isTTY);
    writableBuilder.Method("getWindowSize", &WritableStream::getWindowSize);

    auto stdinVal = readableBuilder.Build().CallFunctionSingle("new").GetValue();
    
    auto stdoutClass = writableBuilder.Build();
    auto stdoutVal = stdoutClass.CallFunctionSingle("new", Lode::Value(1.0)).GetValue();
    auto stderrVal = stdoutClass.CallFunctionSingle("new", Lode::Value(2.0)).GetValue();

    exports.Set("stdin", stdinVal);
    exports.Set("stdout", stdoutVal);
    exports.Set("stderr", stderrVal);

    exports.Set("print", vm.CreateFastFunction([cppStdout](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!cppStdout->IsAvailable()) { vm.RaiseError("stdio stdout is unavailable"); return Lode::Value(); }
        std::string out;
        for (size_t i = 0; i < args.Size(); ++i) {
            if (i > 0) out += " ";
            if (args[i].IsString()) out += args[i].AsStringView();
            else if (args[i].IsNumber()) {
                double val = args[i].AsNumber();
                if (val == static_cast<int64_t>(val)) out += std::to_string(static_cast<int64_t>(val));
                else out += std::to_string(val);
            }
            else if (args[i].IsBoolean()) out += args[i].AsBoolean() ? "true" : "false";
            else out += "[Value]";
        }
        out += "\n";
        cppStdout->writeData(out.data(), out.size());
        return Lode::Value();
    }));

    exports.Set("eprint", vm.CreateFastFunction([cppStderr](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!cppStderr->IsAvailable()) { vm.RaiseError("stdio stderr is unavailable"); return Lode::Value(); }
        std::string out;
        for (size_t i = 0; i < args.Size(); ++i) {
            if (i > 0) out += " ";
            if (args[i].IsString()) out += args[i].AsStringView();
            else if (args[i].IsNumber()) {
                double val = args[i].AsNumber();
                if (val == static_cast<int64_t>(val)) out += std::to_string(static_cast<int64_t>(val));
                else out += std::to_string(val);
            }
            else if (args[i].IsBoolean()) out += args[i].AsBoolean() ? "true" : "false";
            else out += "[Value]";
        }
        out += "\n";
        cppStderr->writeData(out.data(), out.size());
        return Lode::Value();
    }));

    exports.Set("write", vm.CreateFastFunction([cppStdout](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!cppStdout->IsAvailable()) { vm.RaiseError("stdio stdout is unavailable"); return Lode::Value(); }
        if (args.Size() > 0) {
            if (args[0].IsString()) {
                std::string_view sv = args[0].AsStringView();
                cppStdout->writeData(sv.data(), sv.size());
            } else if (args[0].IsBuffer()) {
                size_t size = 0;
                void* ptr = args[0].AsBuffer(&size);
                if (ptr) cppStdout->writeData(static_cast<const char*>(ptr), size);
            }
        }
        return Lode::Value();
    }));

    exports.Set("prompt", vm.CreateFastFunction([cppStdin, cppStdout](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!cppStdin->stream && !cppStdin->fileMode) { vm.RaiseError("stdio stdin is unavailable"); return Lode::Value(); }
        if (!cppStdout->IsAvailable()) { vm.RaiseError("stdio stdout is unavailable"); return Lode::Value(); }
        if (args.Size() > 0 && args[0].IsString()) {
            std::string_view sv = args[0].AsStringView();
            cppStdout->writeData(sv.data(), sv.size());
        }
        
        cppStdin->mainL = vm.GetMainThread();
        ReadableStream::PendingRead req;
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        req.isLine = true;
        cppStdin->queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }));

    exports.Set("clear", vm.CreateFastFunction([cppStdout](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        if (!cppStdout->IsAvailable()) { vm.RaiseError("stdio stdout is unavailable"); return Lode::Value(); }
        const char* clearCmd = "\033[2J\033[H";
        cppStdout->writeData(clearCmd, std::strlen(clearCmd));
        return Lode::Value();
    }));

    auto selectFactory = vm.Require("@self/utils/selectFactory");
    auto selectFn = vm.CallFunction(selectFactory, stdinVal, stdoutVal).GetValue()[0];
    exports.Set("select", selectFn);

    return { Lode::Value(exports) };
}
