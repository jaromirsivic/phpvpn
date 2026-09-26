<# .SYNOPSIS
Read-only verification of the complete server and client distribution.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$DistributionDirectory,
    [string]$IconResourceTest = (Join-Path $PSScriptRoot '../work/build/release/tests/phpvpn-icon-resource-tests.exe'),
    [switch]$AllowLocalRuntime
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'distribution-common.ps1')
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$tree = (Resolve-Path -LiteralPath $DistributionDirectory).Path
$null = Assert-DistributionPath -Path $tree -Within ([IO.Path]::GetDirectoryName($tree)) -Tree
function Assert-ShellScript([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes -contains 13 -or $bytes[0] -ne 35) { throw "Shell script must use LF and no BOM: $Path" }
}
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
    if ($entry.Key.EndsWith('.sh')) { Assert-ShellScript $copy }
}
$name = 'client/macOS/README.md'
if ([IO.File]::ReadAllText((Join-Path $tree $name)) -cne (Get-UnsupportedClientReadme 'macOS')) {
    throw 'Incorrect unsupported-platform README: macOS'
}
$expected.Add($name)
$client = Join-Path $tree 'client/Windows/x64'
$clientNames = @('phpvpn-service.exe','phpvpn-tray.exe','phpvpn.exe','install-phpvpn.exe','uninstall-phpvpn.exe',
    'wintun.dll','LICENSE-wintun.txt','LICENSE-material-symbols.txt','NOTICE-material-symbols.txt',
    'install.bat','install.ps1','uninstall.bat','uninstall.ps1','release-manifest.json','release-manifest.sig','release-signing.cer')
foreach ($name in $clientNames) { $expected.Add("client/Windows/x64/$name") }

# Linux x64: unchanged installer, README and licence copies, exactly four packages of the
# client release (phpvpn and phpvpn-tray, RPM and DEB) and the signed release manifest.
$linux = Join-Path $tree 'client/Linux/x64'
$clientVersion = Get-ClientVersion $repository
$linuxNames = New-Object 'Collections.Generic.List[string]'
foreach ($entry in (Get-LinuxClientSources).GetEnumerator()) {
    $copy = Join-Path $linux $entry.Key
    if (-not (Test-Path -LiteralPath $copy -PathType Leaf)) { throw "Missing distribution file: client/Linux/x64/$($entry.Key)" }
    if ((Get-FileHash -LiteralPath (Join-Path $repository $entry.Value)).Hash -ne (Get-FileHash -LiteralPath $copy).Hash) {
        throw "Stale distribution source: client/Linux/x64/$($entry.Key)"
    }
    if ($entry.Key.EndsWith('.sh')) { Assert-ShellScript $copy }
    $linuxNames.Add($entry.Key)
}
$linuxPatterns = Get-LinuxPackagePatterns
$linuxRelease = $null
foreach ($kind in $linuxPatterns.Keys) {
    $matched = @(Get-ChildItem -LiteralPath $linux -File -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -cmatch $linuxPatterns[$kind] })
    if ($matched.Count -ne 1) { throw "Linux client requires exactly one $kind package, found $($matched.Count)." }
    $null = $matched[0].Name -cmatch $linuxPatterns[$kind]
    if ($Matches[1] -ne $clientVersion) { throw "Linux package $($matched[0].Name) is not client version $clientVersion." }
    if ($null -eq $linuxRelease) { $linuxRelease = $Matches[2] }
    elseif ($Matches[2] -ne $linuxRelease) { throw "Linux packages do not share one release: $($matched[0].Name)" }
    # RPM lead magic, Debian ar archive magic.
    $magic = if ($kind -like '*rpm') { [byte[]](0xED,0xAB,0xEE,0xDB) } else { [Text.Encoding]::ASCII.GetBytes("!<arch>`n") }
    $stream = [IO.File]::OpenRead($matched[0].FullName)
    try { $head = New-Object byte[] $magic.Length; $read = $stream.Read($head, 0, $head.Length) } finally { $stream.Dispose() }
    if ($read -ne $magic.Length -or (Compare-Object $magic $head -SyncWindow 0)) { throw "Linux package has an invalid format: $($matched[0].Name)" }
    $linuxNames.Add($matched[0].Name)
}
foreach ($name in $linuxNames) { $expected.Add("client/Linux/x64/$name") }
foreach ($name in @('release-manifest.json','release-manifest.sig','release-signing.cer')) { $expected.Add("client/Linux/x64/$name") }

