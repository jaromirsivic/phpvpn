<#
.SYNOPSIS
    Registers the phpvpn service straight from a build directory.

.DESCRIPTION
    plan_client.md chapter 3.5: a fast local registration that skips the whole
    distribution step. It deliberately performs none of the release checks of
    chapter 20, so it is only meant for a development machine.

    The script never downgrades or deletes the Wintun driver and it never
    touches an existing configuration file.
#>
[CmdletBinding()]
param(
    # Build directory that holds apps\phpvpn-service.exe. Defaults to the debug
    # preset output of this repository.
    [string]$BuildDirectory,

    # Compatibility switch: --install now always owns machine tray autostart.
    [switch]$WithTrayAutostart
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
        throw "Registering a Windows service requires an elevated PowerShell session."
    }
}

Assert-Administrator

$appsDirectory = Join-Path (Resolve-Path $BuildDirectory) "apps"
$service = Join-Path $appsDirectory "phpvpn-service.exe"
$tray = Join-Path $appsDirectory "phpvpn-tray.exe"
$wintun = Join-Path $appsDirectory "wintun.dll"

foreach ($required in @($service, $tray, $wintun)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "The build is incomplete, $required is missing. Build the project first."
    }
}

# The service must find wintun.dll next to itself, so a build directory that
# lost the post build copy is refused instead of failing later at connect time.
Write-Host "Using build   : $appsDirectory"
Write-Host "Service binary: $service"

$existing = Get-Service -Name "phpvpn" -ErrorAction SilentlyContinue
if ($null -ne $existing) {
    Write-Host "An earlier registration exists, removing it first."
    & $service --uninstall
    if ($LASTEXITCODE -ne 0) {
        throw "The earlier service registration could not be removed."
    }
}

& $service --install
if ($LASTEXITCODE -ne 0) {
    throw "The service could not be registered."
}

Start-Service -Name "phpvpn"
Write-Host "The phpvpn service is registered and running."

$dataDirectory = Join-Path $env:ProgramData "phpvpn"
Write-Host "Configuration : $(Join-Path $dataDirectory 'config.json')"
Write-Host "Logs          : $(Join-Path $dataDirectory 'logs')"

# Migrate only the exact old entry for this known build in this profile.
$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$legacy = Get-ItemProperty -Path $runKey -Name "phpvpn-tray" -ErrorAction SilentlyContinue
if ($null -ne $legacy -and $legacy.'phpvpn-tray' -ceq "`"$tray`"") {
    try { Remove-ItemProperty -Path $runKey -Name "phpvpn-tray" -ErrorAction Stop }
    catch { Write-Warning 'The matching legacy current-user tray entry could not be removed.' }
}
if ($WithTrayAutostart) { Write-Host 'WithTrayAutostart is redundant: --install registers machine autostart.' }

Write-Host ""
Write-Host "Next steps:"
Write-Host "  Configure the file using Configuration Wizard or Edit Config. Edit Config can recreate a file deleted while running."
Write-Host "  Service startup creates config.json only when missing, after secure storage recovery. Existing bytes are preserved."
Write-Host "  $(Join-Path $appsDirectory 'phpvpn.exe') status"
Write-Host "  $(Join-Path $appsDirectory 'phpvpn.exe') connect"
