// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "FileSystem/FsHelpers.hpp"
#include "Lode/Table.hpp"
#include "Lode/Signal.hpp"

namespace lodefs
{

class ReadStream : public std::enable_shared_from_this<ReadStream>
{
public:
    std::shared_ptr<FsManager> mgr;
    std::shared_ptr<ReadStream> selfGuard;
    
    std::shared_ptr<Lode::Signal> dataReceived;
    std::shared_ptr<Lode::Signal> endOfStream;
    std::shared_ptr<Lode::Signal> errorOccurred;
    Lode::Value dataProxy;
    Lode::Value endProxy;
    Lode::Value errorProxy;
    
    uv_file fd = -1;
    bool reading = false;
    bool closing = false;
    bool closed = false;
    uint64_t offset = 0;
    
    uv_fs_t readReq;
    char buffer[65536]; // 64KB chunks
    
    ReadStream(std::shared_ptr<FsManager> m);
    ~ReadStream();
    
    void RequestClose();
    void FinishClosed();
    void CheckClosed();
    
    void ContinueRead();
    static void OnRead(uv_fs_t* req);
    
    Lode::Value MethodStart(Lode::State& vm, const std::vector<Lode::Value>& args);
};

Lode::Value WrapReadStream(Lode::State& vm, const std::shared_ptr<ReadStream>& stream, const Lode::Table& methods);

} // namespace lodefs
