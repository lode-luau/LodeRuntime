# LodeRuntime

A modular runtime environment for executing Luau scripts.

---

## Building the Project

### Prerequisites
* [CMake](https://cmake.org/) (3.15 or higher recommended)
* A C++ compiler (MSVC for Windows, GCC or Clang for Linux)

---

### Windows

**Debug Build:**
```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
```
**Release Build:**
```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```
---
### Linux
**Debug Build:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
**Release Build:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
---

### Running Tests
To run the test scripts using the Debug binary:
```bash
./build/bin/Debug/lode_runtime temp/test.luau
./build/bin/Debug/lode_runtime temp/testBase.luau
./build/bin/Debug/lode_runtime temp/testTask.luau
./build/bin/Debug/lode_runtime temp/test_simd.luau
./build/bin/Debug/lode_runtime temp/test_native_require.luau
```
---

### Usage
Run any Luau script by passing its path as an argument to the executable:
```bash
./build/bin/Release/lode_runtime path/to/script.luau
```
---

[License](LICENSE)

