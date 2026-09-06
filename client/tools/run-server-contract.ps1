<#
.SYNOPSIS
Builds and runs the real C++/WinHTTP/CNG contract against an isolated local PHP relay.
.DESCRIPTION
Requires PHP 8.4+, the wizard-default profile (CHANGE_THIS_SERVER_SECRET and
default .2-.15 access keys), MSVC AMD64, CMake and Ninja. An absent or incorrect
server is a failure. This runner never starts/stops a service or modifies a relay.
The tests consume the fixture's .2-.15 queues. Do not share the fixture with
Python integration tests or another client while this target is running.

One-time fixture preparation, from the repository root, in separate terminal:
  ./tools/deploy-test.ps1 -Profile wizard-default -WebRoot "$PWD/work/contract-php" -BufferDirectory "$PWD/work/contract-buffer" -BaseUrl http://127.0.0.1:18093/phpvpn.php -ConfigurationPath "$PWD/work/contract-config.json"
  php -S 127.0.0.1:18093 -t ./work/contract-php

Then explicitly run:
  ./client/tools/run-server-contract.ps1

The separate work/build/contract-debug directory leaves ordinary Debug tests
independent of server availability. Manual equivalent from an AMD64 Developer
PowerShell is:
  cmake -S client -B work/build/contract-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DPHPVPN_BUILD_CONTRACT_TESTS=ON
  cmake --build work/build/contract-debug --target phpvpn-contract-tests
  ctest --test-dir work/build/contract-debug -R '^phpvpn-contract-tests$' -V

PHPVPN_CONTRACT_URL overrides the executable's default endpoint, restricted to
http://127.0.0.1:<port>/phpvpn.php. No remote deployment is accepted.
#>
[CmdletBinding()]
param([string]$Url = 'http://127.0.0.1:18093/phpvpn.php')
$ErrorActionPreference = 'Stop'
$contractUri = [uri]$Url
if (-not $contractUri.IsAbsoluteUri -or $contractUri.Scheme -ne 'http' -or
    $contractUri.Host -ne '127.0.0.1' -or $contractUri.AbsolutePath -ne '/phpvpn.php' -or
    $contractUri.UserInfo -or $contractUri.Query -or $contractUri.Fragment) {
    throw 'The contract fixture must be http://127.0.0.1:<port>/phpvpn.php.'
}
$availability = Invoke-WebRequest -Uri $Url -UseBasicParsing -TimeoutSec 5
if ($availability.StatusCode -ne 200) { throw 'The local PHP fixture is unavailable.' }
$clientRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent $clientRoot
$contractBuild = Join-Path $repositoryRoot 'work/build/contract-debug'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC AMD64 tools were not found.' }
$devCommand = '"' + (Join-Path $installation 'Common7/Tools/VsDevCmd.bat') + '" -arch=x64 -host_arch=x64 >nul && set'
cmd /c $devCommand | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
& cmake -S $clientRoot -B $contractBuild -G Ninja '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_CXX_COMPILER=cl.exe' '-DBUILD_TESTING=ON' '-DPHPVPN_BUILD_CONTRACT_TESTS=ON'
if ($LASTEXITCODE) { throw 'Contract configuration failed.' }
& cmake --build $contractBuild --target phpvpn-contract-tests
if ($LASTEXITCODE) { throw 'Contract compilation failed.' }
$previousContractUrl = [Environment]::GetEnvironmentVariable('PHPVPN_CONTRACT_URL', 'Process')
try {
    $env:PHPVPN_CONTRACT_URL = $Url
    & ctest --test-dir $contractBuild -R '^phpvpn-contract-tests$' -V
    if ($LASTEXITCODE) { throw 'The real PHP contract test failed. See CTest output.' }
} finally {
    [Environment]::SetEnvironmentVariable('PHPVPN_CONTRACT_URL', $previousContractUrl, 'Process')
}
