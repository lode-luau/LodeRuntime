// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
//
// tty_internal.hpp - Internal declarations shared by the tty implementation
// files (tty_stream.cpp, tty_helpers.cpp).
// NOT included by consumers; they only see tty.h.
#ifndef LODE_TTY_INTERNAL_HPP
#define LODE_TTY_INTERNAL_HPP

#include "tty.h"
#include "uv.h"

#include <string>
#include <vector>

namespace ttyimpl
{

// Shared helpers (tty_helpers.cpp).
bool IsTtyFd(int fd);

struct WriteRequest
{
    uv_write_t req;
    std::vector<char> data;
};

struct TtyStream
{
    uv_loop_t* loop = nullptr;

    uv_tty_t tty{};
    bool ttyInited = false;
    bool ttyClosed = false;
    bool open = false;
    bool reading = false;
    bool closing = false;
    bool closed = false;
    bool readable = false;

    tty_data_cb onData = nullptr;
    tty_close_cb onClose = nullptr;
    tty_error_cb onError = nullptr;
    void* ctx = nullptr;

    // Lifetime: a consumer reference (from tty_stream_new) plus an internal
    // async reference held while uv work is active. The object deletes itself
    // when both drop to zero.
    int consumerRefs = 1;
    int asyncRefs = 0;

    void GrabAsync() { ++asyncRefs; }
    void DropAsync();
    void MaybeDelete();

    void FireData(const char* data, size_t len);
    void FireClose();
    void FireError(const std::string& message);

    void StartReading();
    void StopReading();
    int Write(const char* data, size_t len);
    void CloseHandles();
    void RequestClose();
    void FinishClosed();

    static void OnHandleClosed(uv_handle_t* handle);
    static void AllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buf);
    static void OnRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void OnWritten(uv_write_t* req, int status);
};

} // namespace ttyimpl

#endif