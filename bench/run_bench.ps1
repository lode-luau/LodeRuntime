# run_bench.ps1 - LodeRuntime benchmark system (local + CI).
#
# Anyone can run their own measurements with this script; the baseline it
# compares against is machine-specific and recorded automatically:
#
#   - LOCAL (default): benchmark results are compared against the local
#     baseline (bench/results/local-baseline.json, gitignored). The first
#     run on a machine records that baseline automatically; later runs
#     print a delta table. Use -RecordBaseline to refresh it (e.g. after
#     a hardware change or an intentional code change).
#   - CI: the workflow compares against the committed, runner-specific
#     baseline (bench/results/ci-baseline.json) with -FailOnRegression, so
#     the public gate always compares CI vs CI.
#
# Steps:
# 1. SELF-TEST: verifies the whole bench infrastructure works end-to-end
#    (binaries exist, fixture generation is deterministic, every benchmark
#    exits 0 and emits its expected metrics). Any mechanical failure aborts.
# 2. MEASURE: runs lode_bench (C++), the Luau scenarios, and the warm-CLI
#    benchmark; every harness reports the arithmetic mean of 7-10 repeats.
# 3. COMPARE OR RECORD: if the baseline is missing (or -RecordBaseline is
#    given), the current run becomes the baseline; otherwise a delta table
#    is printed against it. Deltas are informational unless -FailOnRegression
#    is passed, because machines have different noise floors.
# 4. SAVE: writes the full result set as JSON for local history and as a
#    CI artifact.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File bench/run_bench.ps1
#     [-Config Release]        (Release only; Debug is rejected — timing is
#                               meaningless without optimizations)
#     [-Build]                  (cmake configure + build before benching)
#     [-Baseline path]          (default bench/results/local-baseline.json)
#     [-OutDir path]            (default bench/results/)
#     [-ThresholdPct n]         (default 5.0; regression warning threshold)
#     [-FailOnRegression]       (exit 1 when any metric regresses past threshold)
#     [-SummaryPath path]       append a Markdown results table to this file
#                               (e.g. $env:GITHUB_STEP_SUMMARY in CI)
#     [-RecordBaseline]         (overwrite the baseline with this run)

param(
    [string]$Config = "Release",
    [switch]$Build,
    [string]$Baseline = "",
    [string]$OutDir = "",
    [double]$ThresholdPct = 5.0,
    [switch]$FailOnRegression,
    [switch]$RecordBaseline,
    [string]$SummaryPath = ""
)

$ErrorActionPreference = "Stop"

