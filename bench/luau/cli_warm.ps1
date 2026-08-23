# Measures the warm-run wall time of lode on a cached .luau fixture.
# Usage:
#   powershell -ExecutionPolicy Bypass -File bench/luau/cli_warm.ps1
#   powershell -ExecutionPolicy Bypass -File bench/luau/cli_warm.ps1 -Runtime build/bin/Release/lode.exe -Runs 20
param(
    [string]$Runtime = "build/bin/Release/lode.exe",
    [int]$Runs = 10,
    [string]$Script = "bench/fixtures/big.luau"
)

# First run populates the compile cache (not measured).
& $Runtime $Script | Out-Null

$times = @()
for ($i = 0; $i -lt $Runs; $i++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & $Runtime $Script | Out-Null
    $sw.Stop()
    $times += $sw.ElapsedMilliseconds
}

# Arithmetic mean over the runs, matching the ns/op harnesses (7-10 repeats,
# mean reported); min kept as a lower-bound diagnostic.
$stats = $times | Measure-Object -Average
$avg = [int][math]::Round($stats.Average)
$sorted = @($times | Sort-Object)
$min = $sorted[0]

Write-Output "cli_warm_ms: avg=$avg min=$min runs=$Runs"
