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

# All recursive moves have validated absolute targets and link-free trees.
# Keep the previous release and rollback on publication failure.
$previous = Join-Path $session 'previous-dist'
$previousZip = Join-Path $session 'previous-dist.zip'
$failedPublication = Join-Path $session 'failed-publication'
$null = Assert-DistributionPath $staging $session -Tree
$null = Assert-DistributionPath $previous $session
$null = Assert-DistributionPath $previousZip $session
$null = Assert-DistributionPath $failedPublication $session
$null = Assert-DistributionPath $dist $repository -Tree
$hadDist = Test-Path -LiteralPath $dist
$hadZip = Test-Path -LiteralPath $zipTarget
$publishedDist = $false
try {
    if ($hadDist) { Move-Item -LiteralPath $dist -Destination $previous }
    if ($hadZip) { Move-Item -LiteralPath $zipTarget -Destination $previousZip }
    Move-Item -LiteralPath $staging -Destination $dist
    $publishedDist = $true
    Move-Item -LiteralPath $archive -Destination $zipTarget
} catch {
    # Move failed new outputs aside before restoring the previous publication.
    if ($publishedDist -and (Test-Path -LiteralPath $dist)) {
        $null = Assert-DistributionPath $dist $repository -Tree
        Move-Item -LiteralPath $dist -Destination $failedPublication
    }
    if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $dist }
    if (Test-Path -LiteralPath $previousZip) { Move-Item -LiteralPath $previousZip -Destination $zipTarget }
    throw
}
Write-Output "Distribution: $dist"
Write-Output "Archive: $zipTarget"
Write-Output "Verified archive extraction (for isolated runtime tests): $unpacked"
Write-Output "Previous distribution and build staging: $session"
