[CmdletBinding(SupportsShouldProcess)]
param()

$ErrorActionPreference = 'Stop'
$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $installer)) {
    throw 'Visual Studio Installer was not found. Install Visual Studio 2022 first.'
}
if (-not (Test-Path -LiteralPath $vsWhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

# By default vswhere searches Community, Professional, and Enterprise. Restrict
# the result to Visual Studio 2022 and use the actual instance path/channel so a
# non-default installation can be modified safely.
$installPath = [string](& $vsWhere -latest -version '[17.0,18.0)' -property installationPath)
$channelId = [string](& $vsWhere -latest -version '[17.0,18.0)' -property channelId)
if (-not $installPath -or -not (Test-Path -LiteralPath $installPath)) {
    throw 'No complete Visual Studio 2022 Community, Professional, or Enterprise instance was found.'
}
if (-not $channelId) {
    throw "The update channel for the Visual Studio instance could not be determined: $installPath"
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated (Run as administrator) PowerShell window.'
}

if ($PSCmdlet.ShouldProcess($installPath, 'Add the Desktop development with C++ workload')) {
    $arguments = @(
        'modify',
        '--installPath', "`"$installPath`"",
        '--channelId', $channelId,
        '--add', 'Microsoft.VisualStudio.Workload.NativeDesktop',
        '--includeRecommended',
        '--passive',
        '--norestart'
    )
    $process = Start-Process -FilePath $installer -ArgumentList $arguments `
        -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) { throw "Visual Studio Installer failed: $($process.ExitCode)" }
}
