<#
.SYNOPSIS
Builds and runs explicit Win32 UI tests using private randomized named pipes.
.DESCRIPTION
Requires an interactive Windows desktop, MSVC AMD64, CMake and Ninja.
Production WizardWindow, readers, EditorWindow and ContractsWindow are linked
directly. The IPC backend is deterministic test state, not the real service.
No service, ProgramData, adapter, route or real config file is changed.
Only windows in the test process are inspected or controlled.

Run automated checks:
  ./client/tools/run-tray-ui-tests.ps1
Run one scenario:
  ./client/tools/run-tray-ui-tests.ps1 -Filter late_start
Leave the edit-mode Wizard open for manual screenshots:
  ./client/tools/run-tray-ui-tests.ps1 -Interactive

The executable also accepts --interactive directly. Close the Wizard or the
confirmed Contracts window to exit. Test-only controls do not enter phpvpn-tray.
Screenshots are intentionally not written into the repository.
#>
[CmdletBinding()]
param([switch]$Interactive, [string]$Filter = '')
$ErrorActionPreference = 'Stop'
$clientRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $clientRoot
$uiBuild = Join-Path $repositoryRoot 'work/build/ui-debug'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC AMD64 tools were not found.' }
$devCommand = '"' + (Join-Path $installation 'Common7/Tools/VsDevCmd.bat') + '" -arch=x64 -host_arch=x64 >nul && set'
cmd /c $devCommand | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
& cmake -S $clientRoot -B $uiBuild -G Ninja '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_CXX_COMPILER=cl.exe' '-DBUILD_TESTING=ON' '-DPHPVPN_BUILD_UI_TESTS=ON' '-DPHPVPN_BUILD_CONTRACT_TESTS=OFF'
if ($LASTEXITCODE) { throw 'UI test configuration failed.' }
& cmake --build $uiBuild --target phpvpn-tray-ui-tests
if ($LASTEXITCODE) { throw 'UI test compilation failed.' }
$uiExecutable = Join-Path $uiBuild 'tests/phpvpn-tray-ui-tests.exe'
if ($Interactive) { & $uiExecutable --interactive }
elseif ($Filter) { & $uiExecutable $Filter }
else { & $uiExecutable }
if ($LASTEXITCODE) { throw 'Win32 UI tests reported failures.' }
