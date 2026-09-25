<# .SYNOPSIS
Read-only verification of the complete server and client distribution.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$DistributionDirectory,
    [string]$IconResourceTest = (Join-Path $PSScriptRoot '../work/build/release/tests/phpvpn-icon-resource-tests.exe')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'distribution-common.ps1')
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$tree = (Resolve-Path -LiteralPath $DistributionDirectory).Path
$null = Assert-DistributionPath -Path $tree -Within ([IO.Path]::GetDirectoryName($tree)) -Tree
$sources = Get-DistributionSources
$expected = New-Object 'Collections.Generic.List[string]'
foreach ($entry in $sources.GetEnumerator()) {
    $source = Join-Path $repository $entry.Value
    $copy = Join-Path $tree $entry.Key
    if (-not (Test-Path -LiteralPath $copy -PathType Leaf)) { throw "Missing distribution file: $($entry.Key)" }
    if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
        throw "Stale distribution source: $($entry.Key)"
    }
    $expected.Add($entry.Key)
    if ($entry.Key.EndsWith('.sh')) {
        $bytes = [IO.File]::ReadAllBytes($copy)
        if ($bytes -contains 13 -or $bytes[0] -ne 35) { throw "Shell script must use LF and no BOM: $copy" }
    }
}
foreach ($platform in @('macOS', 'Linux')) {
    $name = "client/$platform/README.md"
    if ([IO.File]::ReadAllText((Join-Path $tree $name)) -cne (Get-UnsupportedClientReadme $platform)) {
        throw "Incorrect unsupported-platform README: $platform"
    }
    $expected.Add($name)
}
$client = Join-Path $tree 'client/Windows/x64'
$clientNames = @('phpvpn-service.exe','phpvpn-tray.exe','phpvpn.exe','install-phpvpn.exe','uninstall-phpvpn.exe',
    'wintun.dll','LICENSE-wintun.txt','LICENSE-material-symbols.txt','NOTICE-material-symbols.txt',
    'install.bat','install.ps1','uninstall.bat','uninstall.ps1','release-manifest.json','release-manifest.sig','release-signing.cer')
foreach ($name in $clientNames) { $expected.Add("client/Windows/x64/$name") }
$actual = @(Get-ChildItem -LiteralPath $tree -File -Recurse -Force | ForEach-Object {
    $_.FullName.Substring($tree.Length + 1).Replace('\','/')
})
if (Compare-Object $expected.ToArray() $actual -CaseSensitive) { throw 'Distribution contains missing or unexpected files (including hidden runtime files).' }
$directories = @('server','server/PHP','server/Python','server/Node.js','client','client/Windows','client/Windows/x64','client/macOS','client/Linux')
$actualDirs = @(Get-ChildItem -LiteralPath $tree -Directory -Recurse -Force | ForEach-Object {
    $_.FullName.Substring($tree.Length + 1).Replace('\','/')
})
if (Compare-Object $directories $actualDirs -CaseSensitive) { throw 'Distribution directory set is incorrect.' }
$manifest = Get-Content -LiteralPath (Join-Path $client 'release-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.architecture -cne 'x64') { throw 'Complete distribution requires a Windows x64 client.' }
& (Join-Path $repository 'client/tools/verify-package.ps1') -PackageDirectory $client -IconResourceTest $IconResourceTest
Write-Output 'PASS: complete distribution allowlist, source hashes, LF shell scripts, empty Node.js, platform READMEs and signed Windows package.'
