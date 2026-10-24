# Read-only verifier rejection tests against an isolated synthetic package.
# Never invokes production installation, a service or elevation.
$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('phpvpn-package-contract-' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $PSScriptRoot '../tools/package'
$verifier = Join-Path $PSScriptRoot '../tools/verify-package.ps1'
$required = @('phpvpn-service.exe', 'phpvpn-tray.exe', 'phpvpn.exe', 'install-phpvpn.exe', 'uninstall-phpvpn.exe', 'wintun.dll')
. (Join-Path $PSScriptRoot '../../tools/distribution-common.ps1')
$certificate = $null
try {
    $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=phpvpn package contract test' `
        -KeyAlgorithm RSA -KeyLength 2048 -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy NonExportable
    New-Item -ItemType Directory -Path $fixture | Out-Null
    foreach ($script in @('install.bat', 'install.ps1', 'uninstall.bat', 'uninstall.ps1')) {
        Copy-Item -LiteralPath (Join-Path $source $script) -Destination $fixture
    }
    Write-PinnedInstaller -Source (Join-Path $source 'install.ps1') -Destination (Join-Path $fixture 'install.ps1') -Platform Windows -Certificate $certificate
    foreach ($name in $required) { [IO.File]::WriteAllText((Join-Path $fixture $name), 'never executable') }
    foreach ($missing in @('install-phpvpn.exe', 'uninstall-phpvpn.exe')) {
        foreach ($omitManifestOnly in @($true, $false)) {
            $files = @(Get-ChildItem -LiteralPath $fixture -File | Where-Object { $_.Name -ne 'release-manifest.json' -and $_.Name -ne $missing } | ForEach-Object {
                @{ path=$_.Name; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash }
            })
            if (-not $omitManifestOnly) { Remove-Item -LiteralPath (Join-Path $fixture $missing) }
            @{ files=$files } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fixture 'release-manifest.json') -Encoding UTF8
            $rejected = $false
            try { & $verifier -PackageDirectory $fixture -CertificateThumbprint $certificate.Thumbprint } catch {
                if ($_.Exception.Message -notlike "Required package file is missing or duplicated: $missing*") { throw }
                $rejected = $true
            }
            if (-not $rejected) { throw "Verifier accepted absent launcher $missing" }
            [IO.File]::WriteAllText((Join-Path $fixture $missing), 'never executable')
        }
    }
    Write-Output 'PASS: both launchers are mandatory on disk and in the manifest, even if a manifest omits them consistently.'
} finally {
    if ($certificate) {
        Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $certificate.Thumbprint) -DeleteKey
        $certificate.Dispose()
    }
    # Only the unique temp fixture created above is removed.
    $resolved = [IO.Path]::GetFullPath($fixture)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notlike 'phpvpn-package-contract-*') { throw 'Unsafe fixture cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
