# Measures the warm-run wall time of lode_runtime on a cached .luau fixture.
# Usage:
#   powershell -ExecutionPolicy Bypass -File bench/luau/cli_warm.ps1
#   powershell -ExecutionPolicy Bypass -File bench/luau/cli_warm.ps1 -Runtime build/bin/Release/lode_runtime.exe -Runs 20
param(
    [string]$Runtime = "build/bin/Release/lode_runtime.exe",
    [int]$Runs = 20,
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

$sorted = @($times | Sort-Object)
$median = $sorted[[int]($sorted.Count / 2)]
$min = $sorted[0]

Write-Output "cli_warm_ms: median=$median min=$min runs=$Runs"
