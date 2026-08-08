# LodeRuntime Benchmark Harness

Benchmarks live here so optimizations are measured, not guessed. The rule is
simple: **a change only lands if its before/after numbers prove an improvement
(or are explicitly behavior-neutral).**

## Layout

```
bench/
  CMakeLists.txt            # lode_bench target (built only with -DLODE_BUILD_BENCH=ON)
  run_bench.ps1             # one script: self-test + measure + compare/record + save
  cpp/Bench.cpp             # C++ side measurements through the Lode public API
  luau/run.luau             # Luau runner (adaptive timing, median ns/op)
  luau/scenarios/           # scenario scripts loaded by run.luau
  luau/cli_warm.ps1         # warm CLI startup measurement (compile-cache path)
  fixtures/big.luau         # ~2.5k line deterministic fixture for cli_warm
  fixtures/gen_big.luau     # Luau generator for big.luau (Lode modules only)
  results/                  # baselines + per-run JSON history (see below)
```

## Two baselines, one script

Benchmark numbers are machine-specific, so there are two baselines:

| Baseline | File | Tracked | Used by |
|---|---|---|---|
| Local | `bench/results/local-baseline.json` | **gitignored** | anyone running `run_bench.ps1` on their own machine |
| CI | `bench/results/ci-baseline.json` | committed | `.github/workflows/benchmark.yml` (windows-latest runners) |

- **Local** — the first run on a machine records its baseline automatically;
  later runs print a delta table against it. Refresh it with
  `-RecordBaseline` (after a hardware/OS change or an intentional code
  change). The gate is informational by default; pass `-FailOnRegression`
  to fail on regressions.
- **CI** — the workflow always compares CI vs CI against the committed
  `ci-baseline.json` and fails the job on regressions. Refresh the public
  baseline on purpose with `workflow_dispatch` + `record-baseline: true`,
  which records and commits the new baseline. Results are also uploaded as
  a `bench-results-<sha>` artifact on every run.

Anyone can reproduce the local flow: checkout, build, run `run_bench.ps1`.
No shared state is required — each machine records its own reference.

## How to run locally

```bash
cmake -B build -DLODE_BUILD_BENCH=ON
cmake --build build --config Release
powershell -NoProfile -ExecutionPolicy Bypass -File bench/run_bench.ps1
```

First run records your local baseline; subsequent runs compare. Options:
`-Build` (configure+build first), `-Baseline <path>`, `-ThresholdPct <n>`
(default 5.0), `-RecordBaseline`, `-FailOnRegression`. Only Release builds
are benchmarked — `-Config Debug` is rejected because unoptimized timing is
meaningless.

## What each scenario measures

### C++ (`lode_bench` — through the Lode public API)

| Scenario | Code path |
|---|---|
| `fast_closure_call` | `CreateFastFunction` closure via `Value::CallSingle` — per-callback `State` wrapper, `StackArgs` |
| `slow_closure_call` | `CreateFunction` closure — `vector<Value>` marshalling |
| `table_value_conversion` | `Value::FromLuaState` on a table — `PinnedRef` + registry ref + `StateLifetime` lookup |
| `class_method_call` | `ClassBuilder`-bound method via `CallSingle` — per-call binding overhead |
| `state_create` | `State::Create` + destroy (VM init/teardown) |
| `get_set_global` | `SetGlobal`/`GetGlobal` round trip |
| `protected_call` | `ExecuteBytecodeWithResults` of a tiny precompiled chunk (thread creation + ref + resume) |
| `buffer_read_write` | `Lode::Buffer` Read/Write on a 64-byte buffer |
| `table_get_set` | `Table::Set`/`Get` round trip |
| `json_parse` / `json_stringify` | `Lode::Json::Parse`/`Stringify` (nlohmann-backed, shared with the json module) |
| `class_builder_build` | `ClassBuilder` registration (properties + method + build) |

### Luau (`bench/luau/run.luau`)

| Scenario | Code path |
|---|---|
| `native_square_call` | fast closure call from Lua |
| `native_roundtrip_ref` | reference marshalling through the slow path |
| `native_roundtrip_string` | string marshalling through the fast path |
| `class_method_call` | `ClassBuilder`-bound method from Lua |
| `coroutine_spawn_resume` | `coroutine.create` + `resume` (per-task path) |
| `fs_read_file` / `fs_write_file` / `fs_stat` / `fs_read_dir` | filesystem module native calls |
| `json_parse` / `json_stringify` | json module native calls |
| `path_resolve` | path module `resolve` (real canonicalization, `@self` handling) |
| `path_join` | path module `join` (pure lexical — regression guard against disk I/O per call) |

### CLI

| Scenario | Code path |
|---|---|
| `cli_warm_ms` | median warm-run wall time of a cached script (compile-cache hit path) |

## Methodology

- **Release builds only** — Debug is rejected by `run_bench.ps1` (unoptimized
  timing is meaningless).
- **Adaptive timing** (google-benchmark `min_time` model, in both harnesses):
  a doubling calibration probe estimates per-op cost, the iteration count
  targets a ~400 ms repeat, and the number of repeats follows a ~2 s
  per-scenario budget (3-10 repeats). Slow ops are sampled fewer times, fast
  ops more — total runtime stays bounded (~30 s per harness) even if a
  scenario becomes orders of magnitude slower or faster.
- `run_bench.ps1` self-tests the infrastructure first (binaries exist,
  fixture generation is deterministic, every benchmark exits 0 and emits its
  expected metrics); any mechanical failure aborts before measuring.
- Warmup pass before measuring (caches, branch predictors, codegen).
- **median** reported (robust against outliers/CPU noise).
- Compare medians of the *same harness* on baseline vs change, **on the same
  machine** — cross-machine or cross-session deltas are noise (locally the
  delta table is informational unless `-FailOnRegression` is passed).
- **Decision rule**: a change counts as an improvement only when the targeted
  scenario moves >2-3% past noise, with no regression on the other scenarios.
  Neutral or negative changes are dropped or reworked — never merged on hope.

## The fixture

`big.luau` is generated by `bench/fixtures/gen_big.luau`, a pure-Luau script
using only Lode's own modules:

```bash
build/bin/Release/lode_runtime.exe bench/fixtures/gen_big.luau
```

It resolves the output path via `path.resolve("@self", "big.luau")` (the
package dir of the script, independent of the working directory) and uses a
deterministic LCG, so `big.luau` is stable across runs and machines. Commit
the regenerated fixture alongside any generator change.
