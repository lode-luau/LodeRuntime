--!strict

-- Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
-- SPDX-License-Identifier: MIT

--- The marshaling benchmark fixture module.
---
--- Exposes the same add(a, b) through the three native-closure paths the
--- runtime provides so tests/native/marshal_bench.luau can compare their
--- per-call cost through the real runtime path.
export type MarshalModule = {
    --- Adds two numbers through the legacy boxing path (State::CreateFunction):
    --- arguments arrive as a heap-allocated vector of boxed Values and the
    --- result is a boxed Value.
    addLegacy: (a: number, b: number) -> number,

    --- Adds two numbers through the fast path (State::CreateFastFunction):
    --- arguments are read from the Luau stack via StackArgs (no per-argument
    --- boxing); the single result still goes through one boxed Value.
    addFast: (a: number, b: number) -> number,

    --- Adds all arguments through the zero-marshaling N-variant
    --- (State::CreateFastFunctionN): StackArgs reads plus direct stack pushes;
    --- no Lode::Value is constructed at all.
    --- @return The sum of all arguments.
    --- @return The number of arguments received.
    addFastN: (...number) -> (number, number),
}

return {} :: MarshalModule
