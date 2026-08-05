# LodeRuntime — Workflow

## Branch naming
- Daily work branches, one per batch: `fix/runtime-issues-YYYYDDMM-N`.
- The date component is year, day, month, with zero padding: `20260508` means 2026-08-05. Use the next available numeric suffix for that date, for example `fix/runtime-issues-20260508-3`.
- The old `fix/runtime-issues` long-lived branch is retired. Never use it.
- Start every new branch from the latest `main`.
- Do not reuse a suffix that already exists locally or on `origin`.

## Flow
1. Update local `main`: `git checkout main` followed by `git pull origin main`.
2. Check existing branch suffixes and create `fix/runtime-issues-YYYYDDMM-N` from the updated `main`.
3. Make changes. Each independent fix or feature gets its own commit.
4. Before every commit, inspect `git status`, `git diff`, and `git diff --check`, then run the required validation below.
5. Push: `git push -u origin fix/runtime-issues-YYYYDDMM-N`.
6. Create the PR with base `main`, never `fix/runtime-issues`.
7. Keep the PR open for review. Do not merge immediately after creation or bypass required checks.
8. Merge only into `main`, after review and passing checks.
9. After merge, update local `main`, delete the local branch, delete the remote branch, and prune stale remote refs.

## Build & test
- Configure when needed: `cmake -B build`.
- Build Debug: `cmake --build build --config Debug`.
- Build Release: `cmake --build build --config Release`.
- Tests: `build/bin/Debug/lode_runtime.exe modules/sanity/run.luau`, `build/bin/Release/lode_runtime.exe modules/sanity/run.luau`, and `build/bin/Debug/lode_runtime.exe temp/test.luau`.
- The sanity suite currently contains 22 tests; treat the reported count as authoritative if it changes.
- Run Debug and Release builds sequentially when native DLL copy steps can conflict; retry a failed copy-only build before changing code.

## Commit & PR checklist
- Keep commits focused and mention the issue or behavior they address when applicable.
- Do not commit generated build output, temporary probe executables, secrets, or unrelated worktree changes.
- Review the complete branch diff against `main` before opening the PR.
- In the PR description, list the validation commands and link the relevant issues.
- Do not close issues manually if the PR uses `Closes #N`; let GitHub close them after the merge.

## Issue Writing
- Write issue descriptions in English using this structure: `Summary`, `Repro`, `Root cause`, `Suggested fix`, and `Acceptance criteria`.
- Include exact file and line references in the `Summary` or `Repro` section.
- Reproduction steps must be based on verified behavior or clearly identified code paths; do not claim tests or probes that were not run.
- Add `Note on the API` or `Additional note` only when it provides relevant technical context.
- Acceptance criteria must be concrete, testable, and include regression coverage when applicable.

## Notes
- Do not keep temporary probe executables/targets in the repo; validate through the real runtime path (`lode_runtime` + `.luau`), not standalone probes that link Luau twice.
- Native modules and asynchronous resources must be tested through the real runtime path and must be shut down before the owning `State` is destroyed.
- The canonical integration test is `temp/test.luau`; the canonical regression suite is `modules/sanity/run.luau`.
