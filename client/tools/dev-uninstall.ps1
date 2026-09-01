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

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "Removing a Windows service requires an elevated PowerShell session."
    }
}

Assert-Administrator

$appsDirectory = Join-Path (Resolve-Path $BuildDirectory) "apps"
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
    $existing = Get-Service -Name "phpvpn" -ErrorAction SilentlyContinue
    if ($null -ne $existing) {
        Stop-Service -Name "phpvpn" -Force -ErrorAction SilentlyContinue
        & sc.exe delete phpvpn | Out-Null
    }
}

$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
if ($null -ne (Get-ItemProperty -Path $runKey -Name "phpvpn-tray" -ErrorAction SilentlyContinue)) {
    Remove-ItemProperty -Path $runKey -Name "phpvpn-tray"
    Write-Host "The tray autostart entry was removed."
}

Write-Host "The phpvpn service is no longer registered."
Write-Host "The configuration, the ownership journal and the logs were kept in $(Join-Path $env:ProgramData 'phpvpn')."
