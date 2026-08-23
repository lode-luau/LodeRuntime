# Changelog

All notable changes to LodeRuntime are documented here. The current
runtime/SDK line is `1.0.0`, but the project still ships only nightly builds
(`1.0.0-nightly.YYYYMMDD.N`); this does not represent a stable release.
Entries are grouped by date until a stable release is declared.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- `ci` workflow: build + validate (sanity suite and CTest) on every pull
  request and push to `main`, in addition to the existing nightly build.
- `tests/` directory: native module fixtures (`tests/native/`) and diagnostic
  regression scripts (`tests/diagnostics/`), wired into CTest via
  `tests/CMakeLists.txt`. Replaces the ad hoc `temp/` scratch directory.
- Positive-path regression coverage for `tcp`, `udp`, and `socket` in
  `modules/sanity/tests/`.
- `.clang-format` and `.editorconfig` for consistent code style.
- CLI performance flags consumed before script arguments: `--opt=<0-2>`
  (bytecode optimization level), `--codegen=native|all|off` (native codegen
  scope), `--gc-goal/--gc-stepmul/--gc-stepsize` (incremental GC tunables)
  and `--mem-limit=<mb>` (soft heap limit enforced from the event loop).
- `Lode::EventLoop::SetGcBudget`: donates time to the incremental GC between
  event loop iterations and enforces soft/hard heap limits when configured.
- `Lode::State::SetCodeGenMode`: selects whether native codegen compiles only
  `--!native` modules (default), every function, or nothing.

### Changed
- Pinned the `nlohmann_json` and `rapidjson` `FetchContent` downloads with
  `URL_HASH` for supply-chain integrity and reproducible builds.
- `Lode::Detail::PathFromUtf8` no longer uses the C++20-deprecated
  `std::filesystem::u8path`.
- Performance: `Lode::Buffer` caches the pinned buffer pointer/size captured
  at construction, removing a registry roundtrip from every read/write.
- Performance: `ClassBuilder` conversions use zero-copy `string_view`
  arguments and return integers through the 64-bit integer path; bound methods
  returning `bool` now produce real booleans instead of numbers.

### Removed
- The `temp/` scratch directory. Fixtures and scripts still in active use
  (native module fixtures, diagnostic scripts, the canonical integration
  test) moved to `tests/`; redundant manual scripts were folded into the
  `modules/sanity` regression suite or dropped where already covered.
