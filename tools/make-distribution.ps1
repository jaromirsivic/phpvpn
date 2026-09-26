<# .SYNOPSIS
Builds, verifies and publishes the entire dist tree and a separately verified ZIP.
Client CMake configuration must already exist. The build is always refreshed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [string]$CertificateThumbprint = '7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8',
    [switch]$SignBinaries
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'distribution-common.ps1')
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
$sourceEntry = @($cache | Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' })
if ($sourceEntry.Count -ne 1 -or [IO.Path]::GetFullPath($sourceEntry[0].Split('=',2)[1]) -ne (Join-Path $repository 'client')) {
    throw 'BuildDirectory was not configured from this checkout/client.'
}
# Supply the compiler environment for Ninja, including automatic reconfiguration.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'MSVC AMD64 tools were not found.' }
$devCommand = '"' + (Join-Path $installation 'Common7/Tools/VsDevCmd.bat') + '" -arch=x64 -host_arch=x64 >nul && set'
cmd /c $devCommand | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
& cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'Client build failed. Existing distribution was not changed.' }
$work = Join-Path $repository 'work'
$null = Assert-DistributionPath $work $repository
[IO.Directory]::CreateDirectory($work) | Out-Null
$session = Join-Path $work ('distribution-' + [Guid]::NewGuid().ToString('N'))
$null = Assert-DistributionPath $session $work
[IO.Directory]::CreateDirectory($session) | Out-Null
$staging = Join-Path $session 'staging'
$archive = Join-Path $session 'phpvpn.zip'
$unpacked = Join-Path $session 'unpacked'
$dist = Assert-DistributionPath (Join-Path $repository 'dist') $repository -Tree
$zipTarget = Assert-DistributionPath (Join-Path $repository 'dist.zip') $repository
$iconTest = Join-Path $build 'tests/phpvpn-icon-resource-tests.exe'
foreach ($directory in @('server/PHP','server/Python','server/Node.js','client/Windows/x64','client/macOS','client/Linux')) {
    [IO.Directory]::CreateDirectory((Join-Path $staging $directory)) | Out-Null
}
foreach ($entry in (Get-DistributionSources).GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $repository $entry.Value) -Destination (Join-Path $staging $entry.Key)
}
foreach ($platform in @('macOS','Linux')) {
    [IO.File]::WriteAllText((Join-Path $staging "client/$platform/README.md"),
        (Get-UnsupportedClientReadme $platform), [Text.UTF8Encoding]::new($false))
}
& (Join-Path $repository 'client/tools/make-dist.ps1') -BuildDirectory $build `
    -OutputDirectory (Join-Path $staging 'client/Windows/x64') `
    -CertificateThumbprint $CertificateThumbprint -SignBinaries:$SignBinaries
& (Join-Path $PSScriptRoot 'verify-distribution.ps1') -DistributionDirectory $staging -IconResourceTest $iconTest
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($archive, 'Create')
try {
    foreach ($directory in Get-ChildItem -LiteralPath $staging -Directory -Recurse) {
        $name = $directory.FullName.Substring($staging.Length + 1).Replace('\','/') + '/'
        $entry = $zip.CreateEntry($name)
        $entry.ExternalAttributes = 1106051088 # POSIX directory 0755 plus DOS directory flag
    }
    foreach ($file in Get-ChildItem -LiteralPath $staging -File -Recurse) {
        $name = $file.FullName.Substring($staging.Length + 1).Replace('\','/')
        $entry = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $name)
        # Signed Int32 representation of regular file 0755 / 0644.
        $entry.ExternalAttributes = if ($name.EndsWith('.sh')) { -2115174400 } else { -2119958528 }
    }
} finally { $zip.Dispose() }
[IO.Compression.ZipFile]::ExtractToDirectory($archive, $unpacked)
& (Join-Path $PSScriptRoot 'verify-distribution.ps1') -DistributionDirectory $unpacked -IconResourceTest $iconTest

Publish-Distribution -Repository $repository -Session $session -Staging $staging -Archive $archive
& (Join-Path $PSScriptRoot 'verify-distribution.ps1') -DistributionDirectory $dist -IconResourceTest $iconTest -AllowLocalRuntime
# Rollback files are needed only until the canonical output passes validation.
# Python operational state was never moved out of dist by the publisher.
$previous = Assert-DistributionPath (Join-Path $session 'previous-dist') $session -Tree
$previousZip = Assert-DistributionPath (Join-Path $session 'previous-dist.zip') $session
if (Test-Path -LiteralPath $previous) { Remove-Item -LiteralPath $previous -Recurse -Force }
if (Test-Path -LiteralPath $previousZip) { Remove-Item -LiteralPath $previousZip -Force }
Write-Output "Distribution: $dist"
Write-Output "Archive: $zipTarget"
Write-Output "Verified archive extraction (for isolated runtime tests): $unpacked"
Write-Output "Build staging: $session (temporary rollback removed after verification)"
