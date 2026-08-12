<#
.SYNOPSIS
    Stops the local nginx and PHP FastCGI pair used by the integration tests.
#>

[CmdletBinding()]
param(
    [string]$NginxRoot = 'D:\nginx'
)

$ErrorActionPreference = 'Continue'

$nginx = Join-Path $NginxRoot 'nginx.exe'
if (Get-Process nginx -ErrorAction SilentlyContinue) {
    Start-Process -FilePath $nginx -ArgumentList '-s', 'quit' -WorkingDirectory $NginxRoot -WindowStyle Hidden -Wait
    Start-Sleep -Milliseconds 500
}
Get-Process nginx -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process php-cgi -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process php -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host 'stopped   nginx, php-cgi and the php built in server'