# Timing numbers are meaningless without optimizations: Debug builds run
# 10-30x slower and would produce a misleading baseline. Bench only Release.
if ($Config -ne "Release") {
    throw "benchmarks are only meaningful in Release builds; use -Config Release (got '$Config')"
}

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Push-Location $Root
try {
    $Baseline = if ($Baseline -eq "") { Join-Path $Root "bench/results/local-baseline.json" } else { $Baseline }
    $OutDir = if ($OutDir -eq "") { Join-Path $Root "bench/results" } else { $OutDir }
    if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

    $Runtime = Join-Path $Root "build/bin/$Config/lode.exe"
    $LodeBench = Join-Path $Root "build/bin/$Config/lode_bench.exe"
    $CliWarm = Join-Path $Root "bench/luau/cli_warm.ps1"
    $FixtureGen = "bench/fixtures/gen_big.luau"
    $Fixture = "bench/fixtures/big.luau"

    if ($Build) {
        Write-Host "== configure + build ($Config) =="
        cmake -B build -DLODE_BUILD_BENCH=ON
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed (exit $LASTEXITCODE)" }
        cmake --build build --config $Config
        if ($LASTEXITCODE -ne 0) { throw "cmake build failed (exit $LASTEXITCODE)" }
    }

    foreach ($bin in @($Runtime, $LodeBench)) {
        if (-not (Test-Path $bin)) { throw "Missing binary: $bin - build first or pass -Build" }
    }

    $selftest = @{ }

    function Invoke-Checked([string]$Exe, [string]$What, [string[]]$Arguments) {
        $out = (& $Exe $Arguments 2>&1 | Out-String)
        $code = $LASTEXITCODE
        if ($code -ne 0) { throw "$What failed (exit $code):`n$out" }
        return $out
    }

    # --- Self-test 1: fixture generation is deterministic and runnable ---
    Write-Host "== self-test: fixture =="
    $g1 = Invoke-Checked $Runtime "fixture generation (run 1)" $FixtureGen
    $h1 = (Get-FileHash -Path $Fixture -Algorithm SHA256).Hash
    $g2 = Invoke-Checked $Runtime "fixture generation (run 2)" $FixtureGen
    $h2 = (Get-FileHash -Path $Fixture -Algorithm SHA256).Hash
    $selftest.fixture_deterministic = ($h1 -eq $h2)
    if (-not $selftest.fixture_deterministic) { throw "big.luau is not deterministic across runs" }
    $frun = Invoke-Checked $Runtime "fixture execution" $Fixture
    $selftest.fixture_runs = ($frun -match "fixture ok")
    if (-not $selftest.fixture_runs) { throw "big.luau did not print 'fixture ok'" }

    # --- Self-test 2: harnesses produce their expected output ---
    Write-Host "== self-test: harnesses =="
    $cppOut = Invoke-Checked $LodeBench "lode_bench" @()
    $selftest.lode_bench_ok = ($cppOut -match "=== done ===")
    if (-not $selftest.lode_bench_ok) { throw "lode_bench did not finish cleanly" }

    $luauOut = Invoke-Checked $Runtime "luau benchmark runner" "bench/luau/run.luau"
    $selftest.run_luau_ok = ($luauOut -match "=== done ===")
    if (-not $selftest.run_luau_ok) { throw "bench/luau/run.luau did not finish cleanly" }

    $cliOut = Invoke-Checked "powershell" "cli_warm.ps1" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $CliWarm)
    $selftest.cli_warm_ok = ($cliOut -match "cli_warm_ms: avg=\d+")
    if (-not $selftest.cli_warm_ok) { throw "cli_warm did not report an average" }

    # --- Measure ---
    Write-Host "== measuring =="
    $metrics = @{
        cpp        = @{ }
        luau       = @{ }
        cli_warm_ms = [double]$null
    }

    foreach ($line in ($cppOut -split "`r?`n")) {
        if ($line -match '^\s*([A-Za-z0-9_]+)\s+([0-9]+\.[0-9]+)\s+ns/op') {
            $metrics.cpp[$Matches[1]] = [double]$Matches[2]
        }
    }
    foreach ($line in ($luauOut -split "`r?`n")) {
        if ($line -match '^\s*([A-Za-z0-9_]+)\s+([0-9]+\.[0-9]+)\s+ns/op') {
            $metrics.luau[$Matches[1]] = [double]$Matches[2]
        }
    }
    foreach ($line in ($cliOut -split "`r?`n")) {
        if ($line -match '^cli_warm_ms: avg=(\d+)\s+min=(\d+)') {
            $metrics.cli_warm_ms = [double]$Matches[1]
        }
    }

    if ($metrics.cpp.Count -eq 0) { throw "no C++ metrics parsed from lode_bench output" }
    if ($metrics.luau.Count -eq 0) { throw "no Luau metrics parsed from run.luau output" }
    if ($null -eq $metrics.cli_warm_ms) { throw "no cli_warm median parsed" }

    # --- Result object (shared by record/compare/save) ---
    $commit = (git rev-parse --short HEAD 2>$null)
    $result = [pscustomobject]@{
        commit   = $commit
        config   = $Config
        machine  = $env:COMPUTERNAME
        date     = Get-Date -Format "yyyy-MM-ddTHH:mm:ssK"
        selftest = $selftest
        metrics  = $metrics
    }

    # --- Record or compare ---
    $rows = @()
    $regressed = @()

    if ($RecordBaseline -or -not (Test-Path $Baseline)) {
        [System.IO.File]::WriteAllText($Baseline, ($result | ConvertTo-Json -Depth 6))
        Write-Host "baseline recorded at $Baseline"
        Write-Host "(local baselines are machine-specific and gitignored; CI keeps its own at bench/results/ci-baseline.json)"
    } else {
        $base = Get-Content -Raw -Path $Baseline | ConvertFrom-Json

        $baseCpp = @{ }
        foreach ($p in $base.metrics.cpp.PSObject.Properties) { $baseCpp[$p.Name] = [double]$p.Value }
        $baseLuau = @{ }
        foreach ($p in $base.metrics.luau.PSObject.Properties) { $baseLuau[$p.Name] = [double]$p.Value }

        $sections = @(
            @{ Name = "cpp";  Parent = $metrics.cpp;  Base = $baseCpp },
            @{ Name = "luau"; Parent = $metrics.luau; Base = $baseLuau }
        )
        foreach ($s in $sections) {
            foreach ($key in $s.Parent.Keys) {
                $new = [double]$s.Parent[$key]
                $old = [double]$s.Base[$key]
                if ($null -ne $old -and $old -gt 0) {
                    $delta = (($new - $old) / $old) * 100.0
                    $flag = if ($delta -gt $ThresholdPct) { "REGRESSED" } else { "" }
                    if ($flag -ne "") { $regressed += "$($s.Name).$key" }
                    $rows += [pscustomobject]@{ metric = "$($s.Name).$key"; base = $old; new = $new; delta_pct = [math]::Round($delta, 2); flag = $flag }
                } else {
                    $rows += [pscustomobject]@{ metric = "$($s.Name).$key"; base = $null; new = $new; delta_pct = $null; flag = "NEW" }
                }
            }
        }
        $newCli = [double]$metrics.cli_warm_ms
        $oldCli = [double]$base.metrics.cli_warm_ms
        if ($null -ne $oldCli -and $oldCli -gt 0) {
            $delta = (($newCli - $oldCli) / $oldCli) * 100.0
            $flag = if ($delta -gt $ThresholdPct) { "REGRESSED" } else { "" }
            if ($flag -ne "") { $regressed += "cli_warm_ms" }
            $rows += [pscustomobject]@{ metric = "cli_warm_ms"; base = $oldCli; new = $newCli; delta_pct = [math]::Round($delta, 2); flag = $flag }
        } else {
            $rows += [pscustomobject]@{ metric = "cli_warm_ms"; base = $null; new = $newCli; delta_pct = $null; flag = "NEW" }
        }

        if ($rows.Count -eq 0 -and $SummaryPath -ne "") {
            # Record mode: emit current metrics so the summary is still useful.
            foreach ($p in $metrics.cpp.PSObject.Properties) {
                $rows += [pscustomobject]@{ metric = "cpp.$($p.Name)"; base = $null; new = [double]$p.Value; delta_pct = $null; flag = "NEW" }
            }
            foreach ($p in $metrics.luau.PSObject.Properties) {
                $rows += [pscustomobject]@{ metric = "luau.$($p.Name)"; base = $null; new = [double]$p.Value; delta_pct = $null; flag = "NEW" }
            }
            $rows += [pscustomobject]@{ metric = "cli_warm_ms"; base = $null; new = [double]$metrics.cli_warm_ms; delta_pct = $null; flag = "NEW" }
        }

        if ($rows.Count -gt 0) {
            Write-Host "== comparison vs baseline (threshold $ThresholdPct%) =="
            Write-Host ("{0,-26} {1,12} {2,12} {3,10} {4}" -f "metric", "baseline", "now", "delta%", "flag")
            foreach ($r in $rows) {
                $b = if ($null -eq $r.base) { "-" } else { ("{0:F2}" -f $r.base) }
                $d = if ($null -eq $r.delta_pct) { "-" } else { ("{0:+0.00;-0.00;0.00}" -f $r.delta_pct) }
                Write-Host ("{0,-26} {1,12} {2,12} {3,10} {4}" -f $r.metric, $b, ("{0:F2}" -f $r.new), $d, $r.flag)
            }
        }
    }

    # --- GitHub step summary (Markdown) ---
    if ($SummaryPath -ne "" -and $rows.Count -gt 0) {
        $md = New-Object System.Collections.Generic.List[string]
        $md.Add("### Benchmark results")
        $md.Add("")
        if ($commit) { $md.Add("Commit: $commit") }
        $md.Add("")
        $md.Add("| metric | baseline | now | delta% | flag |")
        $md.Add("|---|---:|---:|---:|---|")
        foreach ($r in $rows) {
            $b = if ($null -eq $r.base) { "-" } else { ("{0:F2}" -f $r.base) }
            $d = if ($null -eq $r.delta_pct) { "-" } else { ("{0:+0.00;-0.00;0.00}%" -f $r.delta_pct) }
            $flag = if ($r.flag -ne "") { "**$($r.flag)**" } else { "" }
            $md.Add("| $($r.metric) | $b | $(('{0:F2}' -f $r.new)) | $d | $flag |")
        }
        [System.IO.File]::AppendAllText($SummaryPath, (($md -join "`n") + "`n"))
        Write-Host "summary appended to $SummaryPath"
    }

    # --- Save ---
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $outPath = Join-Path $OutDir "bench-results-$stamp.json"
    [System.IO.File]::WriteAllText($outPath, ($result | ConvertTo-Json -Depth 6))
    Write-Host "results saved to $outPath"

    if ($FailOnRegression -and $regressed.Count -gt 0) {
        Write-Host "REGRESSED METRICS: $($regressed -join ', ')"
        exit 1
    }
    Write-Host "== bench OK =="
    exit 0
}
finally {
    Pop-Location
}
