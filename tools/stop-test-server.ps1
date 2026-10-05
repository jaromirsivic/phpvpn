<#
.SYNOPSIS
    Stops the private nginx and PHP FastCGI pair started by start-test-server.ps1.

.DESCRIPTION
    Only the processes recorded under <testServer.directory> of the global
    configuration are stopped. Other nginx or PHP processes of the machine stay
    untouched.
#>

[CmdletBinding()]
param(
    [string]$NginxExecutable,
    [string]$PhpCgiExecutable,
    [string]$PhpExecutable,
    [string]$Directory
)

$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'phpvpn-config.ps1')
. (Join-Path $PSScriptRoot 'test-server\test-server-common.ps1')

if (-not $NginxExecutable) { $NginxExecutable = Resolve-PhpVpnTool 'nginx' }
if (-not $PhpCgiExecutable) { $PhpCgiExecutable = Resolve-PhpVpnTool 'phpCgi' 'php-cgi' }
if (-not $PhpExecutable) { $PhpExecutable = Resolve-PhpVpnTool 'php' }
if (-not $Directory) { $Directory = Resolve-PhpVpnConfigPath 'testServer.directory' }
$state = Get-TestServerState $Directory

$master = Get-OwnedProcess $state.NginxPid $NginxExecutable $state.NginxConf
if ($master) {
    $workers = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($master.ProcessId)" -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -and $_.ExecutablePath.Equals([IO.Path]::GetFullPath($NginxExecutable), [StringComparison]::OrdinalIgnoreCase) })
    Start-Process -FilePath $NginxExecutable -ArgumentList '-p', $state.PrefixArgument, '-c', $state.NginxConf, '-s', 'quit' `
        -WorkingDirectory $state.Prefix -WindowStyle Hidden -Wait
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline -and (Get-Process -Id $master.ProcessId -ErrorAction SilentlyContinue)) {
        Start-Sleep -Milliseconds 200
    }
    foreach ($id in @($master.ProcessId) + @($workers | ForEach-Object ProcessId)) {
        Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
    }
}
$fastCgiListen = Get-PhpVpnConfigValue 'testServer.fastCgiListen'
$largeBodyListen = Get-PhpVpnConfigValue 'testServer.largeBodyListen'
foreach ($entry in @(@($state.PhpCgiPid, $PhpCgiExecutable, $fastCgiListen), @($state.BuiltinPid, $PhpExecutable, $largeBodyListen))) {
    $owned = Get-OwnedProcess $entry[0] $entry[1] $entry[2]
    if ($owned) { Stop-Process -Id $owned.ProcessId -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $entry[0] -Force -ErrorAction SilentlyContinue
}
Write-Host 'stopped   the private nginx, php-cgi and php built in server of this checkout'
