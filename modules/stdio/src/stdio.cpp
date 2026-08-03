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
#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <cstring>
#include <algorithm>

namespace {
    void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
        buf->base = new char[suggested_size];
        buf->len = suggested_size;
    }
}

class WritableStream {
public:
    uv_stream_t* stream = nullptr;

    WritableStream() = default;
    explicit WritableStream(uv_stream_t* s) : stream(s) {}

    void writeData(const char* data, size_t len) {
        if (!stream || len == 0) return;
        
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

    Lode::Value write(Lode::State& vm, const std::vector<Lode::Value>& args) {
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
};

class ReadableStream {
public:
    uv_stream_t* stream = nullptr;
    std::vector<uint8_t> buffer;
    bool reading = false;
    Lode::State* vmPtr = nullptr;

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
    explicit ReadableStream(uv_stream_t* s) : stream(s) {}

    void startReadIfNeeded() {
        if (!stream || reading) return;
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

    bool tryResolve(PendingRead& req, Lode::Value& resultValue, bool eof = false) {
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
                    Lode::Value bufVal = vmPtr->CreateBuffer(bytesToConsume);
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
                else if (req.isBuffer) resultValue = vmPtr->CreateBuffer(0);
                else resultValue = Lode::Value(std::string(""));
            }
        }
        
        if (satisfied && bytesToConsume > 0) {
            buffer.erase(buffer.begin(), buffer.begin() + bytesToConsume);
        }
        return satisfied;
    }

    void processQueue(bool eof = false) {
        if (!vmPtr) return;
        
        while (!pendingQueue.empty()) {
            auto& req = pendingQueue.front();
            Lode::Value resultValue;
            
            if (tryResolve(req, resultValue, eof)) {
                auto currentReq = req;
                pendingQueue.pop_front();
                
                if (currentReq.isYield && currentReq.coroutine.IsValid()) {
                    std::vector<Lode::Value> resArgs;
                    if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) {
                        resArgs.push_back(resultValue);
                    }
                    currentReq.coroutine.Resume(resArgs);
                } else if (currentReq.isCallback && currentReq.callback.IsFunction()) {
                    std::vector<Lode::Value> cbArgs;
                    if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) {
                        cbArgs.push_back(resultValue);
                    }
                    Lode::Task::Spawn(*vmPtr, currentReq.callback, cbArgs);
                }
            } else {
                break; // Needs more data from libuv
            }
        }
        
