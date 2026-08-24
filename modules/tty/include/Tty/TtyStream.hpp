// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "TtyExport.hpp"
#include "TtyManager.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"
#include <functional>
#include <memory>
#include <string>

namespace lodetty
{

struct TTY_API TtyStream : std::enable_shared_from_this<TtyStream>
{
    std::shared_ptr<TtyManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    uv_tty_t tty{};
    bool ttyInited = false;
    bool ttyClosed = false;
    bool open = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;
    bool readable = false;

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

    // Yield-based read
    lua_State* yieldThread = nullptr;
    int yieldBytes = -1;
    bool yieldingLine = false;
    bool yieldAsBuffer = false;
    std::string lineBuffer;

    std::shared_ptr<TtyStream> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void StartReading();
    void StopReading();

    std::string InitNative(int fd, bool readable);
    void WriteNative(const char* data, size_t size);

    Lode::Value MethodWrite(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodWriteLine(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodRead(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadBuffer(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadLine(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodStartStreaming(Lode::State& vm);
    Lode::Value MethodStopStreaming(Lode::State& vm);
    Lode::Value MethodGetWindowSize(Lode::State& vm);
    Lode::Value MethodSetMode(Lode::State& vm, Lode::StackArgs args);

    void RequestClose();
    void FinishClosed();
    void CheckYield(ssize_t nread, const uv_buf_t* buf);
    void FinishYield(const Lode::Value& val);

    static void OnHandleClosed(uv_handle_t* handle);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

TTY_API Lode::Value WrapTtyStream(Lode::State& vm, const std::shared_ptr<TtyStream>& stream, const Lode::Table& methods);

} // namespace lodetty
