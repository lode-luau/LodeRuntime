# LodeRuntime — Workflow

## Branch naming
- Daily work branches, one per batch: `fix/runtime-issues-YYYYMMDD-N` (e.g. `fix/runtime-issues-08052026-2`, then `-3`, ...).
- The old `fix/runtime-issues` long-lived branch is retired. Never use it.
- Start every new branch from the latest `main`.

## Flow
1. Update local `main`: `git checkout main; git pull origin main`.
2. Create branch: `git checkout -b fix/runtime-issues-YYYYMMDD-N`.
3. Make changes. Each fix/feature is its own commit.
4. Probe/test before committing (build both Debug and Release, run `modules/sanity/run.luau` and `temp/test.luau`).
5. Push: `git push -u origin fix/runtime-issues-YYYYMMDD-N`.
6. Create PR with base `main` (NOT `fix/runtime-issues`).
7. After merge, delete the branch locally and remotely.
8. Never merge a PR into any branch other than `main`.

## Build & test
- Configure: `cmake build` (in `build/`).
- Build Debug: `cmake --build build --config Debug`.
- Build Release: `cmake --build build --config Release`.
- Tests: `build/bin/Debug/lode_runtime.exe modules/sanity/run.luau` (19 tests) and `build/bin/Debug/lode_runtime.exe temp/test.luau`.

## Notes
- Do not keep temporary probe executables/targets in the repo; validate through the real runtime path (`lode_runtime` + `.luau`), not standalone probes that link Luau twice.
