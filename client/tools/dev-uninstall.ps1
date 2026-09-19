<#
.SYNOPSIS
    Removes a developer registration of the phpvpn service.

.DESCRIPTION
    plan_client.md chapter 3.5. The reverse of dev-install.ps1: it disconnects
    through the service itself so the tunnel removes only its own address and
    routes, then stops and unregisters the service.

    Following chapter 20.5 it never calls WintunDeleteDriver and it never
    deletes the configuration, the ownership journal or the logs.
#>
[CmdletBinding()]
param(
    [string]$BuildDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $scriptRoot "..\..\work\build\debug"
}

function Remove-TrayAutostartFallback {
        $base = $null
        $run = $null
        try {
            $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, [Microsoft.Win32.RegistryView]::Registry64)
            $run = $base.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Run', $true)
            if ($null -ne $run) { $run.DeleteValue('phpvpn-tray', $false) }
        } catch {
            Write-Warning ('phpvpn-tray autostart cleanup failed (HRESULT {0}). Retry uninstall after fixing registry access.' -f $_.Exception.HResult)
        } finally {
            if ($null -ne $run) { $run.Dispose() }
            if ($null -ne $base) { $base.Dispose() }
        }
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Removing a Windows service requires an elevated PowerShell session."
    }
}

Assert-Administrator

$appsDirectory = Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) "apps"
$service = Join-Path $appsDirectory "phpvpn-service.exe"
$cli = Join-Path $appsDirectory "phpvpn.exe"

# The tray holds a handle to the control pipe, so it is closed before the
# service goes away.
Get-Process -Name "phpvpn-tray" -ErrorAction SilentlyContinue | Stop-Process -Force

if (Test-Path -LiteralPath $cli -PathType Leaf) {
    $running = Get-Service -Name "phpvpn" -ErrorAction SilentlyContinue
    if ($null -ne $running -and $running.Status -eq "Running") {
        Write-Host "Disconnecting before the service is removed."
        & $cli disconnect | Out-Null
    }
}

if (Test-Path -LiteralPath $service -PathType Leaf) {
    & $service --uninstall
    if ($LASTEXITCODE -ne 0) {
        throw "The service could not be removed."
    }
} else {
    # The build directory may already be gone, so fall back to the service
    # control manager directly.
    try {
    $existing = Get-Service -Name "phpvpn" -ErrorAction SilentlyContinue
    if ($null -ne $existing) {
        Stop-Service -Name "phpvpn" -Force -ErrorAction Stop
        & sc.exe delete phpvpn | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'The service could not be removed.' }
    }
    } finally { Remove-TrayAutostartFallback }
}

$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$legacy = Get-ItemProperty -Path $runKey -Name "phpvpn-tray" -ErrorAction SilentlyContinue
$knownTray = Join-Path $appsDirectory 'phpvpn-tray.exe'
if ($null -ne $legacy -and $legacy.'phpvpn-tray' -ceq "`"$knownTray`"") {
    try { Remove-ItemProperty -Path $runKey -Name "phpvpn-tray" -ErrorAction Stop }
    catch { Write-Warning 'The matching legacy current-user tray entry could not be removed.' }
}

Write-Host "The phpvpn service is no longer registered."
Write-Host "The configuration, the ownership journal and the logs were kept in $(Join-Path $env:ProgramData 'phpvpn')."
