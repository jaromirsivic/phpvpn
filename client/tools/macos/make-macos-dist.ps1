<#
.SYNOPSIS
    Stages the macOS client folder (dist/client/macOS/arm64) and signs its
    release manifest.

.DESCRIPTION
    plan-macos-v001 step 8, the counterpart of client/tools/linux/make-linux-dist.ps1.
    The input is one package build directory of client/tools/macos/package-pkg.sh,
    by convention the downloaded GitHub Actions artifact phpvpn-macos-arm64 in
    work/macos-packages/<id>, with phpvpn-<version>-arm64.pkg, SHA256SUMS and
    build-provenance.json (README.md, install.sh, uninstall.sh and logs/ of the
    artifact are ignored). The result is a complete folder with the package,
    install.sh (with the pinned signing key), uninstall.sh, README.md and a
    release manifest in the same canonical form and with the same signing
    mechanism and certificate as the Windows and Linux folders
    (tools/distribution-common.ps1).

    The manifest adds the macOS fields platform, architecture arm64,
    minimumOsVersion, sourceCommit, sourceDirty, builtOn and xcode.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PackagesDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,

    # Overrides signing.certificateThumbprint in the development configuration.
    [string]$CertificateThumbprint = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$clientRoot = (Resolve-Path (Join-Path $scriptRoot "..\..")).Path
$repositoryRoot = (Resolve-Path (Join-Path $clientRoot "..")).Path
. (Join-Path $repositoryRoot 'tools/distribution-common.ps1')
$signingCertificate = Get-DistributionSigningCertificate -Thumbprint $CertificateThumbprint -RequirePrivateKey
$CertificateThumbprint = $signingCertificate.Thumbprint

# Like make-linux-dist.ps1: never erase the full dist or a source directory.
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = @((Join-Path $repositoryRoot 'work'), (Join-Path $repositoryRoot 'dist'))
$container = $allowed | Where-Object { $OutputDirectory.StartsWith($_ + '\', [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
if (-not $container) { throw 'macOS client OutputDirectory must be a child of repository work or dist. Use tools/make-distribution.ps1 for the complete distribution.' }
$OutputDirectory = Assert-DistributionPath $OutputDirectory $container -Tree

# ------------------------------------------------------------ package inputs

$clientVersion = Get-ClientVersion $repositoryRoot
$build = Read-MacosPackageBuild -Directory $PackagesDirectory -ClientVersion $clientVersion
$provenance = $build.Provenance
$minimumOsVersion = Get-MacosMinimumOsVersion

$checkoutCommit = $null
try { $checkoutCommit = (& git -C $repositoryRoot rev-parse HEAD 2>$null) } catch { $checkoutCommit = $null }
if ($provenance.sourceDirty) {
    Write-Warning "The macOS package was built from a modified source tree (sourceDirty=true)."
}
if ($checkoutCommit -and $checkoutCommit -ne $provenance.sourceCommit) {
    Write-Warning "The macOS package was built from $($provenance.sourceCommit), the checkout is $checkoutCommit."
}

# The installer entry points are POSIX shell scripts; a CR or a byte order mark
# would break them on the target system.
$sources = Get-MacosClientSources
foreach ($entry in $sources.GetEnumerator()) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $repositoryRoot $entry.Value))
    if ($bytes -contains 13 -or ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
        throw "$($entry.Value) must be UTF-8 without a byte order mark and with LF line endings."
    }
}

# ------------------------------------------------------------ folder assembly

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

Copy-Item -LiteralPath (Join-Path $build.Directory $build.Package) -Destination $OutputDirectory
if ((Get-Sha256 (Join-Path $OutputDirectory $build.Package)) -ne $build.Sha256) { throw "$($build.Package) changed while it was copied." }
foreach ($entry in $sources.GetEnumerator()) {
    if ($entry.Key -eq 'install.sh') {
        Write-PinnedInstaller -Source (Join-Path $repositoryRoot $entry.Value) -Destination (Join-Path $OutputDirectory $entry.Key) `
            -Platform macOS -Certificate $signingCertificate
    } else {
        Copy-Item -LiteralPath (Join-Path $repositoryRoot $entry.Value) -Destination (Join-Path $OutputDirectory $entry.Key)
    }
}

# --------------------------------------------------------------- the manifest

$names = [string[]]@(Get-ChildItem -LiteralPath $OutputDirectory -File | ForEach-Object { $_.Name })
[Array]::Sort($names, [StringComparer]::Ordinal)
$files = New-Object System.Collections.Generic.List[object]
foreach ($name in $names) {
    $path = Join-Path $OutputDirectory $name
    $files.Add([ordered]@{
        path   = $name
        bytes  = [long](Get-Item -LiteralPath $path).Length
        sha256 = Get-Sha256 $path
    })
}

$manifest = [ordered]@{
    manifestVersion  = 1
    product          = "phpvpn-client"
    platform         = "macos"
    releaseVersion   = $clientVersion
    architecture     = "arm64"
    ipcVersion       = 1
    minimumOsVersion = $minimumOsVersion
    sourceCommit     = [string]$provenance.sourceCommit
    sourceDirty      = [bool]$provenance.sourceDirty
    builtOn          = [string]$provenance.builtOn
    xcode            = [string]$provenance.xcode
    signingIdentity  = $CertificateThumbprint.ToUpperInvariant()
    files            = $files.ToArray()
}

$signingResult = Write-SignedReleaseManifest -OutputDirectory $OutputDirectory -Manifest $manifest `
                                             -CertificateThumbprint $CertificateThumbprint
if ($signingResult.UsedCngCompatibilityPath) {
    Write-Host "Signing key  : used the Windows PowerShell CNG compatibility path"
}

Write-Host ""
Write-Host "macOS folder  : $OutputDirectory"
Write-Host "Release       : $clientVersion (arm64, macOS $minimumOsVersion or newer)"
Write-Host "Built on      : macOS $($provenance.builtOn), $($provenance.xcode)"
Write-Host "Source        : $($provenance.sourceCommit)$(if ($provenance.sourceDirty) { ' (dirty)' })"
Write-Host "Tray          : $(if ($provenance.trayIncluded) { 'included' } else { 'not included (headless build)' })"
Write-Host "Signed by     : $($signingResult.Subject)"
Write-Host "Files         : $($files.Count) plus the manifest, the signature and the certificate"
