#include "Lode/State.hpp"
#include "Lode/Result.hpp"
#include "Platform/WindowsCrashHandler.hpp"
#include "Luau/Compiler.h"
#include "luacode.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    // Initialize Windows DbgHelp.dll crash handler for detailed exception reporting
    Lode::Platform::CrashHandler::Initialize();

    if (argc < 2)
    {
        std::cout << "LodeRuntime (lode_runtime) v1.0.0\n";
        std::cout << "Usage: lode_runtime <file.luac | file.luau>\n";
        return 1;
    }

    std::string filePath = argv[1];
    if (!fs::exists(filePath))
    {
        std::cerr << "Error: File not found: " << filePath << "\n";
        return 1;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Error: Failed to open file: " << filePath << "\n";
        return 1;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::string bytecode;
    if (filePath.size() >= 5 && filePath.substr(filePath.size() - 5) == ".luau")
    {
        size_t bytecodeSize = 0;
        char* compiled = luau_compile(content.c_str(), content.length(), nullptr, &bytecodeSize);
        if (!compiled)
        {
            std::cerr << "Error: Failed to compile Luau source file: " << filePath << "\n";
            return 1;
        }
        bytecode.assign(compiled, bytecodeSize);
        free(compiled);
    }
    else
    {
        bytecode = content;
    }

    auto stateResult = Lode::State::Create();
    if (stateResult.IsError())
    {
        std::cerr << "Error initializing runtime state: " << stateResult.GetError().GetMessage() << "\n";
        return 1;
    }

    Lode::State vm = std::move(stateResult.GetValue());

    fs::path absPath = fs::absolute(filePath);
    fs::path parentPath = absPath.parent_path();
    if (!parentPath.empty())
    {
        vm.AddModulePath(parentPath.string());
    }

    std::string chunkName = "@" + absPath.string();
    auto execResult = vm.ExecuteBytecode(bytecode, chunkName);
    if (execResult.IsError())
    {
        std::cerr << "Runtime Error: " << execResult.GetError().GetMessage() << "\n";
        return 1;
    }

    return 0;
}
