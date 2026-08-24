// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "FileSystem/FsHelpers.hpp"
#include "Lode/Table.hpp"

namespace lodefs
{

class FileHandle : public std::enable_shared_from_this<FileHandle>
{
public:
    std::shared_ptr<FsManager> mgr;
    std::shared_ptr<FileHandle> selfGuard;
    
    uv_file fd = -1;
    bool isOpen = false;
    bool closing = false;
    bool closed = false;
    int closeCount = 0;
    
    // For sync usage or fallback
    int openFlags = 0;
    
    void RequestClose();
    void FinishClosed();
    void CheckClosed();
    
    Lode::Value MethodRead(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodReadBuffer(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodWrite(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodSeek(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodStat(Lode::State& vm, Lode::StackArgs args);
    Lode::Value MethodSync(Lode::State& vm, Lode::StackArgs args);
};

Lode::Value WrapFileHandle(Lode::State& vm, const std::shared_ptr<FileHandle>& handle, const Lode::Table& methods);

} // namespace lodefs
