# Packages the nightly runtime distribution, SDK, and stdlib modules.
# Invoked by the nightly workflow; requires the build trees produced by the
# Configure/Build steps (build-debug and build-release) at the repo root.
param(
    [Parameter(Mandatory = $true)]
    [string]$Version
)

# No Set-StrictMode here on purpose: manifests may omit optional keys such
# as dependencies, and the checks below rely on absent properties resolving
# to $null.
$ErrorActionPreference = 'Stop'

$ver = $Version
$distributionDist = "dist/lode"
$runtimeBinDist = "$distributionDist/bin"
$stdlibDist = "$distributionDist/stdlib"
$sdkDist = "dist/sdk"
$modulePackagesDist = "dist/stdlib-packages"
New-Item -ItemType Directory -Force -Path $runtimeBinDist, $stdlibDist, $sdkDist, $modulePackagesDist | Out-Null

# ---- SDK artifact: install both configurations into one prefix ----
# The SDK is consumed through find_package(Lode CONFIG REQUIRED).
# Installing both configurations keeps the package workflow aligned
# with the Debug/Release ABI contract.
cmake --install build-debug --config Debug --prefix $sdkDist --component LodeSDK
cmake --install build-release --config Release --prefix $sdkDist --component LodeSDK
Copy-Item LICENSE, README.md $sdkDist
Set-Content -Path "$sdkDist/VERSION" -Value $ver

# ---- Runtime distribution: executable and runtime DLLs ----
# The end-user archive contains the runtime and the bundled stdlib.
# SDK headers and CMake targets remain in the separate SDK archive.
Copy-Item build-release/bin/Release/lode.exe $runtimeBinDist
Copy-Item build-release/bin/Release/LodeCore.dll $runtimeBinDist
Copy-Item build-release/bin/Release/uv.dll $runtimeBinDist
Copy-Item LICENSE, README.md $distributionDist
Set-Content -Path "$distributionDist/VERSION" -Value $ver

# ---- Standard library: recursive module discovery ----
# package.luau is intentionally static data (return { ... }), so this
# script extracts only the package-manager fields it needs without running
# arbitrary package code during a release build.
function Read-LuauStringField {
    param(
        [Parameter(Mandatory = $true)] [string]$Source,
        [Parameter(Mandatory = $true)] [string]$Field,
        [Parameter(Mandatory = $true)] [string]$Path
    )

    $fieldPattern = '(?m)^\s*(?:' + [regex]::Escape($Field) + '|\["' + [regex]::Escape($Field) + '"\])\s*=\s*"((?:\\.|[^"])*)"'
    $match = [regex]::Match($Source, $fieldPattern)
    if (-not $match.Success) {
        throw "Package manifest is missing string field '$Field': $Path"
    }
    return ConvertFrom-Json ('"' + $match.Groups[1].Value + '"')
}

function Read-LuauPackageManifest {
    param(
        [Parameter(Mandatory = $true)] [string]$Path
    )

    $source = Get-Content -Raw -LiteralPath $Path
    $dependencies = @{}
    $dependenciesMatch = [regex]::Match($source, '(?ms)^\s*dependencies\s*=\s*\{(.*?)^\s*\}')
    if ($dependenciesMatch.Success) {
        $dependencyPattern = '(?m)^\s*(?:([A-Za-z_][A-Za-z0-9_]*)|\["([^"]+)"\])\s*=\s*"((?:\\.|[^"])*)"'
        foreach ($match in [regex]::Matches($dependenciesMatch.Groups[1].Value, $dependencyPattern)) {
            $dependencyName = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            $dependencies[$dependencyName] = ConvertFrom-Json ('"' + $match.Groups[3].Value + '"')
        }
    }

    return [pscustomobject]@{
        name = Read-LuauStringField -Source $source -Field "name" -Path $Path
        version = Read-LuauStringField -Source $source -Field "version" -Path $Path
        dependencies = $dependencies
    }
}

$moduleDirs = Get-ChildItem modules -Directory -Recurse | Where-Object { Test-Path (Join-Path $_.FullName "package.luau") }
$modulesRoot = (Resolve-Path modules).Path
$moduleRecords = @{}

