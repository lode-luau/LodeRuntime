// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "ProjectInitializer.hpp"

#include "PathUtil.hpp"
#include "Platform/Platform.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <system_error>

namespace fs = std::filesystem;

namespace Lode::Package
{
namespace
{
using json = nlohmann::json;
using Lode::Detail::PathToUtf8;

void Error(ProjectInitResult& result, std::string message)
{
    result.errors.push_back(std::move(message));
}

bool IsSemVer(const std::string& value)
{
    static const std::regex pattern(
        R"(^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$)"
    );
    return std::regex_match(value, pattern);
}

bool IsSafeName(const std::string& value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-' || character == '_' || character == '.';
    });
}

std::string LibraryExtension(std::string_view platform)
{
    if (platform == "windows") return ".dll";
    if (platform == "macos" || platform == "ios") return ".dylib";
    return ".so";
}

bool WriteTextFile(const fs::path& path, const std::string& content, ProjectInitResult& result)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        Error(result, "Cannot create project file: " + PathToUtf8(path));
        return false;
    }
    output << content;
    if (!output.good())
    {
        Error(result, "Cannot write project file: " + PathToUtf8(path));
        return false;
    }
    return true;
}

std::string LicenseText(const ProjectInitOptions& options)
{
    if (options.license != "MIT")
        return "SPDX-License-Identifier: " + options.license + "\n";

    return "MIT License\n\n"
        "Copyright (c) " + options.name + "\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
        "of this software and associated documentation files (the \"Software\"), to deal\n"
        "in the Software without restriction, including without limitation the rights\n"
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
        "copies of the Software, and to permit persons to whom the Software is\n"
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in all\n"
        "copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
        "SOFTWARE.\n";
}
}

ProjectInitResult InitializeProject(const fs::path& projectRoot, const ProjectInitOptions& options)
{
    ProjectInitResult result;
    if (!IsSafeName(options.name))
        Error(result, "Project name must contain only letters, digits, '.', '_' or '-'.");
    if (options.description.empty())
        Error(result, "Project description must not be empty.");
    if (!IsSemVer(options.version))
        Error(result, "Project version must be a valid SemVer string.");
    if (options.license.empty())
        Error(result, "Project license must not be empty.");
    if (!result.IsValid())
        return result;

    std::error_code ec;
    const fs::path root = fs::absolute(projectRoot, ec);
    if (ec)
    {
        Error(result, "Cannot resolve project directory: " + ec.message());
        return result;
    }
    if (fs::exists(root, ec) && !fs::is_directory(root, ec))
    {
        Error(result, "Project path is not a directory: " + PathToUtf8(root));
        return result;
    }
    if (fs::is_directory(root, ec) && fs::directory_iterator(root, ec) != fs::directory_iterator())
    {
        Error(result, "Project directory is not empty: " + PathToUtf8(root));
        return result;
    }
    if (!fs::exists(root, ec) && !fs::create_directories(root, ec))
    {
        Error(result, "Cannot create project directory: " + ec.message());
        return result;
    }
    if (ec)
    {
        Error(result, "Cannot inspect project directory: " + ec.message());
        return result;
    }

    json manifest = {
        { "name", options.name },
        { "version", options.version },
        { "description", options.description },
        { "license", options.license }
    };
    std::string nativeSource;
    if (options.native)
    {
        const std::string platform = std::string(Platform::GetOSName());
        const std::string architecture = std::string(Platform::GetArchitectureName());
        if (platform == "unknown" || architecture == "unknown")
        {
            Error(result, "Cannot initialize a native project for an unknown host target.");
            return result;
        }
        const std::string library = "libs/" + platform + "/" + architecture + "/" + options.name + LibraryExtension(platform);
        manifest["libraries"] = { { platform, { { architecture, library } } } };
        manifest["releaseTargets"] = json::array({ { { "platform", platform }, { "architecture", architecture } } });
        nativeSource = "#include \"Lode/Module.hpp\"\n"
            "#include \"Lode/State.hpp\"\n"
            "#include \"Lode/Table.hpp\"\n"
            "#include \"Lode/Value.hpp\"\n\n"
            "LODE_MODULE(vm)\n"
            "{\n"
            "    Lode::Table exports = vm.CreateTable();\n\n"
            "    exports.Set(\"add\", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>& args) -> Lode::Value {\n"
            "        const double left = args.size() > 0 && args[0].IsNumber() ? args[0].AsNumber() : 0.0;\n"
            "        const double right = args.size() > 1 && args[1].IsNumber() ? args[1].AsNumber() : 0.0;\n"
            "        return Lode::Value(left + right);\n"
            "    }));\n\n"
            "    exports.Set(\"identity\", vm.CreateFunction([](Lode::State&, const std::vector<Lode::Value>& args) -> Lode::Value {\n"
            "        return args.empty() ? Lode::Value() : args[0];\n"
            "    }));\n\n"
            "    return exports;\n"
            "}\n";
    }

    if (!WriteTextFile(root / "lode.json", manifest.dump(2) + "\n", result) ||
        !WriteTextFile(root / "init.luau", options.native
            ? "--!strict\n\nexport type NativeModule = {\n    add: (left: number, right: number) -> number,\n    identity: (value: unknown) -> unknown,\n}\n\nreturn {} :: NativeModule\n"
            : "--!strict\n\nreturn {}\n", result) ||
        !WriteTextFile(root / "LICENSE", LicenseText(options), result) ||
        !WriteTextFile(root / "README.md", "# " + options.name + "\n\n" + options.description + "\n", result))
        return result;

    if (options.native)
    {
        fs::create_directories(root / "src", ec);
        if (ec)
        {
            Error(result, "Cannot create native source directory: " + ec.message());
            return result;
        }
        const std::string cmake = "cmake_minimum_required(VERSION 3.20)\n"
            "project(" + options.name + " LANGUAGES CXX)\n\n"
            "find_package(Lode CONFIG REQUIRED)\n\n"
            "lode_add_native_module(" + options.name + "\n"
            "    SOURCES src/main.cpp\n"
            ")\n";
        WriteTextFile(root / "CMakeLists.txt", cmake, result);
        WriteTextFile(root / "src" / "main.cpp", nativeSource, result);
    }
    return result;
}
} // namespace Lode::Package
