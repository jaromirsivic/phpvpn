<#
.SYNOPSIS
    Builds only the Windows client package and its signed release manifest.

.DESCRIPTION
    plan_client.md chapters 3.5, 20.1 and 20.2. The result is a complete,
    handover ready folder that contains the five executables, the unrenamed
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

function Test-IsEphemeralCngHandleException {
    param([System.Exception]$Exception)

    # .NET Framework wraps the RSACng constructor's exact ArgumentException in
    # MethodInvocationException. Match the stable exception contract rather
    # than treating an arbitrary private-key failure as permission to fall back.
    $candidate = $Exception
    while ($null -ne $candidate) {
        if ($candidate -is [System.ArgumentException] -and
            $candidate.HResult -eq -2147024809 -and
            $candidate.ParamName -eq "keyHandleOpenOptions") {
            return $true
        }
        $candidate = $candidate.InnerException
    }
    return $false
}

function Get-CertificateCngBinding {
    param([System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    if (-not ("PhpVpn.CngCertificateBindingReader" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;

namespace PhpVpn
{
    public sealed class CngCertificateBinding
    {
        public string ContainerName { get; private set; }
        public string ProviderName { get; private set; }
        public bool MachineKey { get; private set; }
        public bool Silent { get; private set; }

        internal CngCertificateBinding(string containerName, string providerName,
                                       bool machineKey, bool silent)
        {
            ContainerName = containerName;
            ProviderName = providerName;
            MachineKey = machineKey;
            Silent = silent;
        }
    }

    public static class CngCertificateBindingReader
    {
        private const uint CertKeyProvInfoProperty = 2;
        private const uint CryptMachineKeyset = 0x20;
        private const uint CryptSilent = 0x40;
        private const uint CertNCryptKeySpec = 0xffffffff;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct CryptKeyProvInfo
        {
            public IntPtr ContainerName;
            public IntPtr ProviderName;
            public uint ProviderType;
            public uint Flags;
            public uint ProviderParameterCount;
            public IntPtr ProviderParameters;
            public uint KeySpec;
        }

        [DllImport("crypt32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CertGetCertificateContextProperty(
            IntPtr certificateContext, uint propertyId, IntPtr data, ref uint dataSize);

        public static CngCertificateBinding Read(X509Certificate2 certificate)
        {
            if (certificate == null)
                throw new ArgumentNullException("certificate");

            uint dataSize = 0;
            if (!CertGetCertificateContextProperty(certificate.Handle,
                                                    CertKeyProvInfoProperty,
                                                    IntPtr.Zero,
                                                    ref dataSize))
                throw new Win32Exception(Marshal.GetLastWin32Error());

            if (dataSize < Marshal.SizeOf(typeof(CryptKeyProvInfo)))
                throw new InvalidOperationException("The certificate private-key binding is incomplete.");

            IntPtr data = Marshal.AllocHGlobal(checked((int)dataSize));
            try
            {
                if (!CertGetCertificateContextProperty(certificate.Handle,
                                                        CertKeyProvInfoProperty,
                                                        data,
                                                        ref dataSize))
                    throw new Win32Exception(Marshal.GetLastWin32Error());

                CryptKeyProvInfo info = (CryptKeyProvInfo)Marshal.PtrToStructure(
                    data, typeof(CryptKeyProvInfo));
                // ProviderType zero identifies a CNG KSP. Windows may persist
                // either the documented NCrypt marker or zero in KeySpec for a
                // certificate whose key is already identified by that KSP.
                if (info.ProviderType != 0 ||
                    (info.KeySpec != 0 && info.KeySpec != CertNCryptKeySpec))
                    throw new NotSupportedException("The certificate private key is not bound to a CNG key storage provider.");
                if (info.ContainerName == IntPtr.Zero || info.ProviderName == IntPtr.Zero)
                    throw new InvalidOperationException("The certificate CNG private-key binding is incomplete.");

                string containerName = Marshal.PtrToStringUni(info.ContainerName);
                string providerName = Marshal.PtrToStringUni(info.ProviderName);
                if (String.IsNullOrWhiteSpace(containerName) || String.IsNullOrWhiteSpace(providerName))
                    throw new InvalidOperationException("The certificate CNG private-key binding is incomplete.");

                return new CngCertificateBinding(
                    containerName,
                    providerName,
                    (info.Flags & CryptMachineKeyset) != 0,
                    (info.Flags & CryptSilent) != 0);
            }
            finally
            {
                Marshal.FreeHGlobal(data);
            }
        }
    }
}
"@
    }

    return [PhpVpn.CngCertificateBindingReader]::Read($Certificate)
}

function Test-ByteArrayEqual {
    param([byte[]]$Left, [byte[]]$Right)

    if ($null -eq $Left -or $null -eq $Right -or $Left.Length -ne $Right.Length) {
        return $false
    }
    $difference = 0
    for ($index = 0; $index -lt $Left.Length; ++$index) {
        $difference = $difference -bor ($Left[$index] -bxor $Right[$index])
    }
    return $difference -eq 0
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

$signaturePath = Join-Path $OutputDirectory "release-manifest.sig"
$certificatePath = Join-Path $OutputDirectory "release-signing.cer"
Remove-Item -LiteralPath $signaturePath, $certificatePath -Force -ErrorAction SilentlyContinue

$certificate = Get-Item "Cert:\CurrentUser\My\$CertificateThumbprint" -ErrorAction SilentlyContinue
if ($null -eq $certificate) {
    throw "The signing certificate $CertificateThumbprint is not present in Cert:\CurrentUser\My."
}
try {
    if (-not $certificate.HasPrivateKey) {
        throw "The signing certificate $CertificateThumbprint has no usable private key."
    }

    $publicKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
    if ($null -eq $publicKey) {
        throw "Only an RSA signing certificate is supported."
    }
    $privateKey = $null
    $cngKey = $null
    $usedCngCompatibilityPath = $false
    try {
        try {
            $privateKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
        } catch {
            if (-not (Test-IsEphemeralCngHandleException $_.Exception)) {
                throw "The signing certificate private key could not be opened through the standard RSA provider path."
            }

            try {
                $binding = Get-CertificateCngBinding $certificate
            } catch {
                throw "The signing certificate's CNG provider binding could not be obtained safely."
            }

            $openOptions = [System.Security.Cryptography.CngKeyOpenOptions]::None
            if ($binding.MachineKey) {
                $openOptions = $openOptions -bor [System.Security.Cryptography.CngKeyOpenOptions]::MachineKey
            }
            if ($binding.Silent) {
                $openOptions = $openOptions -bor [System.Security.Cryptography.CngKeyOpenOptions]::Silent
            }

            try {
                $provider = New-Object System.Security.Cryptography.CngProvider($binding.ProviderName)
                $cngKey = [System.Security.Cryptography.CngKey]::Open($binding.ContainerName,
                                                                     $provider,
                                                                     $openOptions)
                if ($cngKey.AlgorithmGroup -ne [System.Security.Cryptography.CngAlgorithmGroup]::Rsa) {
                    throw "not-rsa"
                }
                $privateKey = New-Object System.Security.Cryptography.RSACng($cngKey)
            } catch {
                throw "The certificate-bound CNG private key could not be opened as RSA."
            }
            $usedCngCompatibilityPath = $true
        }

        if ($null -eq $privateKey) {
            throw "The signing certificate has no usable RSA private key."
        }

        try {
            $certificateParameters = $publicKey.ExportParameters($false)
            $privateKeyParameters = $privateKey.ExportParameters($false)
        } catch {
            throw "The opened private key could not be verified against the signing certificate's public key."
        }
        if (-not (Test-ByteArrayEqual $certificateParameters.Modulus $privateKeyParameters.Modulus) -or
            -not (Test-ByteArrayEqual $certificateParameters.Exponent $privateKeyParameters.Exponent)) {
            throw "The opened private key does not match the signing certificate's public key."
        }

        try {
            $signature = $privateKey.SignData($manifestBytes,
                                              [System.Security.Cryptography.HashAlgorithmName]::SHA256,
                                              [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
        } catch {
            throw "Signing the release manifest failed."
        }

        $detached = [ordered]@{
            algorithm   = "RSA-PKCS1-SHA256"
            thumbprint  = $certificate.Thumbprint.ToUpperInvariant()
            subject     = $certificate.Subject
            signature   = [Convert]::ToBase64String($signature)
        }
        $detachedBytes = [System.Text.Encoding]::UTF8.GetBytes((ConvertTo-CanonicalJson $detached))

        # Prepare both public artifacts completely before publishing either one.
        try {
            $certificateBytes = $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
        } catch {
            throw "The public signing certificate could not be exported."
        }
        $signatureTemporaryPath = Join-Path $OutputDirectory ([System.IO.Path]::GetRandomFileName())
        $certificateTemporaryPath = Join-Path $OutputDirectory ([System.IO.Path]::GetRandomFileName())
        try {
            [System.IO.File]::WriteAllBytes($signatureTemporaryPath, $detachedBytes)
            [System.IO.File]::WriteAllBytes($certificateTemporaryPath, $certificateBytes)
            Move-Item -LiteralPath $signatureTemporaryPath -Destination $signaturePath
            Move-Item -LiteralPath $certificateTemporaryPath -Destination $certificatePath
        } catch {
            Remove-Item -LiteralPath $signaturePath, $certificatePath -Force -ErrorAction SilentlyContinue
            throw "The release signature artifacts could not be published safely."
        } finally {
            Remove-Item -LiteralPath $signatureTemporaryPath, $certificateTemporaryPath -Force -ErrorAction SilentlyContinue
        }
    } catch {
        Remove-Item -LiteralPath $signaturePath, $certificatePath -Force -ErrorAction SilentlyContinue
        throw
    } finally {
        if ($null -ne $privateKey) {
            $privateKey.Dispose()
        }
        if ($null -ne $cngKey) {
            $cngKey.Dispose()
        }
        $publicKey.Dispose()
    }

    $signingSubject = $certificate.Subject
} finally {
    $certificate.Dispose()
}

if ($usedCngCompatibilityPath) {
    Write-Host "Signing key  : used the Windows PowerShell CNG compatibility path"
}

Write-Host ""
Write-Host "Package       : $OutputDirectory"
Write-Host "Release       : $releaseVersion ($architecture)"
Write-Host "Wintun        : $driverVersion"
Write-Host "Signed by     : $signingSubject"
Write-Host "Files         : $($files.Count) plus the manifest, the signature and the certificate"
