# Packages the nightly Lode runtime, development distribution, and stdlib bundle.
# Invoked by the nightly workflow; requires the build trees produced by the
# Configure/Build steps (build-debug and build-release) at the repo root.
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$DebugBuildDirectory = "build-debug",
    [string]$ReleaseBuildDirectory = "build-release",
    [string]$OutputDirectory = ".",
    [ValidateSet("windows", "linux", "macos")]
    [string]$Platform = "windows",
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64"
)

# No Set-StrictMode here on purpose: manifests may omit optional keys such
# as dependencies, and the checks below rely on absent properties resolving
# to $null.
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$debugBuildRoot = [System.IO.Path]::GetFullPath($DebugBuildDirectory)
$releaseBuildRoot = [System.IO.Path]::GetFullPath($ReleaseBuildDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$configuration = "Release"
$moduleExtension = switch ($Platform) {
    "windows" { ".dll" }
    "macos" { ".dylib" }
    default { ".so" }
}
$assetTarget = "$Platform-$Architecture"
$releaseBinRoot = Join-Path $releaseBuildRoot "bin/$configuration"

function Copy-LodeDirectoryContents {
    param(
        [Parameter(Mandatory = $true)] [string]$Source,
        [Parameter(Mandatory = $true)] [string]$Destination
    )

    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

function Compress-LodeDirectory {
    param(
        [Parameter(Mandatory = $true)] [string]$Source,
        [Parameter(Mandatory = $true)] [string]$Destination
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Force
    }
    $archive = [System.IO.Compression.ZipFile]::Open(
        $Destination, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        $directorySeparator = [System.IO.Path]::DirectorySeparatorChar
        $alternateSeparator = [System.IO.Path]::AltDirectorySeparatorChar
        $sourceRoot = (Resolve-Path -LiteralPath $Source).Path.TrimEnd($directorySeparator, $alternateSeparator) + $directorySeparator
        Get-ChildItem -LiteralPath $Source -Force -Recurse -File | ForEach-Object {
            $entryName = $_.FullName.Substring($sourceRoot.Length).Replace('\', '/')
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $_.FullName, $entryName,
                [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally {
        $archive.Dispose()
    }
}

$ver = $Version
$distributionDist = Join-Path $outputRoot "lode"
$runtimeBinDist = "$distributionDist/bin"
$stdlibDist = "$distributionDist/stdlib"
$developmentDist = Join-Path $outputRoot "lode-development"
New-Item -ItemType Directory -Force -Path $runtimeBinDist, $stdlibDist, $developmentDist | Out-Null

# ---- Lode development artifact: install both configurations into one prefix ----
# The development distribution is consumed through find_package(Lode CONFIG REQUIRED).
# Installing both configurations keeps the package workflow aligned
# with the Debug/Release ABI contract.
cmake --install $debugBuildRoot --config Debug --prefix $developmentDist --component Lode
cmake --install $releaseBuildRoot --config Release --prefix $developmentDist --component Lode
Copy-Item LICENSE, README.md $developmentDist
Set-Content -Path "$developmentDist/VERSION" -Value $ver
$lodeMetadataPath = Join-Path $developmentDist "share/lode/lode-install.json"
if (-not (Test-Path -LiteralPath $lodeMetadataPath)) {
    throw "Installed Lode metadata was not found: $lodeMetadataPath"
}
$lodeMetadata = Get-Content -Raw -LiteralPath $lodeMetadataPath
$lodeMetadataPattern = '"version"\s*:\s*"[^"]*"'
if (-not [regex]::IsMatch($lodeMetadata, $lodeMetadataPattern)) {
    throw "Installed Lode metadata does not contain version: $lodeMetadataPath"
}
$lodeMetadata = [regex]::new($lodeMetadataPattern).Replace(
    $lodeMetadata, ('"version": "' + $ver + '"'), 1)
Set-Content -Path $lodeMetadataPath -Value $lodeMetadata -NoNewline

# ---- Runtime distribution: executable and runtime libraries ----
# The end-user archive contains the runtime and the bundled stdlib.
# Development headers and CMake targets remain in the optional development archive.
$runtimeExecutable = if ($Platform -eq "windows") { "lode.exe" } else { "lode" }
Copy-Item (Join-Path $releaseBinRoot $runtimeExecutable) $runtimeBinDist
$runtimeLibraryPattern = if ($Platform -eq "windows") {
    @("LodeCore.dll", "uv.dll")
} elseif ($Platform -eq "macos") {
    @("libLodeCore.dylib", "libuv.dylib")
} else {
    @("libLodeCore.so*", "libuv.so*")
}
foreach ($pattern in $runtimeLibraryPattern) {
    $runtimeLibrary = Get-ChildItem -LiteralPath $releaseBinRoot -File -Filter $pattern | Select-Object -First 1
    if (-not $runtimeLibrary) { throw "Runtime library matching '$pattern' was not found in $releaseBinRoot" }
    Copy-Item -LiteralPath $runtimeLibrary.FullName -Destination $runtimeBinDist
}
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

function Read-LuauOptionalStringField {
    param(
        [Parameter(Mandatory = $true)] [string]$Source,
        [Parameter(Mandatory = $true)] [string]$Field
    )

    $fieldPattern = '(?m)^\s*(?:' + [regex]::Escape($Field) + '|\["' + [regex]::Escape($Field) + '"\])\s*=\s*"((?:\\.|[^"])*)"'
    $match = [regex]::Match($Source, $fieldPattern)
    if (-not $match.Success) { return $null }
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
        implementation = if ($null -ne (Read-LuauOptionalStringField -Source $source -Field "artifact") -or
            $null -ne (Read-LuauOptionalStringField -Source $source -Field "layout")) {
            [pscustomobject]@{
                artifact = Read-LuauOptionalStringField -Source $source -Field "artifact"
                layout = Read-LuauOptionalStringField -Source $source -Field "layout"
            }
        } else { $null }
    }
}

function Expand-ModuleArtifactPath {
    param(
        [Parameter(Mandatory = $true)] [object]$Manifest,
        [Parameter(Mandatory = $true)] [string]$Platform,
        [Parameter(Mandatory = $true)] [string]$Architecture,
        [Parameter(Mandatory = $true)] [string]$Configuration
    )

    $artifact = if ($Manifest.implementation.artifact) { [string]$Manifest.implementation.artifact } else { [string]$Manifest.name }
    $layout = if ($Manifest.implementation.layout) {
        [string]$Manifest.implementation.layout
    } else {
        "libs/{platform}/{architecture}/{configuration}/{artifact}{extension}"
    }
    $expanded = $layout.Replace("{platform}", $Platform).Replace("{architecture}", $Architecture).
        Replace("{configuration}", $Configuration).Replace("{artifact}", $artifact).
        Replace("{extension}", $moduleExtension)
    if ([System.IO.Path]::IsPathRooted($expanded) -or $expanded -match '(^|[\\/])\.\.([\\/]|$)') {
        throw "Module implementation layout escapes the package: $expanded"
    }
    return $expanded.Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

$moduleDirs = Get-ChildItem modules -Directory -Recurse | Where-Object { Test-Path (Join-Path $_.FullName "package.luau") }
$modulesRoot = (Resolve-Path modules).Path
$moduleRecords = @{}
$stdlibIndex = [ordered]@{
    format = 1
    release = $ver
    packages = [ordered]@{}
}

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
    $stdlibIndex.packages[$name] = [ordered]@{
        version = $version
        path = "modules/$rel"
    }
    if ($null -ne $manifest.implementation) {
        $stdlibIndex.packages[$name].artifact = if ($manifest.implementation.artifact) {
            [string]$manifest.implementation.artifact
        } else { $name }
        $stdlibIndex.packages[$name].targets = @($assetTarget)
    }
}

# Standard-module dependencies are resolved against this same
# nightly bundle. They use exact versions so a bundle cannot silently
# ship an incompatible transitive module.
foreach ($record in $moduleRecords.Values) {
    $dependencies = $record.Manifest.dependencies
    if ($null -eq $dependencies) { continue }

    foreach ($dependency in $dependencies.GetEnumerator()) {
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

    $isCompiledModule = Test-Path "$src/CMakeLists.txt"
    Get-ChildItem $src | ForEach-Object {
        $childRel = "$rel/$($_.Name)"
        $isNestedModule = $_.PSIsContainer -and $moduleRelSet.ContainsKey($childRel)
        # Exclude build-time files and the libs/ tree (rebuilt below
        # with only the shipped platform/architecture). src/ is
        # excluded only for compiled modules (those with a
        # CMakeLists.txt); pure-Luau modules keep their src/.
        $isExcluded = $_.Name -eq "CMakeLists.txt" -or $_.Name -eq "libs" -or ($_.Name -eq "src" -and $isCompiledModule)
        if (-not $isNestedModule -and -not $isExcluded) {
            Copy-Item $_.FullName (Join-Path $dst $_.Name) -Recurse
        }
    }

    # Compiled modules keep their config-matched shared library in the module's
    # own libs/<platform>/<arch>/<config> entry from package.luau; the
    # loader resolves the config-aware subdir per runtime build.
    if ($isCompiledModule) {
        $relativeArtifact = Expand-ModuleArtifactPath -Manifest $record.Manifest -Platform $Platform -Architecture $Architecture -Configuration $configuration
        $libRelease = Join-Path $src $relativeArtifact
        $artifactName = Split-Path $relativeArtifact -Leaf
        $libFlat = Join-Path $src (Join-Path "libs/$Platform/$Architecture" $artifactName)
        $libBin = Join-Path $releaseBinRoot $artifactName

        $sourceDll = $null
        if (Test-Path $libRelease) {
            $sourceDll = $libRelease
        } elseif (Test-Path $libFlat) {
            $sourceDll = $libFlat
        } elseif (Test-Path $libBin) {
            $sourceDll = $libBin
        }

        if ($sourceDll) {
            $libDirRelease = Split-Path (Join-Path $dst $relativeArtifact) -Parent
            New-Item -ItemType Directory -Force -Path $libDirRelease | Out-Null
            Copy-Item $sourceDll (Join-Path $libDirRelease $artifactName) -Force

            # OpenSSL is a runtime dependency of the POSIX TLS/crypto modules.
            # Keep it beside each module so the packaged artifact remains
            # relocatable and can be loaded without a system OpenSSL install.
            if ($Platform -ne "windows" -and $name -in @("crypto", "http", "tcp", "websocket")) {
                $triplet = if ($Platform -eq "linux") { "x64-linux" } elseif ($Architecture -eq "arm64") { "arm64-osx" } else { "x64-osx" }
                $vcpkgRoot = Join-Path $releaseBuildRoot "vcpkg_installed/$triplet"
                $opensslRoots = @((Join-Path $vcpkgRoot "lib"), (Join-Path $vcpkgRoot "bin"))
                $opensslFiles = foreach ($opensslRoot in $opensslRoots) {
                    if (Test-Path -LiteralPath $opensslRoot) {
                        Get-ChildItem -LiteralPath $opensslRoot -File | Where-Object {
                            if ($Platform -eq "linux") {
                                $_.Name -match '^lib(crypto|ssl)\.so(\.|$)'
                            } else {
                                $_.Name -match '^lib(crypto|ssl)(\.\d+)?\.dylib$'
                            }
                        }
                    }
                }
                foreach ($opensslFile in ($opensslFiles | Sort-Object FullName -Unique)) {
                    Copy-Item -LiteralPath $opensslFile.FullName -Destination $libDirRelease -Force
                }
            }
        } else {
            Write-Error "Module library for $name was not found at $libRelease, $libFlat, or $libBin"
        }
    }
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
($stdlibIndex | ConvertTo-Json -Depth 10) | Set-Content -Path "$stdlibDist/index.json" -Encoding utf8

# Package CI runs the Lode development distribution's lode executable for dependency
# installation and validation. Keep the exact bundled stdlib catalog
# beside that executable so standard-module dependencies resolve in the
# development archive without requiring a second runtime download.
$developmentStdlibDist = Join-Path $developmentDist "stdlib"
New-Item -ItemType Directory -Force -Path $developmentStdlibDist | Out-Null
Copy-LodeDirectoryContents -Source $stdlibDist -Destination $developmentStdlibDist

Compress-LodeDirectory -Source $distributionDist -Destination (Join-Path $outputRoot "lode-$assetTarget-$ver.zip")
Compress-LodeDirectory -Source $developmentDist -Destination (Join-Path $outputRoot "lode-development-$assetTarget-$ver.zip")
Compress-LodeDirectory -Source $stdlibDist -Destination (Join-Path $outputRoot "lode-stdlib-$assetTarget-$ver.zip")

$checksums = @()
foreach ($archive in @(
    (Join-Path $outputRoot "lode-$assetTarget-$ver.zip"),
    (Join-Path $outputRoot "lode-development-$assetTarget-$ver.zip"),
    (Join-Path $outputRoot "lode-stdlib-$assetTarget-$ver.zip")
)) {
    $hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($null -eq $checksums) { $checksums = @() }
    $checksums += "$hash  $([System.IO.Path]::GetFileName($archive))"
}
$checksums | Set-Content -Path (Join-Path $outputRoot "SHA256SUMS") -Encoding ascii
