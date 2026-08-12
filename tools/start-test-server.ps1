<#
.SYNOPSIS
    Starts the local nginx and PHP FastCGI pair used by the integration tests.
#>

[CmdletBinding()]
param(
    [string]$NginxRoot = 'D:\nginx',
    [string]$PhpRoot = 'D:\nginx\php8.5.9',
    [string]$WebRoot = 'D:\nginx\html',
    [string]$FastCgiAddress = '127.0.0.1:9000',
    [string]$BuiltinAddress = '127.0.0.1:8081'
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Process php-cgi -ErrorAction SilentlyContinue)) {
    # Zero means the worker never recycles, which keeps long test runs stable.
    $env:PHP_FCGI_MAX_REQUESTS = '0'
    Start-Process -FilePath (Join-Path $PhpRoot 'php-cgi.exe') `
        -ArgumentList '-b', $FastCgiAddress `
        -WorkingDirectory $PhpRoot -WindowStyle Hidden
    Write-Host "started   php-cgi on $FastCgiAddress"
} else {
    Write-Host 'running   php-cgi'
}

if (-not (Get-Process nginx -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath (Join-Path $NginxRoot 'nginx.exe') -WorkingDirectory $NginxRoot -WindowStyle Hidden
    Write-Host 'started   nginx'
} else {
    Write-Host 'running   nginx'
}

# The nginx default client_max_body_size of 1 MiB is lower than MAX_REQUEST_BODY_BYTES,
# so a second endpoint on the PHP built in server is needed to exercise large bodies.
if (-not (Get-Process php -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath (Join-Path $PhpRoot 'php.exe') `
        -ArgumentList '-S', $BuiltinAddress, '-t', $WebRoot `
        -WorkingDirectory $WebRoot -WindowStyle Hidden
    Write-Host "started   php built in server on $BuiltinAddress"
} else {
    Write-Host 'running   php built in server'
}

Start-Sleep -Milliseconds 900
foreach ($url in @('http://127.0.0.1/phpvpn.php', "http://$BuiltinAddress/phpvpn.php")) {
    try {
        $response = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 5
        Write-Host "reachable HTTP $($response.StatusCode)  $url"
    } catch {
        Write-Warning "not reachable yet: $url - $($_.Exception.Message)"
    }
}
