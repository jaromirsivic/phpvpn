<#
.SYNOPSIS
    Verifies a generated Windows package and its supported read-only entry point.
#>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$IconResourceTest = (Join-Path $PSScriptRoot '../../work/build/release/tests/phpvpn-icon-resource-tests.exe'))
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$scriptNames = @('install.bat', 'install.ps1', 'uninstall.bat', 'uninstall.ps1')
foreach ($name in $scriptNames) {
    $source = Join-Path $PSScriptRoot "package/$name"
    $copy = Join-Path $package $name
    if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
        throw "The package contains a stale $name. Rebuild the complete distribution with tools/make-distribution.ps1, or only this client package with client/tools/make-dist.ps1 -OutputDirectory <path>."
    }
}
$manifest = Get-Content -LiteralPath (Join-Path $package 'release-manifest.json') -Raw | ConvertFrom-Json
$required = @('phpvpn-service.exe', 'phpvpn-tray.exe', 'phpvpn.exe',
    'install-phpvpn.exe', 'uninstall-phpvpn.exe', 'wintun.dll') + $scriptNames
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $name) -PathType Leaf) -or
        @($manifest.files | Where-Object { $_.path -ceq $name }).Count -ne 1) {
        throw "Required package file is missing or duplicated: $name"
    }
}
$actual = @(Get-ChildItem -LiteralPath $package -File | ForEach-Object { $_.Name })
$expected = @($manifest.files | ForEach-Object { $_.path }) + @('release-manifest.json', 'release-manifest.sig', 'release-signing.cer')
if (Compare-Object $expected $actual -CaseSensitive) { throw 'The package file set differs from its manifest.' }
foreach ($entry in $manifest.files) {
    $path = Join-Path $package $entry.path
    if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256) { throw "Manifest mismatch: $($entry.path)" }
}
foreach ($name in @('phpvpn-service.exe', 'phpvpn-tray.exe', 'phpvpn.exe', 'install-phpvpn.exe', 'uninstall-phpvpn.exe', 'wintun.dll')) {
    $stream = [IO.File]::OpenRead((Join-Path $package $name))
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid PE header: $name" }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $name" }
        $machine = $reader.ReadUInt16()
        $architecture = switch ($machine) { 0x8664 { 'x64' } 0xaa64 { 'arm64' } 0x14c { 'x86' } default { 'unknown' } }
        if ($architecture -ne $manifest.architecture) { throw "Package architecture mismatch: $name" }
    } finally { $reader.Dispose(); $stream.Dispose() }
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
if (-not (Test-Path -LiteralPath $IconResourceTest -PathType Leaf)) { throw 'Build phpvpn-icon-resource-tests or supply -IconResourceTest.' }
& $IconResourceTest $package (Join-Path $PSScriptRoot '../../assets')
if ($LASTEXITCODE -ne 0) { throw 'Packaged PE icons do not match the source assets.' }
Write-Output 'PASS: source script parity, manifest files/sizes/hashes and supported BAT VerifyOnly without installation feedback.'
