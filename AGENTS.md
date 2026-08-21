# LodeRuntime — Workflow

## Branch naming
- Daily work branches, one per batch: `fix/runtime-issues-YYYYMMDD-N`.
- The date component is year, month, day, with zero padding: `20260805` means 2026-08-05. Use the next available numeric suffix for that date, for example `fix/runtime-issues-20260805-3`.
- The old `fix/runtime-issues` long-lived branch is retired. Never use it.
- Start every new branch from the latest `main`.
- Do not reuse a suffix that already exists locally or on `origin`.

## Flow
1. Update local `main`: `git checkout main` followed by `git pull origin main`.
2. Check existing branch suffixes and create `fix/runtime-issues-YYYYMMDD-N` from the updated `main`.
3. Make changes. Each independent fix or feature gets its own commit.
4. Before every commit, inspect `git status`, `git diff`, and `git diff --check`, then run the required validation below.
5. Push: `git push -u origin fix/runtime-issues-YYYYMMDD-N`.
6. Create the PR with base `main`, never `fix/runtime-issues`.
7. Keep the PR open for review. Do not merge immediately after creation or bypass required checks.
8. Merge only into `main`, after review and passing checks.
9. After merge, update local `main`, delete the local branch, delete the remote branch, and prune stale remote refs.

## Build & test
- Configure when needed: `cmake -B build` (Visual Studio multi-config, single `build/` tree; CI uses Ninja with `build-debug/`/`build-release/` — see `CONTRIBUTING.md` and `.github/workflows/ci.yml`).
- Build Debug: `cmake --build build --config Debug`.
- Build Release: `cmake --build build --config Release`.
- Tests: `build/bin/Debug/lode.exe modules/sanity/run.luau`, `build/bin/Release/lode.exe modules/sanity/run.luau`, and `ctest --test-dir build -C Debug --output-on-failure` (native fixtures under `tests/native/` and diagnostics under `tests/diagnostics/`).
- The sanity suite currently contains 33 tests; treat the reported count as authoritative if it changes.
- Run Debug and Release builds sequentially when native DLL copy steps can conflict; retry a failed copy-only build before changing code.

## Commit & PR checklist
- Keep commits focused and mention the issue or behavior they address when applicable.
- When a commit addresses a tracked issue, include its reference in the subject using `(#N)`, for example: `filesystem: add glob matching (#96)`.
- Do not commit generated build output, temporary probe executables, secrets, or unrelated worktree changes.
- Review the complete branch diff against `main` before opening the PR.
- In the PR description, list the validation commands and link the relevant issues.
- In PR summaries, mention related issues in the relevant change bullets using `(#N)`, for example: `- Add filesystem globbing (#96).`.
- Create PR descriptions with a reviewed file and `gh pr create --body-file <file>` or `gh pr edit --body-file <file>`. Never pass multiline Markdown containing backticks, `$()`, or shell metacharacters in a double-quoted inline argument.
- Before creating or merging a PR, run `gh pr view <number> --json body --jq .body` and verify the body contains only the intended Markdown, not command output, logs, or literal escape sequences.
- Keep validation commands in fenced or inline code in the PR body; do not paste their output unless explicitly required.
- When closing multiple issues from a PR, put each closing keyword on its own line, for example: `Closes #88` followed by `Closes #94`; do not combine multiple issues on one `Closes` line.
- Do not close issues manually if the PR uses `Closes #N`; let GitHub close them after the merge.
- Merge with `gh pr merge <number> --merge --delete-branch`, which merges into `main` and deletes the branch automatically.

## Issue Writing
- Write issue descriptions in English using this structure: `Summary`, `Repro`, `Root cause`, `Suggested fix`, and `Acceptance criteria`.
- Include exact file and line references in the `Summary` or `Repro` section.
- Reproduction steps must be based on verified behavior or clearly identified code paths; do not claim tests or probes that were not run.
- Add `Note on the API` or `Additional note` only when it provides relevant technical context.
- Acceptance criteria must be concrete, testable, and include regression coverage when applicable.

## Security triage
- Judge potential security issues against the threat model in `SECURITY.md`.
- Native modules and Luau scripts are trusted-by-design (like Node.js): do not report missing sandboxing, missing permission models, or unrestricted `require` path traversal as vulnerabilities.
- The `lode.json` path constraint and the `LodeModuleInit` entrypoint contract are interface/packaging checks, not security boundaries.

## Notes
- Do not keep temporary probe executables/targets in the repo; validate through the real runtime path (`lode` + `.luau`), not standalone probes that link Luau twice.
- Native modules and asynchronous resources must be tested through the real runtime path and must be shut down before the owning `State` is destroyed.
- The canonical integration/regression tests are the CTest suite under `tests/` (native fixtures + diagnostics, see `tests/CMakeLists.txt`) and `modules/sanity/run.luau`.