        if (pendingQueue.empty() && reading) {
            uv_read_stop(stream);
            reading = false;
        }
    }

    void queueRequest(PendingRead req) {
        pendingQueue.push_back(req);
        startReadIfNeeded();
    }

    Lode::Value read(Lode::State& vm, const std::vector<Lode::Value>& args) {
        vmPtr = &vm;
        PendingRead req;
        req.requestedBytes = (args.size() > 0 && args[0].IsNumber()) ? static_cast<size_t>(args[0].AsNumber()) : 0;
        
        Lode::Value resultValue;
        if (tryResolve(req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }
    
    Lode::Value readBuffer(Lode::State& vm, const std::vector<Lode::Value>& args) {
        vmPtr = &vm;
        PendingRead req;
        req.isBuffer = true;
        req.requestedBytes = (args.size() > 0 && args[0].IsNumber()) ? static_cast<size_t>(args[0].AsNumber()) : 0;
        
        Lode::Value resultValue;
        if (tryResolve(req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }
    
    Lode::Value readLine(Lode::State& vm, const std::vector<Lode::Value>& args) {
        vmPtr = &vm;
        PendingRead req;
        req.isLine = true;
        
        Lode::Value resultValue;
        if (tryResolve(req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }

    Lode::Value readInto(Lode::State& vm, const std::vector<Lode::Value>& args) {
        vmPtr = &vm;
        if (args.empty() || !args[0].IsBuffer()) return Lode::Value(0.0);
        
        PendingRead req;
        req.isInto = true;
        req.targetBufferValue = args[0];
        req.offset = (args.size() > 1 && args[1].IsNumber()) ? static_cast<size_t>(args[1].AsNumber()) : 0;
        size_t bSize = 0;
        (void)args[0].AsBuffer(&bSize);
        size_t maxAvailable = (bSize > req.offset) ? (bSize - req.offset) : 0;
        size_t length = (args.size() > 2 && args[2].IsNumber()) ? static_cast<size_t>(args[2].AsNumber()) : maxAvailable;
        req.requestedBytes = (std::min)(length, maxAvailable);
        
        Lode::Value resultValue;
        if (tryResolve(req, resultValue)) return resultValue;
        
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }

    Lode::Value readAsync(Lode::State& vm, const std::vector<Lode::Value>& args) {
        vmPtr = &vm;
        if (args.empty() || !args[0].IsFunction()) return Lode::Value();
        
        PendingRead req;
        req.isCallback = true;
        req.callback = args[0];
        req.requestedBytes = (args.size() > 1 && args[1].IsNumber()) ? static_cast<size_t>(args[1].AsNumber()) : 0;
        
        Lode::Value resultValue;
        if (tryResolve(req, resultValue)) {
            std::vector<Lode::Value> cbArgs;
            if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) cbArgs.push_back(resultValue);
            Lode::Task::Spawn(vm, req.callback, cbArgs);
            return Lode::Value();
        }
        
        queueRequest(req);
        return Lode::Value();
    }

    Lode::Value readBufferAsync(Lode::State& vm, const std::vector<Lode::Value>& args) {
        vmPtr = &vm;
        if (args.empty() || !args[0].IsFunction()) return Lode::Value();
        
        PendingRead req;
        req.isCallback = true;
        req.callback = args[0];
        req.isBuffer = true;
        req.requestedBytes = (args.size() > 1 && args[1].IsNumber()) ? static_cast<size_t>(args[1].AsNumber()) : 0;
        
        Lode::Value resultValue;
        if (tryResolve(req, resultValue)) {
            std::vector<Lode::Value> cbArgs;
            if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) cbArgs.push_back(resultValue);
            Lode::Task::Spawn(vm, req.callback, cbArgs);
            return Lode::Value();
        }
        
        queueRequest(req);
        return Lode::Value();
    }

    Lode::Value readIntoAsync(Lode::State& vm, const std::vector<Lode::Value>& args) {
        vmPtr = &vm;
        if (args.size() < 2 || !args[0].IsBuffer() || !args[1].IsFunction()) return Lode::Value();
        
        PendingRead req;
        req.isCallback = true;
        req.targetBufferValue = args[0];
        req.callback = args[1];
        req.isInto = true;
        req.offset = (args.size() > 2 && args[2].IsNumber()) ? static_cast<size_t>(args[2].AsNumber()) : 0;
        size_t bSize = 0;
        (void)args[0].AsBuffer(&bSize);
        size_t maxAvailable = (bSize > req.offset) ? (bSize - req.offset) : 0;
        size_t length = (args.size() > 3 && args[3].IsNumber()) ? static_cast<size_t>(args[3].AsNumber()) : maxAvailable;
        req.requestedBytes = (std::min)(length, maxAvailable);
        
        Lode::Value resultValue;
        if (tryResolve(req, resultValue)) {
            std::vector<Lode::Value> cbArgs;
            if (resultValue.IsString() || resultValue.IsBuffer() || resultValue.IsNumber()) cbArgs.push_back(resultValue);
            Lode::Task::Spawn(vm, req.callback, cbArgs);
            return Lode::Value();
        }
        
        queueRequest(req);
        return Lode::Value();
    }
};

LODE_MODULE(vm)
{
    Lode::Table exports = vm.CreateTable();
    uv_loop_t* loop = Lode::EventLoop::Default().GetUVLoop();

    auto createWrite = [loop](int fd) {
        uv_handle_type type = uv_guess_handle(fd);
        uv_stream_t* stream = nullptr;
        if (type == UV_TTY) {
            uv_tty_t* tty = new uv_tty_t;
            uv_tty_init(loop, tty, fd, 0); 
            stream = reinterpret_cast<uv_stream_t*>(tty);
        } else if (type == UV_NAMED_PIPE) {
            uv_pipe_t* pipe = new uv_pipe_t;
            uv_pipe_init(loop, pipe, 0);
            uv_pipe_open(pipe, fd);
            stream = reinterpret_cast<uv_stream_t*>(pipe);
        }
        return std::make_shared<WritableStream>(stream);
    };

    auto createRead = [loop](int fd) {
        uv_handle_type type = uv_guess_handle(fd);
        uv_stream_t* stream = nullptr;
        if (type == UV_TTY) {
            uv_tty_t* tty = new uv_tty_t;
            uv_tty_init(loop, tty, fd, 1);
            stream = reinterpret_cast<uv_stream_t*>(tty);
        } else if (type == UV_NAMED_PIPE) {
            uv_pipe_t* pipe = new uv_pipe_t;
            uv_pipe_init(loop, pipe, 0);
            uv_pipe_open(pipe, fd);
            stream = reinterpret_cast<uv_stream_t*>(pipe);
        }
        return std::make_shared<ReadableStream>(stream);
    };

    auto cppStdin = createRead(0);
    auto cppStdout = createWrite(1);
    auto cppStderr = createWrite(2);

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

    Lode::ClassBuilder<WritableStream> writableBuilder(vm, "WritableStream");
    writableBuilder.CustomConstructor([cppStdout, cppStderr](Lode::State&, const std::vector<Lode::Value>& args) {
        int fd = (args.size() > 0 && args[0].IsNumber()) ? static_cast<int>(args[0].AsNumber()) : 1;
        if (fd == 2) return cppStderr;
        return cppStdout;
    });
    
    writableBuilder.Method("write", &WritableStream::write);
    writableBuilder.Method("writeLine", &WritableStream::writeLine);

    auto stdinVal = readableBuilder.Build().CallFunctionSingle("new").GetValue();
    
    auto stdoutClass = writableBuilder.Build();
    auto stdoutVal = stdoutClass.CallFunctionSingle("new", Lode::Value(1.0)).GetValue();
    auto stderrVal = stdoutClass.CallFunctionSingle("new", Lode::Value(2.0)).GetValue();

    exports.Set("stdin", stdinVal);
    exports.Set("stdout", stdoutVal);
    exports.Set("stderr", stderrVal);

    exports.Set("print", vm.CreateFastFunction([cppStdout](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
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
        if (args.Size() > 0 && args[0].IsString()) {
            std::string_view sv = args[0].AsStringView();
            cppStdout->writeData(sv.data(), sv.size());
        }
        
        cppStdin->vmPtr = &vm;
        ReadableStream::PendingRead req;
        req.isYield = true;
        req.coroutine = Lode::Coroutine(vm.GetLuaState());
        req.isLine = true;
        cppStdin->queueRequest(req);
        vm.YieldThread();
        return Lode::Value();
    }));

    exports.Set("clear", vm.CreateFastFunction([cppStdout](Lode::State& vm, Lode::StackArgs args) -> Lode::Value {
        const char* clearCmd = "\033[2J\033[H";
        cppStdout->writeData(clearCmd, std::strlen(clearCmd));
        return Lode::Value();
    }));

    return { Lode::Value(exports) };
}
