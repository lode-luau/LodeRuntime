// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
//
// C++ benchmark harness for the Lode public API (bench/cpp/Bench.cpp).
//
// Measures the native-side hot paths through the real LodeCore API:
//   - fast_closure_call       : CreateFastFunction closure called via Value::CallSingle
//   - slow_closure_call       : CreateFunction (vector<Value>) closure via CallSingle
//   - table_value_conversion  : Value::FromLuaState on a table (PinnedRef + registry ref)
//   - class_method_call       : ClassBuilder-bound method called via CallSingle
//   - state_create            : State::Create + destroy (VM init/teardown)
//   - get_set_global          : SetGlobal/GetGlobal round trip
//   - protected_call          : ExecuteBytecodeWithResults of a tiny chunk
//   - buffer_read_write       : Lode::Buffer Read/Write on a 64-byte buffer
//   - table_get_set           : Table::Set/Get round trip
//   - json_parse              : Lode::Json::Parse of a small document
//   - json_stringify          : Lode::Json::Stringify of a small Value tree
//   - class_builder_build     : ClassBuilder registration (properties + method)
//
// Methodology: adaptive timing (google-benchmark min_time model) — a doubling
// calibration probe estimates per-op cost, the iteration count targets a
// ~400 ms repeat, and the number of repeats follows a ~3.6 s per-scenario
// budget (7-10 repeats, arithmetic mean reported). Total runtime stays bounded
// even if a scenario becomes orders of magnitude slower or faster.
// Use Release builds only; numbers are only meaningful relative to each other
// (baseline vs change), not as absolute throughput claims.
#include "Lode/State.hpp"
#include "Lode/Value.hpp"
#include "Lode/Table.hpp"
#include "Lode/Buffer.hpp"
#include "Lode/ClassBuilder.hpp"
#include "Lode/Compiler.hpp"
#include "Lode/Json.hpp"
#include "Lode/Logger.hpp"
#include "Platform/CrashHandler.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

constexpr double kRepeatMs = 400.0;    // target wall time per measured repeat
constexpr double kBudgetMs = 3600.0;   // total per-scenario budget (repeats)
constexpr int kMinRepeats = 7;
constexpr int kMaxRepeats = 10;
constexpr int64_t kMaxIterations = 1000000000;

// Runs `body` for a time-based budget instead of a fixed iteration count:
// a doubling calibration probe estimates per-op cost, the iteration count is
// derived from kRepeatMs, and the number of repeats follows kBudgetMs. Slow
// ops are sampled fewer times, fast ops more — total runtime stays bounded
// regardless of how fast or slow a scenario becomes (see google benchmark's
// min_time model).
void Measure(const char* name, const std::function<void()>& body)
{
    int64_t probeIters = 1;
    double probeMs = 0.0;
    for (int pass = 0; pass < 12; ++pass)
    {
        auto start = Clock::now();
        for (int64_t i = 0; i < probeIters; ++i)
            body();
        probeMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        if (probeMs >= 25.0 || probeIters >= kMaxIterations)
            break;
        probeIters *= 10;
    }

    double perOpNs = std::max((probeMs * 1e6) / static_cast<double>(probeIters), 1.0);
    int64_t targetIters = static_cast<int64_t>(kRepeatMs * 1e6 / perOpNs);
    targetIters = std::max<int64_t>(1, std::min<int64_t>(targetIters, kMaxIterations));

    // Warmup: short untimed pass to fill caches.
    for (int64_t i = 0; i < targetIters / 10; ++i)
        body();

    double repeatMs = (perOpNs * static_cast<double>(targetIters)) / 1e6;
    int repeats = static_cast<int>(kBudgetMs / std::max(repeatMs, 1.0));
    repeats = std::max(kMinRepeats, std::min(repeats, kMaxRepeats));

    std::vector<double> nsPerOp;
    nsPerOp.reserve(static_cast<size_t>(repeats));
    for (int r = 0; r < repeats; ++r)
    {
        auto start = Clock::now();
        for (int64_t i = 0; i < targetIters; ++i)
            body();
        auto end = Clock::now();
        double ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        nsPerOp.push_back(ns / static_cast<double>(targetIters));
    }

    // Arithmetic mean over the repeats: with 7-10 samples the averaging
    // smooths run-to-run noise better than the old 3-10/median scheme.
    double sum = 0.0;
    for (double ns : nsPerOp)
        sum += ns;
    const double mean = sum / static_cast<double>(nsPerOp.size());
    std::printf("%-32s %12.2f ns/op\n", name, mean);
}

struct Vec
{
    double x = 0.0;
    double y = 0.0;

    double Length() const { return std::sqrt(x * x + y * y); }
};
} // namespace

