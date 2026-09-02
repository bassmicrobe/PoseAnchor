[CmdletBinding()]
param(
    [ValidateRange(3, 31)]
    [int]$Runs = 7,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'

function Read-KeyValueOutput {
    param([Parameter(Mandatory)][string]$Executable)

    $lines = & $Executable
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark failed ($LASTEXITCODE): $Executable"
    }

    $result = @{}
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.+)$') {
            $result[$Matches[1]] = $Matches[2]
        }
    }
    return $result
}

function Get-Statistics {
    param([Parameter(Mandatory)][double[]]$Values)

    $sorted = @($Values | Sort-Object)
    $middle = [Math]::Floor($sorted.Count / 2)
    $median = if (($sorted.Count % 2) -eq 0) {
        ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
    } else {
        $sorted[$middle]
    }
    return [PSCustomObject]@{
        Median = $median
        Min = $sorted[0]
        Max = $sorted[-1]
    }
}

if (-not $SkipBuild) {
    $vsWhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vsWhere)) {
        throw 'Visual Studio Installer (vswhere.exe) was not found.'
    }
    $vsInstall = & $vsWhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsInstall) {
        throw 'MSVC C++ tools are missing.'
    }
    $cmake = Join-Path $vsInstall `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) {
        throw "Visual Studio's bundled CMake was not found: $cmake"
    }

    Push-Location $projectRoot
    try {
        & $cmake --preset vs2022-x64
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
        & $cmake --build $buildRoot --config Release --parallel --target `
            pose_anchor_benchmark pose_anchor_device_registry_benchmark
        if ($LASTEXITCODE -ne 0) { throw "Release build failed: $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
}

$filterExecutable = Join-Path $buildRoot 'Release\pose_anchor_benchmark.exe'
$registryExecutable = Join-Path $buildRoot `
    'Release\pose_anchor_device_registry_benchmark.exe'
foreach ($executable in @($filterExecutable, $registryExecutable)) {
    if (-not (Test-Path -LiteralPath $executable)) {
        throw "Benchmark executable was not found: $executable"
    }
}

$tracking = [System.Collections.Generic.List[double]]::new()
$disturbed = [System.Collections.Generic.List[double]]::new()
$idleScan = [System.Collections.Generic.List[double]]::new()
$watchdogScan = [System.Collections.Generic.List[double]]::new()
$rundownPair = [System.Collections.Generic.List[double]]::new()
$mutexPair = [System.Collections.Generic.List[double]]::new()
$clockNow = [System.Collections.Generic.List[double]]::new()
$poseCopy = [System.Collections.Generic.List[double]]::new()
$poseBytes = 0

for ($run = 1; $run -le $Runs; ++$run) {
    $filter = Read-KeyValueOutput -Executable $filterExecutable
    if ([uint64]$filter.tracking_allocations -ne 0 -or
        [uint64]$filter.disturbed_allocations -ne 0) {
        throw "Hot-path allocation detected in run $run."
    }
    $tracking.Add([double]$filter.tracking_ns_per_sample)
    $disturbed.Add([double]$filter.disturbed_ns_per_sample)

    $registry = Read-KeyValueOutput -Executable $registryExecutable
    $idleScan.Add([double]$registry.idle_scan_ns)
    $watchdogScan.Add([double]$registry.watchdog_64_slot_scan_ns)
    $rundownPair.Add([double]$registry.callback_rundown_pair_ns)
    $mutexPair.Add([double]$registry.uncontended_mutex_pair_ns)
    $clockNow.Add([double]$registry.steady_clock_now_ns)
    $poseCopy.Add([double]$registry.copy_overhead_ns)
    $poseBytes = [int]$registry.driver_pose_bytes
}

$rows = @(
    [PSCustomObject]@{ Benchmark = 'Tracking push'; Unit = 'ns/sample'; Values = $tracking }
    [PSCustomObject]@{ Benchmark = 'Disturbed push'; Unit = 'ns/sample'; Values = $disturbed }
    [PSCustomObject]@{ Benchmark = 'Idle classification'; Unit = 'ns/call'; Values = $idleScan }
    [PSCustomObject]@{ Benchmark = '64-slot watchdog scan'; Unit = 'ns/frame'; Values = $watchdogScan }
    [PSCustomObject]@{ Benchmark = 'Callback rundown atomic pair'; Unit = 'ns/call'; Values = $rundownPair }
    [PSCustomObject]@{ Benchmark = 'Uncontended tracker mutex pair'; Unit = 'ns/call'; Values = $mutexPair }
    [PSCustomObject]@{ Benchmark = 'steady_clock::now'; Unit = 'ns/call'; Values = $clockNow }
    [PSCustomObject]@{ Benchmark = "DriverPose copy ($poseBytes B)"; Unit = 'ns/call'; Values = $poseCopy }
)

$summary = foreach ($row in $rows) {
    $statistics = Get-Statistics -Values $row.Values.ToArray()
    [PSCustomObject]@{
        Benchmark = $row.Benchmark
        Runs = $Runs
        Median = [Math]::Round($statistics.Median, 2)
        Min = [Math]::Round($statistics.Min, 2)
        Max = [Math]::Round($statistics.Max, 2)
        Unit = $row.Unit
    }
}

$summary | Format-Table -AutoSize
Write-Host 'Correctness: benchmark scenarios passed; timed PoseFilter allocations: 0'
