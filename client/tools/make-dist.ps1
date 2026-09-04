<#
.SYNOPSIS
    Builds the distribution folder and the signed release manifest.

.DESCRIPTION
    plan_client.md chapters 3.5, 20.1 and 20.2. The result is a complete,
    handover ready folder that contains the three executables, the unrenamed
    architecture specific wintun.dll, both upstream licences, the install and
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
    [string]$OutputDirectory,

    # Pinned signing identity. The development certificate is the default so a
    # local package can be produced without a release key.
    [string]$CertificateThumbprint = "7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8",

    # Also applies an Authenticode signature to the three executables.
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
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repositoryRoot "dist"
}

$appsDirectory = Join-Path (Resolve-Path $BuildDirectory).Path "apps"

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

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

function ConvertTo-CanonicalJson {
    # A tiny deterministic writer. The built in ConvertTo-Json reorders nothing
    # but it does insert whitespace and escapes that would change the signed
    # bytes between PowerShell versions.
    param($Value)

    if ($null -eq $Value) { return "null" }
    if ($Value -is [bool]) { return $(if ($Value) { "true" } else { "false" }) }
    if ($Value -is [int] -or $Value -is [long]) { return $Value.ToString([cultureinfo]::InvariantCulture) }
    if ($Value -is [string]) {
        $builder = New-Object System.Text.StringBuilder
        [void]$builder.Append('"')
        foreach ($character in $Value.ToCharArray()) {
            switch ($character) {
                '"'  { [void]$builder.Append('\"'); continue }
                '\'  { [void]$builder.Append('\\'); continue }
                "`n" { [void]$builder.Append('\n'); continue }
                "`r" { [void]$builder.Append('\r'); continue }
                "`t" { [void]$builder.Append('\t'); continue }
                default {
                    if ([int]$character -lt 0x20) {
                        [void]$builder.AppendFormat('\u{0:x4}', [int]$character)
                    } else {
                        [void]$builder.Append($character)
                    }
                }
            }
        }
        [void]$builder.Append('"')
        return $builder.ToString()
    }
    if ($Value -is [System.Collections.IList]) {
        $items = foreach ($item in $Value) { ConvertTo-CanonicalJson $item }
        return "[" + ($items -join ",") + "]"
    }
    if ($Value -is [System.Collections.Specialized.OrderedDictionary]) {
        $items = foreach ($key in $Value.Keys) {
            (ConvertTo-CanonicalJson ([string]$key)) + ":" + (ConvertTo-CanonicalJson $Value[$key])
        }
        return "{" + ($items -join ",") + "}"
    }
    throw "The canonical writer does not support $($Value.GetType().FullName)."
}

# ---------------------------------------------------------------- build inputs

$binaries = @("phpvpn-service.exe", "phpvpn-tray.exe", "phpvpn.exe")
foreach ($name in $binaries + @("wintun.dll")) {
    $path = Join-Path $appsDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "The build is incomplete, $path is missing. Build the release preset first."
    }
}

$architecture = Get-PeMachine (Join-Path $appsDirectory "phpvpn-service.exe")
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
    Get-ChildItem -LiteralPath $OutputDirectory -Force | Remove-Item -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

foreach ($name in $binaries + @("wintun.dll")) {
    Copy-Item -LiteralPath (Join-Path $appsDirectory $name) -Destination $OutputDirectory
}

Copy-Item -LiteralPath (Join-Path $clientRoot "third_party\wintun\LICENSE.txt") `
          -Destination (Join-Path $OutputDirectory "LICENSE-wintun.txt")
Copy-Item -LiteralPath (Join-Path $clientRoot "assets\icons\LICENSE-material-symbols.txt") `
          -Destination $OutputDirectory
Copy-Item -LiteralPath (Join-Path $clientRoot "assets\icons\NOTICE-material-symbols.txt") `
          -Destination $OutputDirectory
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

$canonical = ConvertTo-CanonicalJson $manifest
$manifestBytes = [System.Text.Encoding]::UTF8.GetBytes($canonical)
[System.IO.File]::WriteAllBytes((Join-Path $OutputDirectory "release-manifest.json"), $manifestBytes)

$certificate = Get-Item "Cert:\CurrentUser\My\$CertificateThumbprint" -ErrorAction SilentlyContinue
if ($null -eq $certificate) {
    throw "The signing certificate $CertificateThumbprint is not present in Cert:\CurrentUser\My."
}
if (-not $certificate.HasPrivateKey) {
    throw "The signing certificate $CertificateThumbprint has no usable private key."
}

$privateKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
if ($null -eq $privateKey) {
    throw "Only an RSA signing certificate is supported."
}
$signature = $privateKey.SignData($manifestBytes,
                                  [System.Security.Cryptography.HashAlgorithmName]::SHA256,
                                  [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)

$detached = [ordered]@{
    algorithm   = "RSA-PKCS1-SHA256"
    thumbprint  = $certificate.Thumbprint.ToUpperInvariant()
    subject     = $certificate.Subject
    signature   = [Convert]::ToBase64String($signature)
}
$detachedBytes = [System.Text.Encoding]::UTF8.GetBytes((ConvertTo-CanonicalJson $detached))
[System.IO.File]::WriteAllBytes((Join-Path $OutputDirectory "release-manifest.sig"), $detachedBytes)

# The public certificate travels with the package so the installer can pin the
# identity without contacting anything.
[System.IO.File]::WriteAllBytes((Join-Path $OutputDirectory "release-signing.cer"),
                                $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))

Write-Host ""
Write-Host "Package       : $OutputDirectory"
Write-Host "Release       : $releaseVersion ($architecture)"
Write-Host "Wintun        : $driverVersion"
Write-Host "Signed by     : $($certificate.Subject)"
Write-Host "Files         : $($files.Count) plus the manifest, the signature and the certificate"