int main()
{
    Lode::Platform::CrashHandler::Initialize();
    Lode::Logger::Initialize();

    auto stateResult = Lode::State::Create();
    Lode::State vm = std::move(stateResult.GetValue());
    lua_State* L = vm.GetLuaState();

    std::printf("=== LodeRuntime C++ benchmarks ===\n");

    // Fast closure: zero-allocation callback path (State wrapper + StackArgs).
    Lode::Value square = vm.CreateFastFunction([](Lode::State&, Lode::StackArgs args) -> Lode::Value {
        double v = args[0].AsNumber();
        return Lode::Value(v * v);
    });
    (void)square.CallSingle(vm, 3.0);
    Measure("fast_closure_call", [&] {
        (void)square.CallSingle(vm, 3.0);
    });

    // Slow closure: vector<Value> marshalling path (CreateFunction).
    Lode::Value identity = vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>& args) -> Lode::Value {
        return args.empty() ? Lode::Value() : args[0];
    });
    (void)identity.CallSingle(vm, 3.0);
    Measure("slow_closure_call", [&] {
        (void)identity.CallSingle(vm, 3.0);
    });

    // Reference conversion: capture a table as a pinned Lode::Value.
    Lode::Table sharedTable = vm.CreateTable();
    Measure("table_value_conversion", [&] {
        vm.PushTable(sharedTable);
        Lode::Value v = Lode::Value::FromLuaState(L, -1);
        vm.Pop(1);
        (void)v;
    });

    // ClassBuilder-bound method call (per-call binding overhead).
    Lode::ClassBuilder<Vec> builder(vm, "Vec");
    builder.Constructor();
    builder.Property("x", &Vec::x);
    builder.Property("y", &Vec::y);
    builder.Method("length", &Vec::Length);
    Lode::Table vecClass = builder.Build();
    Lode::Value newFn = vecClass.Get("new").GetValue();
    Lode::Value instance = newFn.CallSingle(vm, 3.0, 4.0).GetValue();
    Lode::Value lengthFn = vecClass.Get("length").GetValue();
    (void)lengthFn.CallSingle(vm, instance);
    Measure("class_method_call", [&] {
        (void)lengthFn.CallSingle(vm, instance);
    });

    // Full VM creation and teardown (luaL_newstate + openlibs + codegen +
    // module loader + event loop).
    Measure("state_create", [] {
        auto res = Lode::State::Create();
        if (res.IsError())
            return;
        Lode::State tmp = std::move(res.GetValue());
        (void)tmp;
    });

    // Global round trip through the C++ API.
    Measure("get_set_global", [&] {
        vm.SetGlobal("__bench_g", Lode::Value(3.0));
        (void)vm.GetGlobal("__bench_g");
    });

    // Precompiled bytecode execution (thread creation + ref + resume).
    // NOTE: ExecuteBytecodeWithResults leaves results on the main stack; the
    // caller must pop them (mirrors ProtectedCall's contract).
    std::string bytecode = Lode::Compiler::Compile("return 1 + 2", nullptr, "bench_protected_call.luau");
    auto firstCall = vm.ExecuteBytecodeWithResults(bytecode, "@bench_protected_call");
    if (!firstCall.IsError())
        vm.Pop(firstCall.GetValue());
    Measure("protected_call", [&] {
        auto r = vm.ExecuteBytecodeWithResults(bytecode, "@bench_protected_call");
        if (!r.IsError())
            vm.Pop(r.GetValue());
    });

    // Zero-copy buffer read/write.
    Lode::Value bufVal = vm.CreateBuffer(64);
    bufVal.PushToLuaState(L);
    Lode::Buffer benchBuf(L, -1);
    vm.Pop(1);
    Measure("buffer_read_write", [&] {
        benchBuf.WriteUInt32(0, 0xDEADBEEF);
        (void)benchBuf.ReadUInt32(0);
    });

    // Table::Set/Get round trip.
    Lode::Table benchTable = vm.CreateTable();
    benchTable.Set("key", Lode::Value(3.0));
    Measure("table_get_set", [&] {
        benchTable.Set("key", Lode::Value(3.0));
        (void)benchTable.Get("key");
    });

    // Native JSON parse/stringify (nlohmann-backed, shared by the json module).
    const std::string jsonDoc = R"({"name":"lode","version":"1.0.0","enabled":true,"tags":["a","b","c"],"nested":{"one":1,"two":2,"three":3},"items":[1,2,3,4,5,6,7,8,9,10],"mixed":[1,"two",3.5,true,null,{"k":false}]})";
    Measure("json_parse", [&] {
        (void)Lode::Json::Parse(vm, jsonDoc);
    });
    Lode::Value parsedJson = Lode::Json::Parse(vm, jsonDoc).GetValue();
    Measure("json_stringify", [&] {
        (void)Lode::Json::Stringify(parsedJson);
    });

    // ClassBuilder registration cost (properties + method + build).
    Measure("class_builder_build", [&] {
        Lode::ClassBuilder<Vec> b(vm, "BenchVec");
        b.Constructor();
        b.Property("x", &Vec::x);
        b.Property("y", &Vec::y);
        b.Method("length", &Vec::Length);
        (void)b.Build();
    });

    std::printf("=== done ===\n");
    return 0;
}
