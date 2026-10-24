# Run on Windows. Optional package inputs exercise both real packagers without installation.
[CmdletBinding()]
param([string]$BuildDirectory, [string]$LinuxPackagesDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../tools/distribution-common.ps1')
$repository = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work = Join-Path $repository 'work'
$fixture = Assert-DistributionPath (Join-Path $work ('signing-test-' + [guid]::NewGuid().ToString('N'))) $work
$oldConfig = $env:PHPVPN_CONFIG
$certificates = @()
function Expect-Rejected([scriptblock]$Action, [string]$Message) {
    try { & $Action } catch {
        if ($_.Exception.Message -notlike "*$Message*") { throw }
        return
    }
    throw "Expected rejection: $Message"
}
try {
    $null = New-Item -ItemType Directory -Path $fixture
    foreach ($id in 1,2) {
        $certificates += New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=phpvpn signing regression $id" `
            -KeyAlgorithm RSA -KeyLength 2048 -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy NonExportable
    }
    $env:PHPVPN_CONFIG = Join-Path $fixture 'config.json'
    [IO.File]::WriteAllText($env:PHPVPN_CONFIG, ('{"signing":{"certificateThumbprint":"' + $certificates[0].Thumbprint + '"}}'))
    if ((Get-DistributionSigningCertificate -RequirePrivateKey).Thumbprint -ne $certificates[0].Thumbprint) { throw 'Configuration was ignored.' }
    if ((Get-DistributionSigningCertificate -Thumbprint $certificates[1].Thumbprint).Thumbprint -ne $certificates[1].Thumbprint) { throw 'Explicit identity did not override configuration.' }
    Expect-Rejected { Get-DistributionSigningCertificate -Thumbprint '../invalid' } '40 hexadecimal'
    foreach ($platform in 'Windows','Linux') {
        $source = if ($platform -eq 'Windows') { Join-Path $repository 'client/tools/package/install.ps1' } else { Join-Path $repository 'client/packaging/linux/dist/install.sh' }
        $copy = Join-Path $fixture "$platform-installer"
        Write-PinnedInstaller -Source $source -Destination $copy -Platform $platform -Certificate $certificates[0]
        Assert-PinnedInstaller -Source $source -Copy $copy -Platform $platform -Certificate $certificates[0]
        Expect-Rejected { Assert-PinnedInstaller -Source $source -Copy $copy -Platform $platform -Certificate $certificates[1] } 'configured signing identity'
        [IO.File]::AppendAllText($copy, '# modified')
        Expect-Rejected { Assert-PinnedInstaller -Source $source -Copy $copy -Platform $platform -Certificate $certificates[0] } 'configured signing identity'
    }
    Write-Output 'PASS: configured identity, explicit override, invalid identity, foreign pin and altered installer'
    if ($BuildDirectory) {
        $windows = Join-Path $fixture 'windows'
        & (Join-Path $repository 'client/tools/make-dist.ps1') -BuildDirectory $BuildDirectory -OutputDirectory $windows
        & (Join-Path $repository 'client/tools/verify-package.ps1') -PackageDirectory $windows `
            -IconResourceTest (Join-Path $BuildDirectory 'tests/phpvpn-icon-resource-tests.exe')
        $certPath = Join-Path $windows 'release-signing.cer'
        $original = [IO.File]::ReadAllBytes($certPath)
        [IO.File]::WriteAllBytes($certPath, $certificates[1].RawData)
        $output = & powershell.exe -NoProfile -File (Join-Path $windows 'install.ps1') -VerifyOnly 2>&1
        if ($LASTEXITCODE -eq 0 -or ($output -join "`n") -notmatch 'pinned') { throw "Foreign Windows key was not rejected: $output" }
        [IO.File]::WriteAllBytes($certPath, $original)
        Write-Output 'PASS: Windows package with fresh signing key and foreign-key rejection'
    }
    if ($LinuxPackagesDirectory) {
        $linux = Join-Path $fixture 'linux'
        & (Join-Path $repository 'client/tools/linux/make-linux-dist.ps1') -PackagesDirectory $LinuxPackagesDirectory -OutputDirectory $linux
        $linuxPath = & wsl.exe -d Ubuntu --exec wslpath -a $linux.Replace('\','/')
        if ($LASTEXITCODE) { throw 'WSL path conversion failed.' }
        $linuxPath = $linuxPath.Trim()
        & wsl.exe -d Ubuntu --exec sh "$linuxPath/install.sh" --verify-only
        if ($LASTEXITCODE) { throw 'Linux verification with fresh signing key failed.' }
        $certPath = Join-Path $linux 'release-signing.cer'
        $original = [IO.File]::ReadAllBytes($certPath)
        [IO.File]::WriteAllBytes($certPath, $certificates[1].RawData)
        $ErrorActionPreference = 'Continue' # Windows PowerShell wraps expected native stderr in error records.
        $output = & wsl.exe -d Ubuntu --exec sh "$linuxPath/install.sh" --verify-only 2>&1
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($code -eq 0 -or ($output -join "`n") -notmatch 'not pinned') { throw "Foreign Linux key was not rejected: $output" }
        [IO.File]::WriteAllBytes($certPath, $original)
        $readme = Join-Path $linux 'README.md'
        [IO.File]::AppendAllText($readme, 'tampered')
        $ErrorActionPreference = 'Continue'
        $output = & wsl.exe -d Ubuntu --exec sh "$linuxPath/install.sh" --verify-only 2>&1
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($code -eq 0 -or ($output -join "`n") -notmatch 'recorded size') { throw "Tampered Linux payload was not rejected: $output" }
        Write-Output 'PASS: Linux package with fresh signing key, foreign-key rejection and tampered payload rejection'
    }
} finally {
    $env:PHPVPN_CONFIG = $oldConfig
    foreach ($certificate in $certificates) {
        Remove-Item -LiteralPath ("Cert:\CurrentUser\My\" + $certificate.Thumbprint) -DeleteKey
        $certificate.Dispose()
    }
    $safe = Assert-DistributionPath $fixture $work -Tree
    if (Test-Path -LiteralPath $safe) { Remove-Item -LiteralPath $safe -Recurse -Force }
}
