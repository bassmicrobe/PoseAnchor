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
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string[]]$RequiredKeys,
        [string[]]$SignedKeys = @()
    )

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
    foreach ($key in $RequiredKeys) {
        $number = 0.0
        if (-not $result.ContainsKey($key) -or
            -not [double]::TryParse($result[$key],
                [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
            [double]::IsNaN($number) -or [double]::IsInfinity($number) -or
            ($number -lt 0 -and $key -notin $SignedKeys)) {
            throw "Missing or invalid benchmark metric '${key}': $Executable"
        }
        $result[$key] = $number
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
    $vsInstall = & $vsWhere -latest -products * -version '[17.0,18.0)' `
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
$lostTicks = [System.Collections.Generic.List[double]]::new()
$idleScan = [System.Collections.Generic.List[double]]::new()
$watchdogScan = [System.Collections.Generic.List[double]]::new()
$rundownPair = [System.Collections.Generic.List[double]]::new()
$mutexPair = [System.Collections.Generic.List[double]]::new()
$clockNow = [System.Collections.Generic.List[double]]::new()
$poseCopy = [System.Collections.Generic.List[double]]::new()
$transformPair = [System.Collections.Generic.List[double]]::new()
$adapterPipeline = [System.Collections.Generic.List[double]]::new()
$poseBytes = 0

for ($run = 1; $run -le $Runs; ++$run) {
    $filter = Read-KeyValueOutput -Executable $filterExecutable -RequiredKeys @(
        'tracking_allocations', 'disturbed_allocations',
        'tracking_ns_per_sample', 'disturbed_ns_per_sample', 'samples_per_phase',
        'lost_tick_ns_per_tick', 'lost_tick_outputs', 'lost_tick_invalid_outputs',
        'lost_tick_allocations', 'lost_tick_lost_entries'
    )
    if ([uint64]$filter.tracking_allocations -ne 0 -or
        [uint64]$filter.disturbed_allocations -ne 0) {
        throw "Hot-path allocation detected in run $run."
    }
    if ($filter.samples_per_phase -le 0 -or
        [Math]::Truncate($filter.samples_per_phase) -ne $filter.samples_per_phase -or
        $filter.lost_tick_outputs -ne $filter.samples_per_phase -or
        $filter.lost_tick_invalid_outputs -ne $filter.samples_per_phase -or
        $filter.lost_tick_allocations -ne 0 -or
        $filter.lost_tick_lost_entries -ne 1) {
        throw "Lost watchdog scenario validation failed in run $run."
    }
    $tracking.Add([double]$filter.tracking_ns_per_sample)
    $disturbed.Add([double]$filter.disturbed_ns_per_sample)
    $lostTicks.Add([double]$filter.lost_tick_ns_per_tick)

    $registry = Read-KeyValueOutput -Executable $registryExecutable -RequiredKeys @(
        'idle_scan_ns', 'watchdog_64_slot_scan_ns', 'callback_rundown_pair_ns',
        'uncontended_mutex_pair_ns', 'steady_clock_now_ns',
        'raw_forward_ns', 'copied_forward_ns', 'copy_overhead_ns',
        'driver_pose_bytes', 'static_transform_pair_ns', 'adapter_pipeline_ns',
        'adapter_unexpected_changes'
    ) -SignedKeys @('copy_overhead_ns')
    # Only the difference of two independent timings may be negative due to
    # measurement noise. Every raw duration and scenario counter stays nonnegative.
    if ($registry.adapter_unexpected_changes -ne 0) {
        throw "Unexpected static transform change detected in run $run."
    }
    $idleScan.Add([double]$registry.idle_scan_ns)
    $watchdogScan.Add([double]$registry.watchdog_64_slot_scan_ns)
    $rundownPair.Add([double]$registry.callback_rundown_pair_ns)
    $mutexPair.Add([double]$registry.uncontended_mutex_pair_ns)
    $clockNow.Add([double]$registry.steady_clock_now_ns)
    $poseCopy.Add([double]$registry.copy_overhead_ns)
    $transformPair.Add([double]$registry.static_transform_pair_ns)
    $adapterPipeline.Add([double]$registry.adapter_pipeline_ns)
    $poseBytes = [int]$registry.driver_pose_bytes
}

$rows = @(
    [PSCustomObject]@{ Benchmark = 'Tracking push'; Unit = 'ns/sample'; Values = $tracking }
    [PSCustomObject]@{ Benchmark = 'Disturbed push'; Unit = 'ns/sample'; Values = $disturbed }
    [PSCustomObject]@{ Benchmark = 'Lost watchdog tick'; Unit = 'ns/tick'; Values = $lostTicks }
    [PSCustomObject]@{ Benchmark = 'Idle classification'; Unit = 'ns/call'; Values = $idleScan }
    [PSCustomObject]@{ Benchmark = '64-slot watchdog scan'; Unit = 'ns/frame'; Values = $watchdogScan }
    [PSCustomObject]@{ Benchmark = 'Callback rundown atomic pair'; Unit = 'ns/call'; Values = $rundownPair }
    [PSCustomObject]@{ Benchmark = 'Uncontended tracker mutex pair'; Unit = 'ns/call'; Values = $mutexPair }
    [PSCustomObject]@{ Benchmark = 'steady_clock::now'; Unit = 'ns/call'; Values = $clockNow }
    [PSCustomObject]@{ Benchmark = "DriverPose copy ($poseBytes B)"; Unit = 'ns/call'; Values = $poseCopy }
    [PSCustomObject]@{ Benchmark = 'Static transform read/check pair'; Unit = 'ns/sample'; Values = $transformPair }
    [PSCustomObject]@{ Benchmark = 'Adapter read/check/sample pipeline'; Unit = 'ns/sample'; Values = $adapterPipeline }
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