$actual = @(Get-ChildItem -LiteralPath $tree -File -Recurse -Force | ForEach-Object {
    $_.FullName.Substring($tree.Length + 1).Replace('\','/')
} | Where-Object {
    -not ($AllowLocalRuntime -and (Test-DistributionRuntimePath $_))
})
if (Compare-Object $expected.ToArray() $actual -CaseSensitive) { throw 'Distribution contains missing or unexpected files (including hidden runtime files).' }
$directories = @('server','server/PHP','server/Python','server/Node.js','client','client/Windows','client/Windows/x64','client/macOS','client/Linux','client/Linux/x64')
$actualDirs = @(Get-ChildItem -LiteralPath $tree -Directory -Recurse -Force | ForEach-Object {
    $_.FullName.Substring($tree.Length + 1).Replace('\','/')
} | Where-Object {
    -not ($AllowLocalRuntime -and (Test-DistributionRuntimePath $_))
})
if (Compare-Object $directories $actualDirs -CaseSensitive) { throw 'Distribution directory set is incorrect.' }
$manifest = Get-Content -LiteralPath (Join-Path $client 'release-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.architecture -cne 'x64') { throw 'Complete distribution requires a Windows x64 client.' }

# Linux manifest: canonical single-line UTF-8, fields, and size and SHA-256 of every file.
$linuxManifestBytes = [IO.File]::ReadAllBytes((Join-Path $linux 'release-manifest.json'))
if (($linuxManifestBytes.Length -ge 3 -and $linuxManifestBytes[0] -eq 0xEF) -or
    $linuxManifestBytes -contains 10 -or $linuxManifestBytes -contains 13) { throw 'Linux manifest is not in its canonical single-line form.' }
$linuxManifest = [Text.Encoding]::UTF8.GetString($linuxManifestBytes) | ConvertFrom-Json
if ($linuxManifest.manifestVersion -ne 1 -or $linuxManifest.product -cne 'phpvpn-client' -or
    $linuxManifest.platform -cne 'linux' -or $linuxManifest.architecture -cne 'x64') { throw 'Linux manifest is not an x64 Linux phpvpn client manifest.' }
if ($linuxManifest.releaseVersion -cne $clientVersion -or $linuxManifest.releaseVersion -cne $manifest.releaseVersion) {
    throw 'Linux manifest release version differs from the client and the Windows package.'
}
if ([string]$linuxManifest.packageRelease -cne $linuxRelease) { throw 'Linux manifest package release differs from the packages.' }
if ($linuxManifest.ipcVersion -ne $manifest.ipcVersion) { throw 'Linux and Windows packages declare different IPC versions.' }
$distributionIds = @($linuxManifest.distributions | ForEach-Object {
    if ([string]$_.minimumVersion -notmatch '^\d+(\.\d+)*$') { throw "Linux manifest has an invalid minimum version for $($_.id)." }
    $_.id
})
if (Compare-Object @('fedora','ubuntu') $distributionIds -CaseSensitive) { throw 'Linux manifest must name exactly Fedora and Ubuntu.' }
if ($linuxManifest.sourceCommit -notmatch '^[0-9a-f]{40}$' -or $linuxManifest.sourceDirty -isnot [bool]) {
    throw 'Linux manifest has no valid source commit and dirty flag.'
}
$listed = @($linuxManifest.files | ForEach-Object { $_.path })
if (Compare-Object $linuxNames.ToArray() $listed -CaseSensitive) { throw 'Linux client file set differs from its manifest.' }
foreach ($entry in $linuxManifest.files) {
    $path = Join-Path $linux $entry.path
    if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Linux manifest mismatch: $($entry.path)" }
}

# Linux signature: the same certificate as the Windows package (pinned by install.ps1), the
# signing key pinned in install.sh, and RSA PKCS#1 v1.5 SHA-256 over the exact manifest bytes.
function Get-DerElement([byte[]]$Data, [int]$Offset) {
    if ($Offset + 2 -gt $Data.Length) { throw 'Truncated DER element.' }
    $length = [int]$Data[$Offset + 1]
    $content = $Offset + 2
    if ($length -ge 0x80) {
        $count = $length -band 0x7F
        if ($count -lt 1 -or $count -gt 3) { throw 'Unsupported DER length.' }
        $length = 0
        for ($index = 0; $index -lt $count; ++$index) { $length = ($length -shl 8) -bor $Data[$content]; ++$content }
    }
    if ($content + $length -gt $Data.Length) { throw 'Truncated DER element.' }
    return [pscustomobject]@{ Tag = $Data[$Offset]; Offset = $Offset; Content = $content; End = $content + $length }
}
function Get-SubjectPublicKeyInfoSha256([byte[]]$Certificate) {
    $tbs = Get-DerElement $Certificate (Get-DerElement $Certificate 0).Content
    $field = Get-DerElement $Certificate $tbs.Content
    if ($field.Tag -eq 0xA0) { $field = Get-DerElement $Certificate $field.End } # explicit version
    # serialNumber, signature, issuer, validity and subject precede subjectPublicKeyInfo.
    foreach ($tag in @(0x02, 0x30, 0x30, 0x30, 0x30)) {
        if ($field.Tag -ne $tag) { throw 'Unexpected certificate structure.' }
        $field = Get-DerElement $Certificate $field.End
    }
    if ($field.Tag -ne 0x30) { throw 'Unexpected certificate structure.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash($Certificate, $field.Offset, $field.End - $field.Offset) } finally { $sha.Dispose() }
    return (-join ($digest | ForEach-Object { $_.ToString('x2') }))
}
$linuxCertificateBytes = [IO.File]::ReadAllBytes((Join-Path $linux 'release-signing.cer'))
$windowsCertificateBytes = [IO.File]::ReadAllBytes((Join-Path $client 'release-signing.cer'))
if (-not (Test-ByteArrayEqual $linuxCertificateBytes $windowsCertificateBytes)) { throw 'Linux manifest is not signed with the certificate of the Windows package.' }
$linuxCertificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2 (, $linuxCertificateBytes)
try {
    $thumbprint = $linuxCertificate.Thumbprint.ToUpperInvariant()
    $detached = [IO.File]::ReadAllText((Join-Path $linux 'release-manifest.sig')) | ConvertFrom-Json
    if ($detached.algorithm -cne 'RSA-PKCS1-SHA256') { throw "Linux signature algorithm $($detached.algorithm) is not supported." }
    if ($detached.thumbprint.ToUpperInvariant() -ne $thumbprint -or $linuxManifest.signingIdentity.ToUpperInvariant() -ne $thumbprint -or
        $manifest.signingIdentity.ToUpperInvariant() -ne $thumbprint) { throw 'Linux signature or manifest names another signing identity.' }
    $pinned = [regex]::Match([IO.File]::ReadAllText((Join-Path $linux 'install.sh')), '(?m)^PINNED_SPKI_SHA256=([0-9a-f]{64})$')
    if (-not $pinned.Success) { throw 'install.sh holds no pinned signing key.' }
    if ((Get-SubjectPublicKeyInfoSha256 $linuxCertificateBytes) -cne $pinned.Groups[1].Value) { throw 'Linux signing key is not the key pinned in install.sh.' }
    $publicKey = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($linuxCertificate)
    try {
        $verified = $publicKey.VerifyData($linuxManifestBytes, [Convert]::FromBase64String($detached.signature),
            [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    } finally { $publicKey.Dispose() }
    if (-not $verified) { throw 'Linux manifest signature did not verify.' }
} finally { $linuxCertificate.Dispose() }
if ($linuxManifest.sourceDirty) { Write-Warning "Linux packages were built from a modified source tree ($($linuxManifest.sourceCommit)); not a release build." }

& (Join-Path $repository 'client/tools/verify-package.ps1') -PackageDirectory $client -IconResourceTest $IconResourceTest
Write-Output "Linux x64: release $($linuxManifest.releaseVersion)-$($linuxManifest.packageRelease), source $($linuxManifest.sourceCommit)$(if ($linuxManifest.sourceDirty) { ' (dirty)' }), signed by $thumbprint."
Write-Output 'PASS: complete distribution allowlist, source hashes, LF shell scripts, empty Node.js, macOS README, signed Linux folder and signed Windows package.'
if ($AllowLocalRuntime) { Write-Output 'Local Python .runtime and .venv are preserved and excluded from the distribution file set.' }
