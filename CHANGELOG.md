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

### Changed
- Pinned the `nlohmann_json` and `rapidjson` `FetchContent` downloads with
  `URL_HASH` for supply-chain integrity and reproducible builds.
- `Lode::Detail::PathFromUtf8` no longer uses the C++20-deprecated
  `std::filesystem::u8path`.

### Removed
- The `temp/` scratch directory. Fixtures and scripts still in active use
  (native module fixtures, diagnostic scripts, the canonical integration
  test) moved to `tests/`; redundant manual scripts were folded into the
  `modules/sanity` regression suite or dropped where already covered.
