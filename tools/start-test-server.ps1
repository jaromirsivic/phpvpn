<#
.SYNOPSIS
    Starts the private nginx and PHP FastCGI pair used by the integration tests.

.DESCRIPTION
    Every value comes from the testServer and tools sections of the global
    configuration phpvpn.config.json (tools/phpvpn-config.ps1); a parameter
    overrides it for one run. The nginx configuration is rendered from
    tools/test-server/nginx.conf.template into <testServer.directory>/conf, nginx
    runs with that directory as its prefix, so no file of the nginx installation
    is used or changed. The started processes are recorded under
    <testServer.directory>/run and tools/stop-test-server.ps1 stops only those.
#>

[CmdletBinding()]
param(
    [string]$NginxExecutable,
    [string]$PhpCgiExecutable,
    [string]$PhpExecutable,
    [string]$Directory,
    [string]$WebRoot,
    [string]$NginxListen,
    [string]$FastCgiListen,
    [string]$LargeBodyListen,
    [long]$BodyLimitBytes
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'phpvpn-config.ps1')
. (Join-Path $PSScriptRoot 'test-server\test-server-common.ps1')

if (-not $NginxExecutable) { $NginxExecutable = Resolve-PhpVpnTool 'nginx' }
if (-not $PhpCgiExecutable) { $PhpCgiExecutable = Resolve-PhpVpnTool 'phpCgi' 'php-cgi' }
if (-not $PhpExecutable) { $PhpExecutable = Resolve-PhpVpnTool 'php' }
if (-not $Directory) { $Directory = Resolve-PhpVpnConfigPath 'testServer.directory' }
if (-not $WebRoot) { $WebRoot = Resolve-PhpVpnConfigPath 'testServer.webRoot' }
if (-not $NginxListen) { $NginxListen = Get-PhpVpnConfigValue 'testServer.nginxListen' }
if (-not $FastCgiListen) { $FastCgiListen = Get-PhpVpnConfigValue 'testServer.fastCgiListen' }
if (-not $LargeBodyListen) { $LargeBodyListen = Get-PhpVpnConfigValue 'testServer.largeBodyListen' }
if (-not $BodyLimitBytes) { $BodyLimitBytes = [long](Get-PhpVpnConfigValue 'testServer.nginxBodyLimitBytes') }
foreach ($required in 'Directory', 'WebRoot', 'NginxListen', 'FastCgiListen', 'LargeBodyListen') {
    if (-not (Get-Variable -Name $required -ValueOnly)) { throw "testServer value for $required is missing in $(Get-PhpVpnConfigFile)." }
}
if ($BodyLimitBytes -le 0) { throw "testServer.nginxBodyLimitBytes must be positive." }

$state = Get-TestServerState $Directory
foreach ($path in @($state.Conf, $state.Logs, $state.Run, (Join-Path $state.Prefix 'temp'), $WebRoot)) {
    [IO.Directory]::CreateDirectory($path) | Out-Null
}

$template = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'test-server\nginx.conf.template'))
$rendered = $template.Replace('@LISTEN@', $NginxListen).Replace('@FASTCGI@', $FastCgiListen).
    Replace('@WEB_ROOT@', ([IO.Path]::GetFullPath($WebRoot).Replace('\', '/'))).Replace('@BODY_LIMIT@', [string]$BodyLimitBytes)
[IO.File]::WriteAllText($state.NginxConf, $rendered, (New-Object Text.UTF8Encoding($false)))

# nginx -t reports on stderr even on success; Windows PowerShell must not turn that into an error.
$ErrorActionPreference = 'Continue'
$test = & $NginxExecutable -p $state.PrefixArgument -c $state.NginxConf -t 2>&1
$testExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($testExit -ne 0) { throw "The rendered nginx configuration is invalid:`n$($test | Out-String)" }

if (Get-OwnedProcess $state.PhpCgiPid $PhpCgiExecutable $FastCgiListen) {
    Write-Host 'running   php-cgi'
} else {
    Assert-ListenerFree $FastCgiListen
    # Zero means the worker never recycles, which keeps long test runs stable.
    $env:PHP_FCGI_MAX_REQUESTS = '0'
    $process = Start-Process -FilePath $PhpCgiExecutable -ArgumentList '-b', $FastCgiListen `
        -WorkingDirectory (Split-Path -Parent $PhpCgiExecutable) -WindowStyle Hidden -PassThru
    Set-Content -LiteralPath $state.PhpCgiPid -Value $process.Id -Encoding ASCII
    Write-Host "started   php-cgi on $FastCgiListen"
}

if (Get-OwnedProcess $state.NginxPid $NginxExecutable $state.NginxConf) {
    Write-Host 'running   nginx'
} else {
    Assert-ListenerFree $NginxListen
    Start-Process -FilePath $NginxExecutable -ArgumentList '-p', $state.PrefixArgument, '-c', $state.NginxConf `
        -WorkingDirectory $state.Prefix -WindowStyle Hidden
    Write-Host "started   nginx on $NginxListen"
}

# The nginx body limit is lower than MAX_REQUEST_BODY_BYTES, so a second endpoint
# on the PHP built in server is needed to exercise large bodies.
if (Get-OwnedProcess $state.BuiltinPid $PhpExecutable $LargeBodyListen) {
    Write-Host 'running   php built in server'
} else {
    Assert-ListenerFree $LargeBodyListen
    $process = Start-Process -FilePath $PhpExecutable -ArgumentList '-S', $LargeBodyListen, '-t', ('"' + $WebRoot + '"') `
        -WorkingDirectory $WebRoot -WindowStyle Hidden -PassThru
    Set-Content -LiteralPath $state.BuiltinPid -Value $process.Id -Encoding ASCII
    Write-Host "started   php built in server on $LargeBodyListen"
}

Start-Sleep -Milliseconds 900
foreach ($url in @("http://$NginxListen/phpvpn.php", "http://$LargeBodyListen/phpvpn.php")) {
    try {
        $response = Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 5
        Write-Host "reachable HTTP $($response.StatusCode)  $url"
    } catch {
        Write-Warning "not reachable yet: $url - $($_.Exception.Message)"
    }
}
