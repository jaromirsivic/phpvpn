<#
.SYNOPSIS
    Verifies a generated Windows package and its supported read-only entry point.
#>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$scriptNames = @('install.bat', 'install.ps1', 'uninstall.bat', 'uninstall.ps1')
foreach ($name in $scriptNames) {
    $source = Join-Path $PSScriptRoot "package/$name"
    $copy = Join-Path $package $name
    if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
        throw "The package contains a stale $name. Regenerate it with make-dist.ps1."
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $package 'release-manifest.json') -Raw | ConvertFrom-Json
$actual = @(Get-ChildItem -LiteralPath $package -File | ForEach-Object { $_.Name })
$expected = @($manifest.files | ForEach-Object { $_.path }) + @('release-manifest.json', 'release-manifest.sig', 'release-signing.cer')
if (Compare-Object $expected $actual -CaseSensitive) { throw 'The package file set differs from its manifest.' }
foreach ($entry in $manifest.files) {
    $path = Join-Path $package $entry.path
    if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256) { throw "Manifest mismatch: $($entry.path)" }
}
# Use the same BAT entry point as an interactive user. VerifyOnly must neither
# claim installation nor reach the success-only delay in the PowerShell helper.
$output = & (Join-Path $package 'install.bat') -VerifyOnly 2>&1
$code = $LASTEXITCODE
$text = $output -join "`n"
if ($code -ne 0) { throw "Package verification failed ($code): $text" }
if ($text -match 'PHPVPN (un)?installed successfully' -or $text -notmatch 'nothing was installed') {
    throw "VerifyOnly reported an incorrect result: $text"
}
$output | Write-Output
Write-Output 'PASS: source script parity, manifest files/sizes/hashes and supported BAT VerifyOnly without installation feedback.'
