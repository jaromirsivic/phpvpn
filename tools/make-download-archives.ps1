<# .SYNOPSIS
Creates the client archives for the download page (distribution.md, client download archives).
From a verified distribution tree it writes <OutputDirectory>/phpvpn/client/<major>.x/ with
phpvpn-win-x64.zip, phpvpn-macos.zip and phpvpn-linux.zip, each holding its platform folder
(client/Windows/x64, client/macOS/arm64 or the placeholder client/macOS, client/Linux/x64)
directly in the ZIP root. Every archive is checked against its folder (file set, SHA-256,
POSIX modes) before the previous output is replaced. tools/make-distribution.ps1 calls it
after publishing dist; run it alone only to recreate the archives of the current dist.
#>
[CmdletBinding()]
param(
    [string]$DistributionDirectory = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'distribution-common.ps1')
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $DistributionDirectory) { $DistributionDirectory = Join-Path $repository 'dist' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository 'dist-downloads' }
$distribution = Assert-DistributionPath $DistributionDirectory $repository -Tree
$output = Assert-DistributionPath $OutputDirectory $repository -Tree
$pending = Assert-DistributionPath ($output + '.new') $repository -Tree
# The download path carries only the major version, so links stay stable within 1.x.
$major = (Get-ClientVersion $repository).Split('.')[0]
# phpvpn-macos.zip holds the staged client/macOS/arm64 folder, or the placeholder client/macOS
# (README.md only) when the distribution was built without a macOS package.
$macosStaged = (Get-MacosDistributionState $distribution) -eq 'staged'
$archives = [ordered]@{
    'phpvpn-win-x64.zip' = 'client/Windows/x64'
    'phpvpn-macos.zip' = $(if ($macosStaged) { Get-MacosClientFolder } else { 'client/macOS' })
    'phpvpn-linux.zip' = 'client/Linux/x64'
}
$executables = @{ 'phpvpn-linux.zip' = @('install.sh', 'uninstall.sh') }
if ($macosStaged) { $executables['phpvpn-macos.zip'] = @('install.sh', 'uninstall.sh') }

if (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending -Recurse -Force }
$target = Join-Path $pending "phpvpn/client/$major.x"
[IO.Directory]::CreateDirectory($target) | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($item in $archives.GetEnumerator()) {
    $source = Join-Path $distribution $item.Value
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Distribution folder $($item.Value) is missing." }
    $archive = Join-Path $target $item.Key
    Write-DistributionZip $source $archive
    $required = if ($executables.ContainsKey($item.Key)) { $executables[$item.Key] } else { @() }
    Assert-DistributionZipModes $archive $required
    # Exactly the files of the platform folder, byte for byte.
    $expected = @{}
    $root = [IO.Path]::GetFullPath($source).TrimEnd('\','/')
    foreach ($file in Get-ChildItem -LiteralPath $root -File -Recurse -Force) {
        $expected[$file.FullName.Substring($root.Length + 1).Replace('\','/')] = Get-Sha256 $file.FullName
    }
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $seen = 0
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName.EndsWith('/')) { continue }
            if (-not $expected.ContainsKey($entry.FullName)) { throw "$($item.Key) holds an unexpected entry $($entry.FullName)" }
            $stream = $entry.Open()
            try {
                $sha = [Security.Cryptography.SHA256]::Create()
                try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
            } finally { $stream.Dispose() }
            if ($hash -ne $expected[$entry.FullName]) { throw "$($item.Key) entry $($entry.FullName) differs from $($item.Value)" }
            $seen++
        }
        if ($seen -ne $expected.Count) { throw "$($item.Key) does not hold every file of $($item.Value)" }
    } finally { $zip.Dispose() }
    Write-Output "PASS: $($item.Key) matches $($item.Value) ($seen files)"
}
if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
Move-Item -LiteralPath $pending -Destination $output
Write-Output "Client downloads: $output"
