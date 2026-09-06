<#
.SYNOPSIS
Configures, builds and optionally tests an AMD64 preset using installed MSVC.
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release', 'asan')][string]$Preset = 'debug',
    [switch]$Configure,
    [switch]$Test,
    [switch]$ResourceTests
)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC AMD64 tools were not found.' }
$devCommand = '"' + (Join-Path $installation 'Common7/Tools/VsDevCmd.bat') + '" -arch=x64 -host_arch=x64 >nul && set'
cmd /c $devCommand | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    if ($Configure -or $ResourceTests) {
        $configureArgs = @('--preset', $Preset)
        if ($ResourceTests) { $configureArgs += '-DPHPVPN_BUILD_RESOURCE_TESTS=ON' }
        & cmake @configureArgs
        if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE) { throw 'CMake build failed.' }
    if ($Test) {
        & ctest --preset $Preset
        if ($LASTEXITCODE) { throw 'CTest failed.' }
    }
} finally { Pop-Location }
