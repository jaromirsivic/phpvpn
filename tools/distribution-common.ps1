# Shared, explicit distribution contract. Source paths are checkout-relative.
function Get-DistributionSources {
    $files = [ordered]@{
        'server/PHP/phpvpn.php' = 'phpvpn.php'
        'server/Python/phpvpn.py' = 'phpvpn.py'
        'server/Python/requirements-server.txt' = 'requirements-server.txt'
        'server/Python/server-python.md' = 'server-python.md'
    }
    foreach ($name in @('init.bat','start.bat','stop.bat','init.sh','start.sh','stop.sh','bootstrap.ps1','server-control.py')) {
        $files["server/Python/$name"] = "tools/server-python/$name"
    }
    return $files
}

function Assert-DistributionPath {
    param([string]$Path, [string]$Within, [switch]$Tree)
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    $parent = [IO.Path]::GetFullPath($Within).TrimEnd('\','/')
    if (-not $full.StartsWith($parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe target outside the permitted directory: $full"
    }
    $current = $full
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse path is not allowed: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
    if ($Tree -and (Test-Path -LiteralPath $full)) {
        # Enumerate level by level so traversal never enters a reparse directory.
        $pending = New-Object 'Collections.Generic.Queue[string]'
        $pending.Enqueue($full)
        while ($pending.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
                if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse entry: $($item.FullName)" }
                if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
            }
        }
    }
    return $full
}

function Get-UnsupportedClientReadme([string]$Platform) {
    return "# PHP VPN client for $Platform`n`nNo distributable client implementation is available for this platform yet.`nThis directory does not claim platform support or completed validation.`n"
}

# Linux client folder client/Linux/x64, staged by client/tools/linux/make-linux-dist.ps1.
# The complete distribution requires phpvpn and phpvpn-tray for both Fedora and Ubuntu.
function Get-LinuxPackagePatterns {
    return [ordered]@{
        rpm     = '^phpvpn-(\d+\.\d+\.\d+)-(\d+)\.x86_64\.rpm$'
        deb     = '^phpvpn_(\d+\.\d+\.\d+)-(\d+)_amd64\.deb$'
        trayRpm = '^phpvpn-tray-(\d+\.\d+\.\d+)-(\d+)\.x86_64\.rpm$'
        trayDeb = '^phpvpn-tray_(\d+\.\d+\.\d+)-(\d+)_amd64\.deb$'
    }
}

# Unchanged copies in client/Linux/x64 besides the packages and the signed manifest.
function Get-LinuxClientSources {
    return [ordered]@{
        'install.sh' = 'client/packaging/linux/dist/install.sh'
        'uninstall.sh' = 'client/packaging/linux/dist/uninstall.sh'
        'README.md' = 'client/packaging/linux/dist/README.md'
        'LICENSE-material-symbols.txt' = 'client/assets/icons/LICENSE-material-symbols.txt'
        'NOTICE-material-symbols.txt' = 'client/assets/icons/NOTICE-material-symbols.txt'
    }
}

function Get-ClientVersion([string]$Repository) {
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $Repository 'client/CMakeLists.txt'))) {
        if ($line -match '^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$') { return $Matches[1] }
    }
    throw 'The client version was not found in client/CMakeLists.txt.'
}

