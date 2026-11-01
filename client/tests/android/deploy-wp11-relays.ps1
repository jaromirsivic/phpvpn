<#
.SYNOPSIS
    Deploys the relays of the WP11 Android test matrix into the host nginx.

.DESCRIPTION
    The Android instrumented tests and the host scripts in this folder use the
    profile wizard-default of tools/deploy-test.ps1 (default server secret and
    the default access keys 10.99.99.2 to 10.99.99.15). One emulator per relay:
    two emulators that both connect as 10.99.99.2 to the same buffer would take
    each other's packets. Each relay gets its own buffer outside the web root.

      <WebRoot>/wp11/api34/phpvpn.php   http://10.0.2.2/wp11/api34/phpvpn.php
      <WebRoot>/wp11/api35/phpvpn.php   http://10.0.2.2/wp11/api35/phpvpn.php
      <WebRoot>/wp11/api36/phpvpn.php   http://10.0.2.2/wp11/api36/phpvpn.php
      <WebRoot>/wp11/v2/phpvpn.php      SERVER_VERSION 2.0.0 (T-A14)

    The relay of the peer tests (T-A03, T-A08, T-A21) is the existing
    <WebRoot>/phpvpn.php that the Windows client on the host uses; it is not
    touched. Run start.bat of the nginx stack first. Remove the relays with
    -Remove.
#>
[CmdletBinding()]
param(
    [string]$NginxRoot = 'D:\nginx',
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$webRoot = Join-Path $NginxRoot 'html\wp11'
$bufferRoot = Join-Path $NginxRoot 'wp11-buffer'
$descriptionRoot = Join-Path $repository 'work\wp11\relays'

if ($Remove) {
    foreach ($path in $webRoot, $bufferRoot) {
        if (Test-Path $path) { Remove-Item -Recurse -Force $path; Write-Host "removed   $path" }
    }
    return
}

foreach ($name in 'api34', 'api35', 'api36', 'v2') {
    $url = "http://10.0.2.2/wp11/$name/phpvpn.php"
    & (Join-Path $repository 'tools\deploy-test.ps1') -Profile wizard-default `
        -WebRoot (Join-Path $webRoot $name) -BufferDirectory (Join-Path $bufferRoot $name) `
        -BaseUrl $url -LargeBodyBaseUrl $url -ConfigurationPath (Join-Path $descriptionRoot "$name.json")
}

# T-A14: a server of another major version.
$v2 = Join-Path $webRoot 'v2\phpvpn.php'
$text = [IO.File]::ReadAllText($v2)
$pattern = "const SERVER_VERSION = '[0-9]+\.[0-9]+\.[0-9]+';"
if ([regex]::Matches($text, $pattern).Count -ne 1) { throw 'SERVER_VERSION not found exactly once.' }
$text = [regex]::Replace($text, $pattern, "const SERVER_VERSION = '2.0.0';")
[IO.File]::WriteAllText($v2, $text, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "patched   $v2 (SERVER_VERSION 2.0.0)"

foreach ($name in 'api34', 'api35', 'api36', 'v2') {
    $probe = & curl.exe -s -m 5 "http://127.0.0.1/wp11/$name/phpvpn.php" -o NUL -w '%{http_code}'
    Write-Host "probe     /wp11/$name/phpvpn.php -> HTTP $probe"
}
