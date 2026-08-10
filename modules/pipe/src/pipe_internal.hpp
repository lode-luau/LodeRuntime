// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
//
// pipe_internal.hpp - Internal declarations shared by the pipe implementation
// files (pipe_stream.cpp, pipe_server.cpp, pipe_helpers.cpp).
// NOT included by consumers; they only see pipe.h.
#ifndef LODE_PIPE_INTERNAL_HPP
#define LODE_PIPE_INTERNAL_HPP

#include "pipe.h"
#include "uv.h"

#include <string>
#include <vector>

namespace pipeimpl
{

// Shared helpers (pipe_helpers.cpp).
std::string NormalizePipePath(const std::string& path);

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

struct PipeStream
{
    uv_loop_t* loop = nullptr;

    uv_pipe_t pipe{};
    uv_connect_t connReq{};
    bool pipeInited = false;
    bool pipeClosed = false;
    bool open = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;

    pipe_connect_cb onConnect = nullptr;
    pipe_data_cb onData = nullptr;
    pipe_close_cb onClose = nullptr;
    pipe_error_cb onError = nullptr;
    void* ctx = nullptr;

    // Lifetime: a consumer reference (from pipe_stream_new / accept transfer)
    // plus an internal async reference held while uv work is active. The
    // object deletes itself when both drop to zero.
    int consumerRefs = 1;
    int asyncRefs = 0;

    void GrabAsync() { ++asyncRefs; }
    void DropAsync();
    void MaybeDelete();

    void FireConnect();
    void FireData(const char* data, size_t len);
    void FireClose();
    void FireError(const std::string& message);

    int BeginConnect(const std::string& path);
    void FailConnect(const std::string& message);

    void StartReading();
    void StopReading();
    int Write(const char* data, size_t len);
    void CloseHandles();
    void RequestClose();
    void CheckClosed();
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnConnected(uv_connect_t* req, int status);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

struct PipeServer
{
    uv_loop_t* loop = nullptr;

    uv_pipe_t pipe{};
    bool pipeInited = false;
    bool pipeClosed = false;
    bool listening = false;
    bool closing = false;
    bool closed = false;

    pipe_accept_cb onAccept = nullptr;
    pipe_error_cb onError = nullptr;
    void* ctx = nullptr;

    int consumerRefs = 1;
    int asyncRefs = 0;

    void GrabAsync() { ++asyncRefs; }
    void DropAsync();
    void MaybeDelete();

    void FireError(const std::string& message);
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void OnConnection(uv_stream_t* server, int status);
};

} // namespace pipeimpl

#endif