// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "PipeExport.hpp"
#include "PipeManager.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <functional>
#include <memory>
#include <string>

namespace lodepipe
{

struct PIPE_API PipeStream : std::enable_shared_from_this<PipeStream>
{
    std::shared_ptr<PipeManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_pipe_t pipe{};
    uv_connect_t connReq{};
    bool pipeInited = false;
    bool pipeClosed = false;
    bool open = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;
    bool connectPending = false;

    // Connect coroutine (yield-based Connect)
    Lode::Coroutine connectCo;
    std::string connectPath;

    // Signals
    std::shared_ptr<Lode::Signal> dataSig;   // DataReceived
    std::shared_ptr<Lode::Signal> endSig;    // EndOfStream
    std::shared_ptr<Lode::Signal> errorSig;  // ErrorOccurred
    Lode::Value dataProxy;
    Lode::Value endProxy;
    Lode::Value errorProxy;

    // C++ callbacks (used when consumed by another native module)
    std::function<void(const char*, size_t)> cppOnData;
    std::function<void()> cppOnEnd;
    std::function<void(const std::string&)> cppOnError;

    // Yield-based read (modelo tty)
    lua_State* yieldThread = nullptr;
    int yieldBytes = -1;
    bool yieldingLine = false;
    bool yieldAsBuffer = false;
    std::string lineBuffer;

    std::shared_ptr<PipeStream> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void StartReading();
    void StopReading();

    std::string OpenFdNative(int fd);
    void ConnectNative(const std::string& path);
    void WriteNative(const char* data, size_t size);

    Lode::Value MethodConnect(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodOpenFD(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodWrite(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodWriteLine(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodRead(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodReadBuffer(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodReadLine(Lode::State& vm, const std::vector<Lode::Value>& args);
    Lode::Value MethodStartStreaming(Lode::State& vm);
    Lode::Value MethodStopStreaming(Lode::State& vm);

    void RequestClose();
    void FinishClosed();
    void FailConnect(const std::string& message);
    void CheckYield(ssize_t nread, const uv_buf_t* buf);
    void FinishYield(const Lode::Value& val);

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnConnected(uv_connect_t* req, int status);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

PIPE_API Lode::Value WrapPipeStream(Lode::State& vm, const std::shared_ptr<PipeStream>& stream, const Lode::Table& methods);

} // namespace lodepipe