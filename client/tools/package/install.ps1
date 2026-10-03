<#
.SYNOPSIS
    Installs the phpvpn client from this package.

.DESCRIPTION
    plan_client.md chapter 20.2. The order is fail closed: nothing is created,
    registered or changed on the network until every verification passed.

      1. the manifest signature and its canonical form,
      2. the SHA-256 of every packaged file, with no missing and no extra
         executable,
      3. the Authenticode signature, the publisher and the architecture of
         wintun.dll,
      4. the mandatory tunnel driver API, without creating an adapter,
      5. the protected installation and data directories,
      6. the single Windows service with a quoted absolute ImagePath,
      7. the control pipe of contract v1, created by the service itself,
      8. the tray autostart for every new interactive logon,
      9. the tray application in the current session.

    A mismatch in steps 1 to 4 is an unrecoverable package error. The script
    then stops without creating an adapter, without installing the service and
    without changing a single address, route or DNS setting.

    The installation never calls WintunDeleteDriver, because WireGuard or
    another application may be using the same system driver.
#>
[CmdletBinding()]
param(
    # Pinned signing identity of the phpvpn release key.
    [string]$ExpectedThumbprint = "7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8",

    # Verifies the package and stops before anything is created.
    [switch]$VerifyOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Native GUI capture and redirected Windows PowerShell 5.1 output share UTF-8.
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding

$package = Split-Path -Parent $MyInvocation.MyCommand.Path
$installRoot = Join-Path $env:ProgramFiles "phpvpn"
$dataRoot = Join-Path $env:ProgramData "phpvpn"
$binaries = @("phpvpn-service.exe", "phpvpn-tray.exe", "phpvpn.exe")

function Fail {
    param([string]$Message)
    Write-Host "installation refused: $Message" -ForegroundColor Red
    exit 1
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not $VerifyOnly -and -not (Assert-Administrator)) {
    # Re-launch elevated once. The user sees exactly one consent prompt.
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"")
    $arguments += @("-ExpectedThumbprint", "`"$ExpectedThumbprint`"")
    $elevated = Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -Verb RunAs -PassThru
    # Wait only for the installer, not its long-running tray child process.
    $elevated.WaitForExit()
    exit $elevated.ExitCode
}

# --------------------------------------------------- 1. manifest and signature

$manifestPath = Join-Path $package "release-manifest.json"
$signaturePath = Join-Path $package "release-manifest.sig"
$certificatePath = Join-Path $package "release-signing.cer"

foreach ($required in @($manifestPath, $signaturePath, $certificatePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        Fail "the package is incomplete, $(Split-Path $required -Leaf) is missing"
    }
}

$manifestBytes = [System.IO.File]::ReadAllBytes($manifestPath)
if ($manifestBytes.Length -ge 3 -and $manifestBytes[0] -eq 0xEF) {
    Fail "the manifest carries a byte order mark and is therefore not canonical"
}
$manifestText = [System.Text.Encoding]::UTF8.GetString($manifestBytes)
if ($manifestText.Contains("`r") -or $manifestText.Contains("`n")) {
    Fail "the manifest is not in its canonical single line form"
}

try {
    $manifest = $manifestText | ConvertFrom-Json
} catch {
    Fail "the manifest is not valid JSON"
}
if ($manifest.manifestVersion -ne 1) {
    Fail "the manifest format version $($manifest.manifestVersion) is not supported"
}

$detached = [System.IO.File]::ReadAllText($signaturePath) | ConvertFrom-Json
if ($detached.algorithm -ne "RSA-PKCS1-SHA256") {
    Fail "the signature algorithm $($detached.algorithm) is not supported"
}

$certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($certificatePath)
if ($certificate.Thumbprint.ToUpperInvariant() -ne $ExpectedThumbprint.ToUpperInvariant()) {
    Fail "the package was signed by an identity that is not pinned"
}
if ($detached.thumbprint.ToUpperInvariant() -ne $ExpectedThumbprint.ToUpperInvariant()) {
    Fail "the signature names an identity that is not pinned"
}
if ($manifest.signingIdentity.ToUpperInvariant() -ne $ExpectedThumbprint.ToUpperInvariant()) {
    Fail "the manifest names an identity that is not pinned"
}

$publicKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
$verified = $publicKey.VerifyData(
    $manifestBytes,
    [Convert]::FromBase64String($detached.signature),
    [System.Security.Cryptography.HashAlgorithmName]::SHA256,
    [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
if (-not $verified) {
    Fail "the manifest signature did not verify"
}
Write-Host "manifest      : verified, release $($manifest.releaseVersion) for $($manifest.architecture)"

# ------------------------------------------------------ 2. per file SHA-256

$listed = @{}
foreach ($entry in $manifest.files) {
    $path = Join-Path $package $entry.path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Fail "the packaged file $($entry.path) is missing"
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $entry.sha256.ToLowerInvariant()) {
        Fail "the packaged file $($entry.path) does not match its recorded hash"
    }
    $listed[$entry.path] = $true
}

foreach ($file in Get-ChildItem -LiteralPath $package -File) {
    if ($file.Extension -eq ".exe" -or $file.Extension -eq ".dll") {
        if (-not $listed.ContainsKey($file.Name)) {
            Fail "the package holds the unexpected binary $($file.Name)"
        }
    }
}
foreach ($name in $binaries) {
    if (-not $listed.ContainsKey($name)) {
        Fail "the package does not hold $name"
    }
}
Write-Host "package files : $($manifest.files.Count) verified"

# ------------------------------------- 3. tunnel driver signature and machine

function Get-PeMachine {
    param([string]$Path)
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        $stream.Position = 0x3C
        $stream.Position = $reader.ReadInt32()
        if ($reader.ReadUInt32() -ne 0x00004550) { return "unknown" }
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

$driverPath = Join-Path $package "wintun.dll"
$driverSignature = Get-AuthenticodeSignature -LiteralPath $driverPath
if ($driverSignature.Status -ne "Valid") {
    Fail "wintun.dll does not carry a valid Authenticode signature"
}
if ($driverSignature.SignerCertificate.Subject -notmatch "WireGuard") {
    Fail "wintun.dll was not published by the expected vendor"
}

$serviceMachine = Get-PeMachine (Join-Path $package "phpvpn-service.exe")
$driverMachine = Get-PeMachine $driverPath
$osMachine = switch ($env:PROCESSOR_ARCHITECTURE) {
    "AMD64" { "x64" }
    "ARM64" { "arm64" }
    "x86"   { "x86" }
    default { "unknown" }
}
if ($serviceMachine -ne $driverMachine) {
    Fail "the service is $serviceMachine but the tunnel driver is $driverMachine"
}
if ($serviceMachine -ne $osMachine) {
    Fail "the package is $serviceMachine but this system is $osMachine"
}
Write-Host "tunnel driver : $($driverSignature.SignerCertificate.Subject), $driverMachine"

# ------------------------------------------------- 4. the mandatory driver API

$probe = & (Join-Path $package "phpvpn-service.exe") --verify-tunnel
if ($LASTEXITCODE -ne 0) {
    Fail "the tunnel driver does not expose the required API"
}
Write-Host "driver API    : $probe"

if ($VerifyOnly) {
    Write-Host ""
    Write-Host "the package is complete and trustworthy, nothing was installed"
    exit 0
}

# -------------------------------------------------------- 5. directories

function Request-TrayExit {
    param([int]$ProcessId)
    if (-not ('PhpVpnInstaller.TrayExit' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace PhpVpnInstaller {
    public static class TrayExit {
        private delegate bool EnumProc(IntPtr window, IntPtr data);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr data);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr window, StringBuilder name, int size);
        [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        public static bool Request(int processId) {
            bool posted = false;
            EnumWindows((window, data) => {
                uint owner;
                GetWindowThreadProcessId(window, out owner);
                var name = new StringBuilder(256);
                GetClassName(window, name, name.Capacity);
                if (owner == processId && name.ToString() == "PhpVpnTrayWindow")
                    posted = PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero) || posted;
                return true;
            }, IntPtr.Zero);
            return posted;
        }
    }
}
'@
    }
    return [PhpVpnInstaller.TrayExit]::Request($ProcessId)
}

function Stop-InstalledClients {
    param([string]$InstallRoot)
    $rootPrefix = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\') + '\'
    foreach ($tray in @(Get-Process -Name 'phpvpn-tray' -ErrorAction SilentlyContinue)) {
        if (-not $tray.Path) { throw 'Cannot identify a running tray. Close it and retry installation.' }
        if (-not ([IO.Path]::GetFullPath($tray.Path)).StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { continue }
        if (-not $tray.HasExited) {
            Write-Host 'tray          : requesting Exit, confirm any unsaved work in the tray'
            if (-not (Request-TrayExit -ProcessId $tray.Id)) {
                throw 'Cannot request tray Exit in its desktop. Close the installed tray in all sessions and retry.'
            }
            if (-not $tray.WaitForExit(30000)) {
                throw 'Tray Exit was cancelled or is still pending. Finish or cancel your work, close the tray and retry.'
            }
        }
    }
    $service = Get-Service -Name 'phpvpn' -ErrorAction SilentlyContinue
    if ($null -ne $service) {
        Write-Host 'service       : stopping the previous generation before replacing files'
        Stop-Service -Name 'phpvpn' -ErrorAction Stop
        $service.WaitForStatus([System.ServiceProcess.ServiceControllerStatus]::Stopped, [TimeSpan]::FromSeconds(30))
    }
}

function New-ProtectedDirectory {
    param([string]$Path)

    if (Test-Path -LiteralPath $Path) {
        $item = Get-Item -LiteralPath $Path -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            Fail "$Path is a reparse point and cannot be used safely"
        }
    } else {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }

    # SYSTEM and Administrators keep full control, authenticated users may read
    # so the tray and the console can start from the installation directory.
    $acl = New-Object System.Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($account in @("NT AUTHORITY\SYSTEM", "BUILTIN\Administrators")) {
        $acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
            $account, "FullControl", "ContainerInherit,ObjectInherit", "None", "Allow")))
    }
    $acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
        "BUILTIN\Users", "ReadAndExecute", "ContainerInherit,ObjectInherit", "None", "Allow")))
    $acl.SetOwner((New-Object System.Security.Principal.NTAccount("BUILTIN\Administrators")))
    Set-Acl -LiteralPath $Path -AclObject $acl
}

$generation = Join-Path $installRoot $manifest.releaseVersion
New-ProtectedDirectory $installRoot
# The service creates/migrates ProgramData through its pinned, no-reparse walker.
# Recursive Set-Acl here could follow an untrusted child or alter an outside hardlink.
# Do not touch the existing data tree before that validation.
New-ProtectedDirectory $generation

# Both equal-version upgrades and different-version upgrades must release the
# old tray and service before copying or publishing the generation pointer.
# WM_CLOSE uses the tray's normal Exit workflow, including unsaved-work consent.
# Refusal/timeout aborts without forcing processes or discarding user drafts.
Stop-InstalledClients -InstallRoot $installRoot

# Refuse remaining locks (for example an open CLI) before the first overwrite.
foreach ($entry in $manifest.files) {
    $destination = Join-Path $generation $entry.path
    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        $handle = [IO.File]::Open($destination, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $handle.Dispose()
    }
}

foreach ($entry in $manifest.files) {
    Copy-Item -LiteralPath (Join-Path $package $entry.path) -Destination $generation -Force
}
Copy-Item -LiteralPath $manifestPath -Destination $generation -Force
Copy-Item -LiteralPath $signaturePath -Destination $generation -Force
Copy-Item -LiteralPath $certificatePath -Destination $generation -Force

# The generation pointer is a single small file, so the switch of chapter 20.3
# stays atomic on the same volume.
$pointerTemp = Join-Path $installRoot "current.generation.new"
[System.IO.File]::WriteAllText($pointerTemp, $manifest.releaseVersion, `
                               (New-Object System.Text.UTF8Encoding($false)))
Move-Item -LiteralPath $pointerTemp -Destination (Join-Path $installRoot "current.generation") -Force
Write-Host "installed to  : $generation"

# ------------------------------------------------------------ 6. the service

$serviceBinary = Join-Path $generation "phpvpn-service.exe"
$existing = Get-Service -Name "phpvpn" -ErrorAction SilentlyContinue
if ($null -ne $existing) {
    & $serviceBinary --uninstall
    if ($LASTEXITCODE -ne 0) { Fail 'the previous service could not be unregistered' }
}

& $serviceBinary --install
if ($LASTEXITCODE -ne 0) {
    Fail "the service could not be registered"
}
Start-Service -Name "phpvpn"

# --------------------------------------------------------- 7. the control pipe

# The service creates the pipe of contract v1 itself, so the installer only
# waits for it to appear instead of pre-creating anything.
$pipeReady = $false
for ($attempt = 0; $attempt -lt 50; ++$attempt) {
    if (Test-Path -LiteralPath "\\.\pipe\phpvpn-control-v1") {
        $pipeReady = $true
        break
    }
    Start-Sleep -Milliseconds 200
}
if (-not $pipeReady) {
    Fail "the service started but did not publish the control pipe"
}
Write-Host "service       : running, control pipe published"

# --------------------- 8. tray autostart is owned by --install above

$trayBinary = Join-Path $generation "phpvpn-tray.exe"

# ------------------------------------------------- 9. the tray in this session

# Only this success path requests the notice. Service autostart and ordinary
# tray launches never carry this switch. A running tray consumes the request.
# The tray is a GUI executable. SW_HIDE would hide its first opened window.
Start-Process -FilePath $trayBinary -ArgumentList '--installation-complete' -ErrorAction Stop

Write-Host ""
Write-Host "phpvpn $($manifest.releaseVersion) is installed."
Write-Host "  configuration : $(Join-Path $dataRoot 'config.json')"
Write-Host "  logs          : $(Join-Path $dataRoot 'logs')"
Write-Host "  console       : $(Join-Path $generation 'phpvpn.exe') status"
Write-Host "Service startup creates the shared default configuration only when missing, after secure storage recovery. Existing bytes are preserved."
Write-Host "Configure the file using Configuration Wizard or Edit Config. If it was deleted while running, Edit Config can confirm its recreation."
Write-Host "Then choose Connect to start the VPN. Existing configuration is preserved."

# Keep the result in the console that performed the operation, including UAC.
Write-Host ""
Write-Host "+------------------------------------------------------------+"
Write-Host "| PHPVPN installed successfully                              |"
Write-Host "| The service and client installation is complete.           |"
Write-Host "+------------------------------------------------------------+"
Start-Sleep -Seconds 3
exit 0
