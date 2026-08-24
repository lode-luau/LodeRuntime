// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "StdioExport.hpp"
#include <string>
#include <vector>
#include <memory>
#include <deque>
#include <functional>
#include "StdioManager.hpp"
#include "Lode/Coroutine.hpp"
#include "Lode/Signal.hpp"
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "uv.h"

namespace lodestdio
{

struct STDIO_API StdioStream : std::enable_shared_from_this<StdioStream>
{
    std::shared_ptr<StdioManager> mgr;
    lua_State* mainL = nullptr;
    uv_loop_t* loop = nullptr;

    int fd = -1;
    bool readable = false;
    uv_handle_type handleType = UV_UNKNOWN_HANDLE;

    uv_tty_t ttyHandle{};
    uv_pipe_t pipeHandle{};
    uv_stream_t* streamHandle = nullptr;

    uv_file fileHandle = -1;
    bool fileMode = false;
    int64_t fileOffset = 0;

    bool handleInited = false;
    bool open = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;

    // Signals
    std::shared_ptr<Lode::Signal> dataSig;    // DataReceived
    std::shared_ptr<Lode::Signal> endSig;     // EndOfStream
    std::shared_ptr<Lode::Signal> errorSig;   // ErrorOccurred
    std::shared_ptr<Lode::Signal> resizeSig;  // WindowResized
    Lode::Value dataProxy;
    Lode::Value endProxy;
    Lode::Value errorProxy;
    Lode::Value resizeProxy;

    // C++ callbacks (used when consumed by another native module)
    std::function<void(const char*, size_t)> cppOnData;
    std::function<void()> cppOnEnd;
    std::function<void(const std::string&)> cppOnError;

    // Read queue & buffering
    std::vector<uint8_t> readBuffer;

    struct PendingRead
    {
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

    std::shared_ptr<StdioStream> selfGuard;

    void InitSignals(Lode::State& vm);
    void FireError(const std::string& message);
    void StartReading();
    void StopReading();

    std::string InitNative(int fd, bool readable);
    void WriteNative(const char* data, size_t size);

    void ProcessQueue(bool isEof = false);
    bool TryResolve(Lode::State& vm, const PendingRead& req, Lode::Value& outVal);
    void QueueRequest(const PendingRead& req);
    void StartFileRead();

    Lode::Value MethodWrite(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodWriteLine(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodRead(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadBuffer(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadLine(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadInto(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadAsync(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadBufferAsync(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadIntoAsync(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodStartStreaming(Lode::State& vm);
    Lode::Value MethodStopStreaming(Lode::State& vm);
    Lode::Value MethodGetWindowSize(Lode::State& vm);
    Lode::Value MethodSetMode(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodSetRawMode(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodIsTTY(Lode::State& vm);

    void RequestClose();
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

STDIO_API Lode::Value WrapStdioStream(Lode::State& vm, const std::shared_ptr<StdioStream>& stream, const Lode::Table& methods);

} // namespace lodestdio
