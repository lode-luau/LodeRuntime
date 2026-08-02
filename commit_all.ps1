# Commit script for LodeRuntime
# Executa os commits de forma lógica: primeiro as definições/engine core, depois o uso/testes.

Write-Host "=== Iniciando commits estruturados do LodeRuntime ===" -ForegroundColor Cyan

# 1. CMake & Infraestrutura do Event Loop (libuv)
git add CMakeLists.txt include/Lode/EventLoop.hpp src/EventLoop.cpp
git commit -m "feat(core): add libuv EventLoop integration and CMake dependencies"

# 2. Sistema de Erros e Tipos Base da Engine
git add include/Lode/Error.hpp src/Error.cpp include/Lode/Result.hpp include/Lode/Export.hpp
git commit -m "feat(core): enhance Error and Result handling for VM lifecycle"

# 3. Abstração de Metatable e API Expandida de Table
git add include/Lode/Metatable.hpp src/Metatable.cpp include/Lode/Table.hpp src/Table.cpp
git commit -m "feat(api): add first-class Metatable abstraction and dynamic table method calls"

# 4. Framework de Class Binding (ObjectWrap & ClassBuilder)
git add include/Lode/ObjectWrap.hpp include/Lode/ClassBuilder.hpp
git commit -m "feat(binding): implement RAII ObjectWrap and ClassBuilder C++ class binding framework"

# 5. Suite OS-Level Task & Timers Assíncronos
git add include/Lode/Task.hpp src/Task.cpp
git commit -m "feat(task): implement OS-level Task suite (Wait, SetTimeout, SetInterval, Defer, Spawn)"

# 6. Módulo Loader & Sistema de Require com Múltiplos Retornos Reais
git add include/Lode/Module.hpp src/Module.cpp src/ModuleLoader.hpp src/ModuleLoader.cpp src/Registry.hpp src/Registry.cpp
git commit -m "feat(module): implement native module loader with true multi-return require support and luarequire VFS navigation"

# 7. State & Coroutine Principal do Runtime Engine
git add include/Lode/State.hpp src/State.cpp src/Runtime.cpp src/Main.cpp include/Lode/Runtime.hpp include/Lode/Lode.hpp
git commit -m "feat(runtime): wrap script execution in Main Coroutine thread with libuv EventLoop runner"

# 8. Módulos de Teste & Exemplo de DLL Nativa C++
git add temp/pure_module/init.luau temp/native_module/CMakeLists.txt temp/native_module/lode.json temp/native_module/init.luau temp/native_module/src/native_module.cpp temp/test.luau
git commit -m "test(showcase): add pure Luau and native C++ DLL module integration tests"

Write-Host "=== Todos os commits foram realizados com sucesso! ===" -ForegroundColor Green
