# RFC: Static Compiled Module Implementations and Target Validation Plan

## Status

Proposed implementation plan. This document does not claim platform support.

## Current state

The runtime has a static-module registry and `LODE_STATIC` module macros. The
normal package loader also has a dynamic-library path. Windows x64 dynamic
packages are the only currently validated and published compiled-module path.

The repository recognizes `ios` as a package and CMake target identifier, but
has not built, linked, run, or shipped a static compiled implementation on
iOS. Dynamic third-party module loading must not be presented as an iOS
deployment path.

## Implementation phases

1. **Host static regression**
   - Add a native fixture compiled into the test executable with `LODE_STATIC`.
   - Require it by both registered names and verify that static resolution wins
     before any dynamic-library lookup.
   - Run the fixture in Debug and Release through CTest.

2. **Static build contract**
   - Define a supported CMake entry point for linking one or more module
     targets statically into an embedding application.
   - Document name registration, duplicate-name behavior, initialization order,
     and the absence of package archive discovery for static modules.
   - Add a consumer example separate from the dynamic `LodeNativeExample`.

3. **iOS validation**
   - Add an Apple toolchain job that builds the runtime and a statically linked
     fixture for an iOS simulator first, then a device target.
   - Run the fixture on the simulator and verify code-signing, registration,
     `require`, and shutdown behavior.
   - Record limitations such as embedding ownership, app-bundle layout, and
     prohibited dynamic loading.

4. **Release support decision**
   - Add iOS to the supported-target matrix only after both simulator and device
     validation are reproducible in CI.
   - Publish only artifacts and package metadata covered by that matrix; until
     then, manifests may recognize `ios` but releases must not claim it.

## Acceptance criteria

- The host static regression passes in Debug and Release.
- The static consumer example builds using the documented CMake API.
- iOS simulator and device jobs pass before iOS is listed as supported.
- Documentation distinguishes recognized identifiers, tested targets, and
  published package artifacts.
