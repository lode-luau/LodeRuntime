# Contributing to Lode

> [!TIP]
> Contributing for the first time? Read this document all the way through, open
> an issue to say hi, and ask questions. No question is too basic.

Contributions to Lode include code, documentation, answering user questions,
running the project's infrastructure, and advocating for the project.

The Lode project welcomes all contributions from anyone willing to work in
good faith with other contributors and the community. No contribution is too
small and all contributions are valued.

Lode is a runtime for Luau, built so that developers coming from Roblox
Studio can use the language they already know with almost no extra learning:
no sandboxing ceremony, no toolchain magic, just `lode_runtime script.luau`.
Keep that in mind when you contribute — if something feels like it only makes
sense inside a big company's build system, it probably does not belong here.

## Contents

* [Code of Conduct](#code-of-conduct)
* [Issues](#issues)
* [Pull Requests](#pull-requests)
* [Automation and bots](#automation-and-bots)
* [Developer's Certificate of Origin 1.1](#developers-certificate-of-origin-11)

## Code of Conduct

The Lode project has a
[Code of Conduct](CODE_OF_CONDUCT.md) to which all contributors must adhere.

## Issues

* [Asking for General Help](#asking-for-general-help)
* [Submitting a Bug Report](#submitting-a-bug-report)
* [Submitting a Feature Request](#submitting-a-feature-request)
* [Reporting a Security Issue](#reporting-a-security-issue)

### Asking for General Help

Open an issue and ask. Include what you are trying to do, what you expected
to happen, and what actually happened. Luau code snippets are welcome.

### Submitting a Bug Report

Bug reports are written in English and follow a fixed structure so they are
fast to triage:

1. **Summary** — one or two sentences, including the exact file and line
   references of the suspected code path when you have them.
2. **Repro** — the exact steps and the minimal Luau (and C++ when a native
   module is involved) needed to reproduce.
3. **Root cause** — your hypothesis about what is going wrong, if you have
   one.
4. **Suggested fix** — what you think the fix looks like, if you have an
   idea.
5. **Acceptance criteria** — concrete, testable conditions that the fix must
   satisfy, including regression coverage when applicable.

Reproduction steps must be based on verified behavior or clearly identified
code paths. Do not claim tests or probes that were not run. If you are not
sure whether something is a bug, open an issue anyway and describe what you
observed.

### Submitting a Feature Request

Describe the problem you are trying to solve, not just the solution you have
in mind. Mention how the feature fits the Lode vision: a runtime that lets
Roblox Studio developers write Luau with a comfortable API and a small
learning curve.

### Reporting a Security Issue

See [SECURITY.md](SECURITY.md). Do not open a public issue for security
bugs.

## Pull Requests

Pull Requests are the way concrete changes are made to the code,
documentation, dependencies, and tools contained in the `lode-luau/LodeRuntime`
repository.

### Setting up your local environment

* Clone the repository.
* CMake 3.15+ and a C++20 compiler (MSVC, GCC, or Clang).

Configure and build:

    cmake -B build
    cmake --build build --config Debug
    cmake --build build --config Release

### The Process of Making Changes

1. Start from the latest `main`:
   `git checkout main` followed by `git pull origin main`.
2. Create a branch with the daily work-batch naming scheme:
   `fix/runtime-issues-YYYYMMDD-N` (for example `fix/runtime-issues-20260806-1`).
   The date component is year, month, day with zero padding, and `N` is the
   next available numeric suffix for that date. Never reuse a suffix that
   already exists locally or on `origin`.
3. Make your changes. Each independent fix or feature gets its own commit.
4. Before every commit, inspect `git status`, `git diff`, and
   `git diff --check`.
5. Validate your changes (see below), then push the branch and open a pull
   request against `main`.
6. Keep the PR open for review. Do not merge immediately after creating it.

### Validating Changes

Run the full validation flow, Debug and Release sequentially:

* `build/bin/Debug/lode_runtime.exe modules/sanity/run.luau`
* `build/bin/Release/lode_runtime.exe modules/sanity/run.luau`
* `build/bin/Debug/lode_runtime.exe temp/test.luau`

The sanity suite currently contains 33 tests; treat the reported count as
authoritative if it changes. Validation must go through the real runtime path
(`lode_runtime` + `.luau`) — do not keep temporary probe executables in the
repository.

### Reviewing Pull Requests

All pull requests are reviewed by the maintainers. Reviews may take a few
days; be patient and respond to feedback. The base branch is always `main`.
The branch diff against `main` should be reviewed in full before the PR is
merged. Merges happen only after review and passing checks.

### Large Pull Requests

Keep pull requests focused. A large change is easier to review as several
smaller, focused PRs than as one big one. If your change spans the runtime
and modules, split it.

## Automation and bots

An automation tool or bot that is not managed by the Lode project must seek
explicit authorization by opening an issue before interacting with the
project. Interactions made by an unauthorized automation are subject to
immediate moderation enforcement on the automation and its owner without
notice.

## Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

 (a) The contribution was created in whole or in part by me and I
     have the right to submit it under the open source license
     indicated in the file; or

 (b) The contribution is based upon previous work that, to the best
     of my knowledge, is covered under an appropriate open source
     license and I have the right under that license to submit that
     work with modifications, whether created in whole or in part
     by me, under the same open source license (unless I am
     permitted to submit under a different license), as indicated
     in the file; or

 (c) The contribution was provided directly to me by some other
     person who certified (a), (b) or (c) and I have not modified
     it.

 (d) I understand and agree that this project and the contribution
     are public and that a record of the contribution (including all
     personal information I submit with it, including my sign-off) is
     maintained indefinitely and may be redistributed consistent with
     this project or the open source license(s) involved.

Commits must be signed with `git commit -s` so that the `Signed-off-by`
trailer is present. Signing applies to commits made after this policy is
adopted.
