<#
.SYNOPSIS
    Builds only the Windows client package and its signed release manifest.

.DESCRIPTION
    plan_client.md chapters 3.5, 20.1 and 20.2. The result is a complete,
    handover ready folder that contains the five executables, the unrenamed
    architecture specific wintun.dll, the Wintun licence, the install and
    uninstall scripts and a signed release manifest with a SHA-256 for every
    packaged file.

    The manifest is never edited by hand. Its canonical form is compact UTF-8
    JSON without a byte order mark, with LF line endings, with the file list
    sorted by the ordinal path and with the signature stored separately, so the
    exact bytes that were signed are the exact bytes the installer verifies.
#>
[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,

    # Pinned signing identity. The development certificate is the default so a
    # local package can be produced without a release key.
    [string]$CertificateThumbprint = "7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8",

    # Also applies an Authenticode signature to all five executables.
    [switch]$SignBinaries
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# The defaults are resolved here rather than in the param block, because a
# script started with a relative -File path does not always have $PSScriptRoot
# available while the parameters are being bound.
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$clientRoot = (Resolve-Path (Join-Path $scriptRoot "..")).Path
$repositoryRoot = (Resolve-Path (Join-Path $clientRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot "work\build\release"
}
. (Join-Path $repositoryRoot 'tools/distribution-common.ps1')
# This partial packager must never erase the full dist or a source directory.
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = @((Join-Path $repositoryRoot 'work'), (Join-Path $repositoryRoot 'dist'))
$container = $allowed | Where-Object { $OutputDirectory.StartsWith($_ + '\', [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
if (-not $container) { throw 'Client OutputDirectory must be a child of repository work or dist. Use tools/make-distribution.ps1 for the complete distribution.' }
$OutputDirectory = Assert-DistributionPath $OutputDirectory $container -Tree

$appsDirectory = Join-Path (Resolve-Path $BuildDirectory).Path "apps"

function Get-PeMachine {
    param([string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "$Path is not a PE image."
        }
        switch ($reader.ReadUInt16()) {
            0x8664 { return "x64" }
            0x014C { return "x86" }
            0xAA64 { return "arm64" }
            default { return "unknown" }
        }
    } finally {
        $stream.Dispose()
    }
}


# ---------------------------------------------------------------- build inputs

$binaries = @("phpvpn-service.exe", "phpvpn-tray.exe", "phpvpn.exe", "install-phpvpn.exe", "uninstall-phpvpn.exe")
foreach ($name in $binaries + @("wintun.dll")) {
    $path = Join-Path $appsDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "The build is incomplete, $path is missing. Build the release preset first."
    }
}

$architecture = Get-PeMachine (Join-Path $appsDirectory "phpvpn-service.exe")
foreach ($name in $binaries) {
    if ((Get-PeMachine (Join-Path $appsDirectory $name)) -ne $architecture) {
        throw "$name does not match the package architecture $architecture."
    }
}
$driverArchitecture = Get-PeMachine (Join-Path $appsDirectory "wintun.dll")
if ($architecture -ne $driverArchitecture) {
    throw "The service is $architecture but wintun.dll is $driverArchitecture."
}

# Step 3 of chapter 20.2 is repeated here so a broken driver never reaches a
# package in the first place.
$driverSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $appsDirectory "wintun.dll")
if ($driverSignature.Status -ne "Valid") {
    throw "wintun.dll does not carry a valid Authenticode signature ($($driverSignature.Status))."
}
Write-Host "Tunnel driver : $($driverSignature.SignerCertificate.Subject)"

$driverVersionInfo = (Get-Item (Join-Path $appsDirectory "wintun.dll")).VersionInfo
$driverVersion = $driverVersionInfo.FileVersion
if ([string]::IsNullOrWhiteSpace($driverVersion)) {
    $driverVersion = "0.14.1"
}

$releaseVersion = (Get-Item (Join-Path $appsDirectory "phpvpn-service.exe")).VersionInfo.FileVersion

# ------------------------------------------------------------ package assembly

if (Test-Path -LiteralPath $OutputDirectory) {
    $null = Assert-DistributionPath $OutputDirectory $container -Tree
    Get-ChildItem -LiteralPath $OutputDirectory -Force | ForEach-Object {
        $null = Assert-DistributionPath $_.FullName $OutputDirectory -Tree
        Remove-Item -LiteralPath $_.FullName -Recurse -Force
    }
} else {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

foreach ($name in $binaries + @("wintun.dll")) {
    Copy-Item -LiteralPath (Join-Path $appsDirectory $name) -Destination $OutputDirectory
}

Copy-Item -LiteralPath (Join-Path $clientRoot "third_party\wintun\LICENSE.txt") `
          -Destination (Join-Path $OutputDirectory "LICENSE-wintun.txt")
Copy-Item -LiteralPath (Join-Path $clientRoot "tools\package\install.bat") -Destination $OutputDirectory
Copy-Item -LiteralPath (Join-Path $clientRoot "tools\package\uninstall.bat") -Destination $OutputDirectory
Copy-Item -LiteralPath (Join-Path $clientRoot "tools\package\install.ps1") -Destination $OutputDirectory
Copy-Item -LiteralPath (Join-Path $clientRoot "tools\package\uninstall.ps1") -Destination $OutputDirectory

if ($SignBinaries) {
    $certificate = Get-Item "Cert:\CurrentUser\My\$CertificateThumbprint"
    foreach ($name in $binaries) {
        $result = Set-AuthenticodeSignature -LiteralPath (Join-Path $OutputDirectory $name) `
                                            -Certificate $certificate -HashAlgorithm SHA256
        if ($result.Status -ne "Valid" -and $result.Status -ne "UnknownError") {
            throw "$name could not be signed ($($result.Status))."
        }
    }
}

# --------------------------------------------------------------- the manifest

$files = New-Object System.Collections.Generic.List[object]
Get-ChildItem -LiteralPath $OutputDirectory -File |
    Sort-Object -Property Name -CaseSensitive |
    ForEach-Object {
        $entry = [ordered]@{
            path   = $_.Name
            bytes  = [long]$_.Length
            sha256 = Get-Sha256 $_.FullName
        }
        $files.Add($entry)
    }

$manifest = [ordered]@{
    manifestVersion = 1
    product         = "phpvpn-client"
    releaseVersion  = $releaseVersion
    architecture    = $architecture
    ipcVersion      = 1
    wintunVersion   = $driverVersion
    signingIdentity = $CertificateThumbprint.ToUpperInvariant()
    files           = $files.ToArray()
}

$signingResult = Write-SignedReleaseManifest -OutputDirectory $OutputDirectory -Manifest $manifest `
                                             -CertificateThumbprint $CertificateThumbprint
$signingSubject = $signingResult.Subject
$usedCngCompatibilityPath = $signingResult.UsedCngCompatibilityPath

if ($usedCngCompatibilityPath) {
    Write-Host "Signing key  : used the Windows PowerShell CNG compatibility path"
}

Write-Host ""
Write-Host "Package       : $OutputDirectory"
Write-Host "Release       : $releaseVersion ($architecture)"
Write-Host "Wintun        : $driverVersion"
Write-Host "Signed by     : $signingSubject"
Write-Host "Files         : $($files.Count) plus the manifest, the signature and the certificate"
