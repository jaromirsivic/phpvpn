<#
.SYNOPSIS
    Stages the Linux client folder (dist/client/Linux/x64) and signs its
    release manifest.

.DESCRIPTION
    plan-v001 section 4.13. The input is one package build directory from
    client/tools/linux/build-packages-in-container.sh (by convention
    work/linux-packages/<id>) with the RPM, the DEB, SHA256SUMS and
    build-provenance.json. The result is a complete folder with the packages,
    install.sh, uninstall.sh, README.md and a release manifest in the same
    canonical form and with the same signing mechanism and certificate as the
    Windows package of client/tools/make-dist.ps1 (tools/distribution-common.ps1).

    The manifest adds the Linux fields platform, packageRelease, distributions
    (with the minimum version, the distribution release the package was built
    on), sourceCommit and sourceDirty. When the build directory holds
    phpvpn-tray packages, they are staged too.
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

# Like make-dist.ps1: never erase the full dist or a source directory.
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$allowed = @((Join-Path $repositoryRoot 'work'), (Join-Path $repositoryRoot 'dist'))
$container = $allowed | Where-Object { $OutputDirectory.StartsWith($_ + '\', [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
if (-not $container) { throw 'Linux client OutputDirectory must be a child of repository work or dist. Use tools/make-distribution.ps1 for the complete distribution.' }
$OutputDirectory = Assert-DistributionPath $OutputDirectory $container -Tree

$packages = (Resolve-Path -LiteralPath $PackagesDirectory).Path
$distSource = Join-Path $clientRoot "packaging\linux\dist"

# ------------------------------------------------------------ package inputs

$clientVersion = $null
foreach ($line in [IO.File]::ReadAllLines((Join-Path $clientRoot "CMakeLists.txt"))) {
    if ($line -match '^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$') { $clientVersion = $Matches[1]; break }
}
if (-not $clientVersion) { throw "The client version was not found in client/CMakeLists.txt." }

$sumsPath = Join-Path $packages "SHA256SUMS"
$provenancePath = Join-Path $packages "build-provenance.json"
foreach ($required in @($sumsPath, $provenancePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "The package build is incomplete, $(Split-Path $required -Leaf) is missing in $packages."
    }
}

$sums = @{}
foreach ($line in [IO.File]::ReadAllLines($sumsPath)) {
    if ($line -eq '') { continue }
    if ($line -notmatch '^([0-9a-f]{64}) [ *]([A-Za-z0-9._+-]+)$') { throw "SHA256SUMS holds a malformed line: $line" }
    $sums[$Matches[2]] = $Matches[1]
}
foreach ($name in $sums.Keys) {
    $path = Join-Path $packages $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "$name of SHA256SUMS is missing." }
    if ((Get-Sha256 $path) -ne $sums[$name]) { throw "$name does not match SHA256SUMS." }
}

$patterns = [ordered]@{
    rpm     = '^phpvpn-(\d+\.\d+\.\d+)-(\d+)\.x86_64\.rpm$'
    deb     = '^phpvpn_(\d+\.\d+\.\d+)-(\d+)_amd64\.deb$'
    trayRpm = '^phpvpn-tray-(\d+\.\d+\.\d+)-(\d+)\.x86_64\.rpm$'
    trayDeb = '^phpvpn-tray_(\d+\.\d+\.\d+)-(\d+)_amd64\.deb$'
}
$selected = @{}
$packageRelease = $null
foreach ($file in Get-ChildItem -LiteralPath $packages -File | Where-Object { $_.Extension -in @('.rpm', '.deb') }) {
    if (-not $sums.ContainsKey($file.Name)) { throw "$($file.Name) is not listed in SHA256SUMS." }
    $kind = $patterns.Keys | Where-Object { $file.Name -cmatch $patterns[$_] } | Select-Object -First 1
    if (-not $kind) { throw "$($file.Name) is not a phpvpn package of this build." }
    $null = $file.Name -cmatch $patterns[$kind]
    if ($Matches[1] -ne $clientVersion) { throw "$($file.Name) is not version $clientVersion." }
    if ($null -eq $packageRelease) { $packageRelease = $Matches[2] }
    elseif ($Matches[2] -ne $packageRelease) { throw "The packages do not share one release ($($file.Name))." }
    if ($selected.ContainsKey($kind)) { throw "The build holds more than one $kind package." }
    $selected[$kind] = $file.Name
}
foreach ($kind in @('rpm', 'deb')) {
    if (-not $selected.ContainsKey($kind)) { throw "The build holds no phpvpn $kind package." }
}
$withTray = $selected.ContainsKey('trayRpm') -or $selected.ContainsKey('trayDeb')
if ($withTray -and -not ($selected.ContainsKey('trayRpm') -and $selected.ContainsKey('trayDeb'))) {
    throw "The build holds a phpvpn-tray package for only one distribution."
}

$provenance = [IO.File]::ReadAllText($provenancePath) | ConvertFrom-Json
if ($provenance.formatVersion -ne 1 -or $provenance.product -ne 'phpvpn') { throw "build-provenance.json is not a phpvpn provenance." }
if ($provenance.version -ne $clientVersion) { throw "build-provenance.json names version $($provenance.version), the client is $clientVersion." }
if ([string]$provenance.packageRelease -ne $packageRelease) { throw "build-provenance.json names release $($provenance.packageRelease), the packages are $packageRelease." }
if ($provenance.sourceCommit -notmatch '^[0-9a-f]{40}$' -or $provenance.sourceDirty -isnot [bool]) {
    throw "build-provenance.json has no valid source commit and dirty flag."
}
$minimumVersions = [ordered]@{}
foreach ($build in $provenance.builds) {
    $id = $build.distribution.id
    if ($id -notin @('fedora', 'ubuntu')) { throw "build-provenance.json names the unsupported distribution $id." }
    if ($minimumVersions.Contains($id)) { throw "build-provenance.json holds two $id builds." }
    if ($build.sourceCommit -ne $provenance.sourceCommit) { throw "The $id build is from another source commit." }
    $minimumVersions[$id] = [string]$build.distribution.versionId
    foreach ($entry in $build.packages) {
        if (-not $sums.ContainsKey($entry.file) -or $sums[$entry.file] -ne $entry.sha256) {
            throw "The provenance of $($entry.file) does not match SHA256SUMS."
        }
    }
}
foreach ($id in @('fedora', 'ubuntu')) {
    if (-not $minimumVersions.Contains($id)) { throw "build-provenance.json holds no $id build." }
}

$checkoutCommit = $null
try { $checkoutCommit = (& git -C $repositoryRoot rev-parse HEAD 2>$null) } catch { $checkoutCommit = $null }
if ($provenance.sourceDirty) {
    Write-Warning "The packages were built from a modified source tree (sourceDirty=true)."
}
if ($checkoutCommit -and $checkoutCommit -ne $provenance.sourceCommit) {
    Write-Warning "The packages were built from $($provenance.sourceCommit), the checkout is $checkoutCommit."
}

# The installer entry points are POSIX shell scripts; a CR or a byte order mark
# would break them on the target system.
foreach ($name in @('install.sh', 'uninstall.sh', 'README.md')) {
    $bytes = [IO.File]::ReadAllBytes((Join-Path $distSource $name))
    if ($bytes -contains 13 -or ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
        throw "$name must be UTF-8 without a byte order mark and with LF line endings."
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

foreach ($name in $selected.Values) {
    Copy-Item -LiteralPath (Join-Path $packages $name) -Destination $OutputDirectory
    if ((Get-Sha256 (Join-Path $OutputDirectory $name)) -ne $sums[$name]) { throw "$name changed while it was copied." }
}
foreach ($name in @('install.sh', 'uninstall.sh', 'README.md')) {
    Copy-Item -LiteralPath (Join-Path $distSource $name) -Destination $OutputDirectory
}

Write-PinnedInstaller -Source (Join-Path $distSource "install.sh") -Destination (Join-Path $OutputDirectory "install.sh") -Platform Linux -Certificate $signingCertificate

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

$distributions = New-Object System.Collections.Generic.List[object]
foreach ($id in $minimumVersions.Keys) {
    $distributions.Add([ordered]@{ id = $id; minimumVersion = $minimumVersions[$id] })
}

$manifest = [ordered]@{
    manifestVersion = 1
    product         = "phpvpn-client"
    platform        = "linux"
    releaseVersion  = $clientVersion
    packageRelease  = $packageRelease
    architecture    = "x64"
    ipcVersion      = 1
    distributions   = $distributions.ToArray()
    sourceCommit    = $provenance.sourceCommit
    sourceDirty     = [bool]$provenance.sourceDirty
    signingIdentity = $CertificateThumbprint.ToUpperInvariant()
    files           = $files.ToArray()
}

$signingResult = Write-SignedReleaseManifest -OutputDirectory $OutputDirectory -Manifest $manifest `
                                             -CertificateThumbprint $CertificateThumbprint
if ($signingResult.UsedCngCompatibilityPath) {
    Write-Host "Signing key  : used the Windows PowerShell CNG compatibility path"
}

Write-Host ""
Write-Host "Linux folder  : $OutputDirectory"
Write-Host "Release       : $clientVersion-$packageRelease (x64)"
Write-Host "Distributions : $(($minimumVersions.Keys | ForEach-Object { "$_ $($minimumVersions[$_])+" }) -join ', ')"
Write-Host "Source        : $($provenance.sourceCommit)$(if ($provenance.sourceDirty) { ' (dirty)' })"
Write-Host "Tray          : $(if ($withTray) { 'included' } else { 'not included (headless only)' })"
Write-Host "Signed by     : $($signingResult.Subject)"
Write-Host "Files         : $($files.Count) plus the manifest, the signature and the certificate"
