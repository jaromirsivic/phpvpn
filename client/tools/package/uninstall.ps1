<#
.SYNOPSIS
    Removes the phpvpn client.

.DESCRIPTION
    plan_client.md chapter 20.5, in the reverse safe order:

      1. a safe disconnect that removes only the addresses and routes the
         client created itself,
      2. the session and the adapter close with it,
      3. the service stops and is unregistered through the service control
         manager,
      4. the tray autostart entry goes away,
      5. only the components the installation created are deleted.

    WintunDeleteDriver is never called, because WireGuard or another
    application may be using the same system driver.

    The configuration, the ownership journal and the logs are kept unless
    -RemoveData is given explicitly.
#>
[CmdletBinding()]
param(
    # Also deletes %ProgramData%\phpvpn with the configuration and the logs.
    [switch]$RemoveData
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Keep stdout, stderr and native child output compatible with the GUI decoder.
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$OutputEncoding = [Console]::OutputEncoding

$installRoot = Join-Path $env:ProgramFiles "phpvpn"
$dataRoot = Join-Path $env:ProgramData "phpvpn"

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
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Assert-Administrator)) {
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"")
    if ($RemoveData) { $arguments += "-RemoveData" }
    $elevated = Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -Verb RunAs -PassThru
    $elevated.WaitForExit()
    exit $elevated.ExitCode
}

$generation = $null
$pointer = Join-Path $installRoot "current.generation"
if (Test-Path -LiteralPath $pointer -PathType Leaf) {
    $generation = Join-Path $installRoot ([System.IO.File]::ReadAllText($pointer).Trim())
}

# ------------------------------------------------------------ 1. disconnect

if ($null -ne $generation -and (Test-Path -LiteralPath (Join-Path $generation "phpvpn.exe"))) {
    $service = Get-Service -Name "phpvpn" -ErrorAction SilentlyContinue
    if ($null -ne $service -and $service.Status -eq "Running") {
        Write-Host "disconnecting the tunnel before the service is removed"
        & (Join-Path $generation "phpvpn.exe") disconnect | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'The tunnel could not be disconnected.' }
        Start-Sleep -Seconds 2
    }
}

# ------------------------------------------------------- 2. and 3. the service

Get-Process -Name "phpvpn-tray" -ErrorAction SilentlyContinue | Stop-Process -Force

$service = Get-Service -Name "phpvpn" -ErrorAction SilentlyContinue
if ($null -ne $generation -and (Test-Path -LiteralPath (Join-Path $generation "phpvpn-service.exe"))) {
    & (Join-Path $generation "phpvpn-service.exe") --uninstall
    if ($LASTEXITCODE -ne 0) { throw 'The service could not be unregistered.' }
} else {
    # Emergency path for a missing binary. Always attempt our Run cleanup,
    # even if the critical SCM operation fails. Never remove the Run key.
    try {
      if ($null -ne $service) {
        Stop-Service -Name "phpvpn" -Force -ErrorAction Stop
        & sc.exe delete phpvpn | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'The service could not be unregistered by sc.exe.' }
      }
    } finally {
        Remove-TrayAutostartFallback
    }
}

# ------------------------------------------------------- 5. installed files

if (Test-Path -LiteralPath $installRoot) {
    $item = Get-Item -LiteralPath $installRoot -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        Write-Host "the installation directory is a reparse point and was left in place" -ForegroundColor Yellow
        throw 'Uninstallation is incomplete because the installation directory was left in place.'
    } else {
        Remove-Item -LiteralPath $installRoot -Recurse -Force
        Write-Host "the installation directory was removed"
    }
}

if ($RemoveData) {
    if (Test-Path -LiteralPath $dataRoot) {
        Remove-Item -LiteralPath $dataRoot -Recurse -Force
        Write-Host "the configuration, the ownership journal and the logs were removed"
    }
} else {
    Write-Host "the configuration, the ownership journal and the logs were kept in $dataRoot"
}

Write-Host ""
Write-Host "phpvpn was removed. The shared Wintun driver was left untouched."

# Keep the result in the console that performed the operation, including UAC.
Write-Host ""
Write-Host "+------------------------------------------------------------+"
Write-Host "| PHPVPN uninstalled successfully                            |"
Write-Host "| The service and client removal is complete.                |"
Write-Host "+------------------------------------------------------------+"
Start-Sleep -Seconds 3
exit 0
