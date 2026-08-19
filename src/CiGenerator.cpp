// Copyright (c) 2026 yanlvl99, Lode Runtime Contributors
// SPDX-License-Identifier: MIT
#include "CiGenerator.hpp"

#include "PathUtil.hpp"
#include "nlohmann/json.hpp"

#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Lode::Package
{

namespace
{

using json = nlohmann::json;
using Lode::Detail::PathToUtf8;

constexpr const char* CheckoutAction = "actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683";
constexpr const char* UploadArtifactAction = "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02";
constexpr const char* DownloadArtifactAction = "actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093";

void Error(ValidationReport& report, std::string message)
{
    report.errors.push_back(std::move(message));
}

std::string SdkDownloadSteps()
{
    return R"LODE(
      - name: Download pinned Lode SDK
        shell: pwsh
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          $ErrorActionPreference = "Stop"
          if ($env:LODE_SDK_VERSION.Contains("<") -or $env:LODE_SDK_SHA256 -notmatch "^[0-9a-fA-F]{64}$") {
              throw "Set LODE_SDK_VERSION and the 64-character LODE_SDK_SHA256 in the generated workflow."
          }
          $downloadRoot = Join-Path $env:RUNNER_TEMP "lode-sdk-download"
          $sdkRoot = Join-Path $env:RUNNER_TEMP "lode-sdk"
          New-Item -ItemType Directory -Force $downloadRoot, $sdkRoot | Out-Null
          $asset = "lode-sdk-windows-x64-$env:LODE_SDK_VERSION.zip"
          gh release download $env:LODE_SDK_VERSION --repo $env:LODE_REPOSITORY --pattern $asset --pattern "$asset.sha256" --dir $downloadRoot
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          $archive = Join-Path $downloadRoot $asset
          $checksumFile = "$archive.sha256"
          $expected = (Get-Content $checksumFile -Raw).Trim().Split(" ")[0].ToLowerInvariant()
          $actual = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
          if ($expected -ne $actual -or $env:LODE_SDK_SHA256.ToLowerInvariant() -ne $actual) {
              throw "Lode SDK SHA-256 verification failed."
          }
          Expand-Archive -LiteralPath $archive -DestinationPath $sdkRoot -Force
          "LODE_SDK_ROOT=$sdkRoot" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
)LODE";
}

std::string NativeWorkflow()
{
    std::ostringstream workflow;
    workflow << R"LODE(name: Lode CI

on:
  pull_request:
    branches: [main]
  push:
    branches: [main]
    tags: ["v*.*.*"]
  workflow_dispatch:

permissions:
  contents: read

# BEGIN LODE MANAGED: v1
env:
  LODE_REPOSITORY: lode-luau/LodeRuntime
  # Pin these values to an existing Lode nightly release before enabling CI.
  LODE_SDK_VERSION: "<nightly-version>"
  LODE_SDK_SHA256: "<sdk-sha256>"

jobs:
  native-windows-x64:
    runs-on: windows-2022
    steps:
      - uses: )LODE" << CheckoutAction << R"LODE(

)LODE";
    workflow << SdkDownloadSteps();
    workflow << R"LODE(
      - name: Validate package source
        shell: pwsh
        run: |
          & "$env:LODE_SDK_ROOT/bin/Release/lode.exe" ci validate --source .
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

      # Add package-specific dependency provisioning here. OpenSSL and other
      # native dependencies remain CMake/CI responsibilities.
      - name: Configure Debug
        shell: pwsh
        run: |
          cmake -S . -B build-lode-debug -G "Visual Studio 17 2022" -A x64 `
            -DCMAKE_PREFIX_PATH="$env:LODE_SDK_ROOT" `
            -DLODE_RUNTIME="$env:LODE_SDK_ROOT/bin/Debug/lode.exe"
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

      - name: Build Debug
        run: cmake --build build-lode-debug --config Debug --parallel 2

      - name: Test Debug
        run: ctest --test-dir build-lode-debug -C Debug --output-on-failure

      - name: Configure Release
        shell: pwsh
        run: |
          cmake -S . -B build-lode-release -G "Visual Studio 17 2022" -A x64 `
            -DCMAKE_PREFIX_PATH="$env:LODE_SDK_ROOT" `
            -DLODE_RUNTIME="$env:LODE_SDK_ROOT/bin/Release/lode.exe"
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

      - name: Build Release
        run: cmake --build build-lode-release --config Release --parallel 2

      - name: Test Release
        run: ctest --test-dir build-lode-release -C Release --output-on-failure

      - name: Validate package artifact
        shell: pwsh
        run: |
          & "$env:LODE_SDK_ROOT/bin/Release/lode.exe" ci validate --artifact .
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

      - name: Package validated artifact
        shell: pwsh
        run: |
          if (-not (Test-Path -LiteralPath "ci/package.ps1")) {
              throw "Native package CI requires ci/package.ps1."
          }
          $manifest = Get-Content lode.json -Raw | ConvertFrom-Json
          $archive = "out/lode-$($manifest.name)-$($manifest.version)-windows-x64.zip"
          New-Item -ItemType Directory -Force out | Out-Null
          & ./ci/package.ps1 -Runtime "$env:LODE_SDK_ROOT/bin/Release/lode.exe" -ArchivePath $archive
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          "PACKAGE_ARCHIVE=$archive" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8

      - name: Verify release tag
        if: startsWith(github.ref, 'refs/tags/v')
        shell: pwsh
        run: |
          $manifest = Get-Content lode.json -Raw | ConvertFrom-Json
          $expected = "v$($manifest.version)"
          if ("$env:GITHUB_REF_NAME" -ne $expected) {
              throw "Release tag $env:GITHUB_REF_NAME does not match $expected."
          }

      - name: Upload package artifact
        uses: )LODE" << UploadArtifactAction << R"LODE(
        with:
          name: lode-package-windows-x64
          path: out/*
          if-no-files-found: error

  release:
    if: startsWith(github.ref, 'refs/tags/v')
    needs: native-windows-x64
    runs-on: windows-2022
    permissions:
      contents: write
    steps:
      - name: Download package artifact
        uses: )LODE" << DownloadArtifactAction << R"LODE(
        with:
          name: lode-package-windows-x64
          path: out

      - name: Publish GitHub release
        shell: pwsh
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          $archive = Get-ChildItem out -Filter "*.zip" | Select-Object -First 1
          if ($null -eq $archive) { throw "Package archive was not found." }
          $checksum = "$($archive.FullName).sha256"
          if (-not (Test-Path -LiteralPath $checksum)) { throw "Package checksum was not found." }
          gh release create $env:GITHUB_REF_NAME $archive.FullName $checksum `
            --verify-tag --title "Package $env:GITHUB_REF_NAME" `
            --notes "Published by the generated Lode CI workflow."

# END LODE MANAGED
)LODE";
    return workflow.str();
}

std::string PureLuauWorkflow()
{
    std::ostringstream workflow;
    workflow << R"LODE(name: Lode CI

on:
  pull_request:
    branches: [main]
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read

# BEGIN LODE MANAGED: v1
env:
  LODE_REPOSITORY: lode-luau/LodeRuntime
  # Pin these values to an existing Lode nightly release before enabling CI.
  LODE_SDK_VERSION: "<nightly-version>"
  LODE_SDK_SHA256: "<sdk-sha256>"

jobs:
  pure-luau:
    runs-on: windows-2022
    steps:
      - uses: )LODE" << CheckoutAction << R"LODE(
)LODE";
    workflow << SdkDownloadSteps();
    workflow << R"LODE(
      - name: Validate package source
        shell: pwsh
        run: |
          & "$env:LODE_SDK_ROOT/bin/Release/lode.exe" ci validate --source .
          if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

      - name: Run package tests
        shell: pwsh
        run: |
          if (Test-Path -LiteralPath "tests/run.luau") {
              & "$env:LODE_SDK_ROOT/bin/Release/lode.exe" tests/run.luau
              if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
          } else {
              Write-Host "No tests/run.luau found; package test step was skipped."
          }

# END LODE MANAGED
)LODE";
    return workflow.str();
}

bool BuildWorkflowText(const fs::path& root, ValidationReport& report, std::string& workflow)
{
    ValidationReport sourceReport = Validate(root, ValidationMode::Source);
    report.errors.insert(report.errors.end(), sourceReport.errors.begin(), sourceReport.errors.end());
    report.warnings.insert(report.warnings.end(), sourceReport.warnings.begin(), sourceReport.warnings.end());
    if (!sourceReport.IsValid())
        return false;

    json manifest;
    try
    {
        std::ifstream file(root / "lode.json");
        manifest = json::parse(file);
    }
    catch (const std::exception& exception)
    {
        Error(report, "Failed to parse lode.json while generating CI: " + std::string(exception.what()));
        return false;
    }

    const bool isNative = manifest.contains("libraries") && manifest["libraries"].is_object();
    if (isNative)
    {
        for (auto platformIt = manifest["libraries"].begin(); platformIt != manifest["libraries"].end(); ++platformIt)
        {
            if (platformIt.key() != "windows" || !platformIt.value().is_object())
            {
                Error(report, "lode ci currently supports only native windows/x64 package targets.");
                return false;
            }

            for (auto architectureIt = platformIt.value().begin(); architectureIt != platformIt.value().end(); ++architectureIt)
            {
                if (architectureIt.key() != "x64")
                {
                    Error(report, "lode ci currently supports only native windows/x64 package targets.");
                    return false;
                }
            }
        }
    }

    workflow = isNative ? NativeWorkflow() : PureLuauWorkflow();
    return true;
}

bool ReplaceManagedWorkflowBlock(const fs::path& workflowPath, const std::string& generated, ValidationReport& report)
{
    constexpr const char* beginMarker = "# BEGIN LODE MANAGED: v1";
    constexpr const char* endMarker = "# END LODE MANAGED";

    std::ifstream input(workflowPath, std::ios::binary);
    if (!input.is_open())
    {
        Error(report, "Failed to read workflow: " + PathToUtf8(workflowPath));
        return false;
    }

    const std::string existing((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::size_t begin = existing.find(beginMarker);
    const std::size_t end = existing.find(endMarker);
    if (begin == std::string::npos || end == std::string::npos || end < begin)
    {
        Error(report, "Workflow does not contain a valid Lode managed block: " + PathToUtf8(workflowPath));
        return false;
    }

    const std::size_t generatedBegin = generated.find(beginMarker);
    const std::size_t generatedEnd = generated.find(endMarker);
    if (generatedBegin == std::string::npos || generatedEnd == std::string::npos || generatedEnd < generatedBegin)
    {
        Error(report, "Internal error: generated workflow does not contain a valid Lode managed block.");
        return false;
    }

    const std::size_t managedEnd = end + std::char_traits<char>::length(endMarker);
    const std::string managed = generated.substr(generatedBegin, generatedEnd + std::char_traits<char>::length(endMarker) - generatedBegin);
    std::string updated = existing.substr(0, begin) + managed + existing.substr(managedEnd);

    std::ofstream output(workflowPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        Error(report, "Failed to write workflow: " + PathToUtf8(workflowPath));
        return false;
    }

    output << updated;
    if (!output.good())
    {
        Error(report, "Failed while writing workflow: " + PathToUtf8(workflowPath));
        return false;
    }

    return true;
}

} // namespace

ValidationReport GenerateWorkflow(const fs::path& packageRoot, bool force)
{
    ValidationReport report;
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(packageRoot, ec), ec);
    if (ec || !fs::is_directory(root))
    {
        Error(report, "Package root is not a directory: " + PathToUtf8(packageRoot));
        return report;
    }

    const fs::path workflowPath = root / ".github" / "workflows" / "lode.yml";
    if (fs::exists(workflowPath) && !force)
    {
        Error(report, "Refusing to overwrite existing workflow: " + PathToUtf8(workflowPath) + ". Use --force explicitly.");
        return report;
    }

    std::string workflow;
    if (!BuildWorkflowText(root, report, workflow))
        return report;

    std::error_code createError;
    fs::create_directories(workflowPath.parent_path(), createError);
    if (createError)
    {
        Error(report, "Failed to create workflow directory: " + createError.message());
        return report;
    }

    std::ofstream output(workflowPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        Error(report, "Failed to write workflow: " + PathToUtf8(workflowPath));
        return report;
    }

    output << workflow;
    if (!output.good())
        Error(report, "Failed while writing workflow: " + PathToUtf8(workflowPath));

    return report;
}

ValidationReport UpdateWorkflow(const fs::path& packageRoot)
{
    ValidationReport report;
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(packageRoot, ec), ec);
    if (ec || !fs::is_directory(root))
    {
        Error(report, "Package root is not a directory: " + PathToUtf8(packageRoot));
        return report;
    }

    const fs::path workflowPath = root / ".github" / "workflows" / "lode.yml";
    if (!fs::is_regular_file(workflowPath))
    {
        Error(report, "Managed workflow does not exist: " + PathToUtf8(workflowPath) + ". Run `lode ci init` first.");
        return report;
    }

    std::string generated;
    if (!BuildWorkflowText(root, report, generated))
        return report;

    ReplaceManagedWorkflowBlock(workflowPath, generated, report);
    return report;
}

} // namespace Lode::Package