foreach ($mdir in $moduleDirs) {
    $manifestPath = Join-Path $mdir.FullName "package.luau"
    $manifest = Read-LuauPackageManifest -Path $manifestPath
    $name = [string]$manifest.name
    $version = [string]$manifest.version
    $rel = $mdir.FullName.Substring($modulesRoot.Length + 1).Replace('\', '/')

    if ([string]::IsNullOrWhiteSpace($name) -or [string]::IsNullOrWhiteSpace($version)) {
        throw "Standard module manifest is missing name or version: $manifestPath"
    }
    if ($moduleRecords.ContainsKey($name)) {
        throw "Duplicate standard module name '$name' in $manifestPath"
    }
    if ($version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') {
        throw "Standard module '$name' has an invalid SemVer: $version"
    }

    $moduleRecords[$name] = [pscustomobject]@{
        Name = $name
        Version = $version
        RelativePath = $rel
        SourcePath = $mdir.FullName
        Manifest = $manifest
    }
}

# Standard-module dependencies are resolved against this same
# nightly bundle. They use exact versions so a bundle cannot silently
# ship an incompatible transitive module.
foreach ($record in $moduleRecords.Values) {
    $dependencies = $record.Manifest.dependencies
    if ($null -eq $dependencies) { continue }

    foreach ($dependency in $dependencies.PSObject.Properties) {
        if ($dependency.Value -isnot [string]) {
            throw "Standard module '$($record.Name)' must use an exact version string for dependency '$($dependency.Name)'."
        }
        $requestedVersion = [string]$dependency.Value
        if (-not $moduleRecords.ContainsKey($dependency.Name)) {
            throw "Standard module '$($record.Name)' depends on missing standard module '$($dependency.Name)'."
        }
        $availableVersion = $moduleRecords[$dependency.Name].Version
        if ($requestedVersion -ne $availableVersion) {
            throw "Standard module '$($record.Name)' requires $($record.Name)@$requestedVersion, but the nightly bundle contains $availableVersion."
        }
    }
}

# Map of module relative paths (forward slashes) so a parent module's
# copy skips its nested module directories (handled separately).
$moduleRelSet = @{}
foreach ($mdir in $moduleDirs) {
    $rel = $mdir.FullName.Substring($modulesRoot.Length + 1).Replace('\', '/')
    $moduleRelSet[$rel] = $true
}

foreach ($record in ($moduleRecords.Values | Sort-Object RelativePath)) {
    $rel = $record.RelativePath
    $name = $record.Name
    $src = "modules/$rel"
    $dst = Join-Path $stdlibDist "modules/$rel"
    New-Item -ItemType Directory -Force -Path $dst | Out-Null

    $isNative = Test-Path "$src/CMakeLists.txt"
    Get-ChildItem $src | ForEach-Object {
        $childRel = "$rel/$($_.Name)"
        $isNestedModule = $_.PSIsContainer -and $moduleRelSet.ContainsKey($childRel)
        # Exclude build-time files and the libs/ tree (rebuilt below
        # with only the shipped platform, windows/x64). src/ is
        # excluded only for native modules (those with a
        # CMakeLists.txt); pure-Luau modules keep their src/.
        $isExcluded = $_.Name -eq "CMakeLists.txt" -or $_.Name -eq "libs" -or ($_.Name -eq "src" -and $isNative)
        if (-not $isNestedModule -and -not $isExcluded) {
            Copy-Item $_.FullName (Join-Path $dst $_.Name) -Recurse
        }
    }

    # Native modules keep their config-matched DLL in the module's
    # own libs/<platform>/<arch>/<config> entry from package.luau; the
    # loader resolves the config-aware subdir per runtime build.
    if ($isNative) {
        $libRelease = "$src/libs/windows/x64/Release/$name.dll"
        $libFlat = "$src/libs/windows/x64/$name.dll"
        $libBin = "build-release/bin/Release/$name.dll"

        $sourceDll = $null
        if (Test-Path $libRelease) {
            $sourceDll = $libRelease
        } elseif (Test-Path $libFlat) {
            $sourceDll = $libFlat
        } elseif (Test-Path $libBin) {
            $sourceDll = $libBin
        }

        if ($sourceDll) {
            $libDirRelease = "$dst/libs/windows/x64/Release"
            New-Item -ItemType Directory -Force -Path $libDirRelease | Out-Null
            Copy-Item $sourceDll "$libDirRelease/$name.dll" -Force
        } else {
            Write-Error "Native library for $name was not found at $libRelease, $libFlat, or $libBin"
        }
    }
    $packageDst = Join-Path $modulePackagesDist $name
    New-Item -ItemType Directory -Force -Path $packageDst | Out-Null
    Copy-Item (Join-Path $dst '*') $packageDst -Recurse -Force
    if (-not (Test-Path (Join-Path $packageDst "LICENSE"))) {
        Copy-Item LICENSE (Join-Path $packageDst "LICENSE")
    }

    $moduleArchive = "lode-stdlib-$name-$($record.Version)-windows-x64.zip"
    Compress-Archive -Path "$packageDst/*" -DestinationPath $moduleArchive -Force
    $moduleHash = (Get-FileHash -LiteralPath $moduleArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$moduleHash  $moduleArchive" | Set-Content -Path "$moduleArchive.sha256" -Encoding ascii
}

# Alias config so require("@name") resolves for every shipped module.
$aliasLines = foreach ($record in ($moduleRecords.Values | Sort-Object RelativePath)) {
    $rel = $record.RelativePath
    $name = $record.Name
    $aliasLine = ('            ' + $name + ' = "modules/' + $rel + '",')
    $aliasLine
}
$config = @"
return {
    luau = {
        aliases = {
$($aliasLines -join "`n")
        }
    }
}
"@
Set-Content -Path "$stdlibDist/.config.luau" -Value $config
Set-Content -Path "$stdlibDist/VERSION" -Value $ver

# Package CI runs the SDK's lode executable for dependency
# installation and validation. Keep the exact bundled stdlib catalog
# beside that executable so standard-module dependencies resolve in
# the SDK archive without requiring a second runtime download.
$sdkStdlibDist = Join-Path $sdkDist "stdlib"
New-Item -ItemType Directory -Force -Path $sdkStdlibDist | Out-Null
Copy-Item "$stdlibDist/*" $sdkStdlibDist -Recurse -Force

Compress-Archive -Path "$distributionDist/*" -DestinationPath "lode-windows-x64-$ver.zip"
Compress-Archive -Path "$sdkDist/*" -DestinationPath "lode-sdk-windows-x64-$ver.zip"

foreach ($archive in @(
    "lode-windows-x64-$ver.zip",
    "lode-sdk-windows-x64-$ver.zip"
)) {
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $archive" | Set-Content -Path "$archive.sha256" -Encoding ascii
}