function Test-DistributionRuntimePath([string]$RelativePath) {
    return $RelativePath.Replace('\', '/') -match '^server/Python/\.(runtime|venv)(/|$)'
}

function Publish-Distribution {
    param([string]$Repository, [string]$Session, [string]$Staging, [string]$Archive)
    $ErrorActionPreference = 'Stop'
    $dist = Assert-DistributionPath (Join-Path $Repository 'dist') $Repository -Tree
    $zip = Assert-DistributionPath (Join-Path $Repository 'dist.zip') $Repository
    $staging = Assert-DistributionPath $Staging $Session -Tree
    $archive = Assert-DistributionPath $Archive $Session
    $previous = Assert-DistributionPath (Join-Path $Session 'previous-dist') $Session
    $previousZip = Assert-DistributionPath (Join-Path $Session 'previous-dist.zip') $Session
    $failed = Assert-DistributionPath (Join-Path $Session 'failed-publication') $Session
    foreach ($path in @($previous, $previousZip, $failed)) {
        if (Test-Path -LiteralPath $path) { throw "Publication backup already exists: $path" }
    }
    if (-not (Test-Path -LiteralPath $staging -PathType Container) -or
        -not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw 'Verified distribution staging or ZIP is missing.' }
    [IO.Directory]::CreateDirectory($dist) | Out-Null
    [IO.Directory]::CreateDirectory($previous) | Out-Null
    $oldEntries = @(Get-ChildItem -LiteralPath $dist -Recurse -Force | Where-Object {
        -not (Test-DistributionRuntimePath $_.FullName.Substring($dist.Length + 1))
    })
    $newEntries = @(Get-ChildItem -LiteralPath $staging -Recurse -Force)
    foreach ($entry in $newEntries) {
        if (Test-DistributionRuntimePath $entry.FullName.Substring($staging.Length + 1)) {
            throw 'Distribution staging must not contain local Python runtime state.'
        }
    }
    $oldDirectories = @($oldEntries | Where-Object PSIsContainer | ForEach-Object { $_.FullName.Substring($dist.Length + 1) })
    $newDirectories = @($newEntries | Where-Object PSIsContainer | ForEach-Object { $_.FullName.Substring($staging.Length + 1) })
    $savedNames = [Collections.Generic.List[string]]::new()
    $publishedNames = [Collections.Generic.List[string]]::new()
    $savedZip = $false
    try {
        # Preserve directory handles/current directories held by open shells.
        # Only replace distribution files. Operational Python state is excluded
        # from both publication and rollback and remains at its original path.
        foreach ($name in $oldDirectories) {
            [IO.Directory]::CreateDirectory((Assert-DistributionPath (Join-Path $previous $name) $previous)) | Out-Null
        }
        foreach ($entry in $oldEntries | Where-Object { -not $_.PSIsContainer }) {
            $name = $entry.FullName.Substring($dist.Length + 1)
            $source = Assert-DistributionPath $entry.FullName $dist
            $destination = Assert-DistributionPath (Join-Path $previous $name) $previous
            Move-Item -LiteralPath $source -Destination $destination
            $savedNames.Add($name)
        }
        if (Test-Path -LiteralPath $zip) {
            Move-Item -LiteralPath $zip -Destination $previousZip
            $savedZip = $true
        }
        foreach ($name in $oldDirectories | Sort-Object Length -Descending) {
            if ($name -notin $newDirectories) {
                $path = Assert-DistributionPath (Join-Path $dist $name) $dist
                [IO.Directory]::Delete($path) # Empty only, never recursive.
            }
        }
        foreach ($name in $newDirectories) {
            [IO.Directory]::CreateDirectory((Assert-DistributionPath (Join-Path $dist $name) $dist)) | Out-Null
        }
        foreach ($entry in $newEntries | Where-Object { -not $_.PSIsContainer }) {
            $name = $entry.FullName.Substring($staging.Length + 1)
            $source = Assert-DistributionPath $entry.FullName $staging
            $destination = Assert-DistributionPath (Join-Path $dist $name) $dist
            Move-Item -LiteralPath $source -Destination $destination
            $publishedNames.Add($name)
        }
        Move-Item -LiteralPath $archive -Destination $zip
    } catch {
        [IO.Directory]::CreateDirectory($failed) | Out-Null
        foreach ($name in $publishedNames) {
            $source = Assert-DistributionPath (Join-Path $dist $name) $dist
            $destination = Assert-DistributionPath (Join-Path $failed $name) $failed
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
            Move-Item -LiteralPath $source -Destination $destination
        }
        foreach ($name in $oldDirectories) {
            [IO.Directory]::CreateDirectory((Assert-DistributionPath (Join-Path $dist $name) $dist)) | Out-Null
        }
        foreach ($name in $savedNames) {
            $source = Assert-DistributionPath (Join-Path $previous $name) $previous
            $destination = Assert-DistributionPath (Join-Path $dist $name) $dist
            Move-Item -LiteralPath $source -Destination $destination
        }
        foreach ($name in $newDirectories | Sort-Object Length -Descending) {
            $path = Assert-DistributionPath (Join-Path $dist $name) $dist
            if ($name -notin $oldDirectories -and (Test-Path -LiteralPath $path)) { [IO.Directory]::Delete($path) }
        }
        if ($savedZip) { Move-Item -LiteralPath $previousZip -Destination $zip }
        throw
    }
}

# ------------------------------------------------ release manifest and signature
# Shared by client/tools/make-dist.ps1 (Windows client package) and
# client/tools/linux/make-linux-dist.ps1 (Linux client folder). The manifest is
# compact UTF-8 JSON without a byte order mark or line break, and the signature
# is stored separately, so the exact bytes that were signed are the exact bytes
# the installers verify.

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
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

function Write-SignedReleaseManifest {
    # Writes release-manifest.json in its canonical form, then the detached
    # RSA-PKCS1-SHA256 signature release-manifest.sig and the public
    # certificate release-signing.cer, signed with the pinned certificate from
    # Cert:\CurrentUser\My. Both public artifacts are published together or
    # not at all. Returns the signing subject and whether the Windows
    # PowerShell CNG compatibility path opened the private key.
    param(
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [Parameter(Mandatory=$true)][System.Collections.Specialized.OrderedDictionary]$Manifest,
        [Parameter(Mandatory=$true)][string]$CertificateThumbprint
    )

    $canonical = ConvertTo-CanonicalJson $Manifest
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

    return [pscustomobject]@{
        Subject                  = $signingSubject
        UsedCngCompatibilityPath = $usedCngCompatibilityPath
        ManifestBytes            = $manifestBytes
    }
}
