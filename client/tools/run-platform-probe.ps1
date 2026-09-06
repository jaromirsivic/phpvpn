[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$clientRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $clientRoot
$output = Join-Path $repositoryRoot 'work\build\platform-probe'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC AMD64 tools were not found.' }
$devCommand = '"' + (Join-Path $installation 'Common7\Tools\VsDevCmd.bat') + '" -arch=x64 -host_arch=x64 >nul && set'
cmd /c $devCommand | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
Push-Location $output
try {
    & cl /nologo /std:c++20 /EHsc /W4 (Join-Path $clientRoot 'tests\platform_plan3_probe.cpp') /Feplatform-plan3-probe.exe
    if ($LASTEXITCODE) { throw 'Platform probe compilation failed.' }
    & .\platform-plan3-probe.exe | Tee-Object -FilePath (Join-Path $output 'result.txt')
    if ($LASTEXITCODE) { throw 'Platform probe failed.' }
} finally { Pop-Location }
