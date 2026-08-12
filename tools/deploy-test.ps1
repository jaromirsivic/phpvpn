<#
.SYNOPSIS
    Deploys phpvpn.php into the local nginx web root for integration testing.

.DESCRIPTION
    Copies phpvpn.php to $WebRoot and rewrites the configuration header so that
    the deployed instance uses a known test secret, known access keys and a
    buffer directory placed outside the web root. Also writes the matching
    client configuration used by the Python integration tests.

    Nothing outside the deployment target is modified, nginx.conf is left as is.
#>

[CmdletBinding()]
param(
    [string]$WebRoot = 'D:\nginx\html',
    [string]$BufferDirectory = 'D:/nginx/phpvpn-buffer',
    [string]$BaseUrl = 'http://127.0.0.1/phpvpn.php',
    [string]$LargeBodyBaseUrl = 'http://127.0.0.1:8081/phpvpn.php'
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot

$secret = 'phpvpn-integration-test-secret-2026'
$keys = [ordered]@{
    '10.99.99.2' = 'a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90'
    '10.99.99.3' = 'b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90a1'
    '10.99.99.4' = 'c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2'
    '10.99.99.5' = 'd4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3'
}

$source = Join-Path $repository 'phpvpn.php'
$content = [System.IO.File]::ReadAllText($source)

$content = $content.Replace(
    "const SERVER_SECRET = '00000000000000000000000000000000';",
    "const SERVER_SECRET = '$secret';")

$oldKeys = @"
const ACCESS_KEYS = [
    '10.99.99.2' => '0202020202020202020202020202020202020202020202020202020202020202',
    '10.99.99.3' => '0303030303030303030303030303030303030303030303030303030303030303',
    '10.99.99.4' => '0404040404040404040404040404040404040404040404040404040404040404',
];
"@ -replace "`r`n", "`n"

$newKeyLines = ($keys.Keys | ForEach-Object { "    '$_' => '$($keys[$_])'," }) -join "`n"
$newKeys = "const ACCESS_KEYS = [`n$newKeyLines`n];"

if (-not $content.Contains($oldKeys)) {
    throw 'The ACCESS_KEYS block in phpvpn.php does not match the expected default.'
}
$content = $content.Replace($oldKeys, $newKeys)

$content = $content.Replace(
    "const BUFFER_DIRECTORY = './phpvpn';",
    "const BUFFER_DIRECTORY = '$BufferDirectory';")

$target = Join-Path $WebRoot 'phpvpn.php'
[System.IO.File]::WriteAllText($target, $content, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "deployed  $target"

if (-not (Test-Path $BufferDirectory)) {
    New-Item -ItemType Directory -Path $BufferDirectory | Out-Null
}
Write-Host "buffer    $BufferDirectory"

$configuration = [ordered]@{
    baseUrl          = $BaseUrl
    # nginx caps request bodies at 1 MiB by default, this endpoint has no such cap.
    largeBodyBaseUrl = $LargeBodyBaseUrl
    serverSecret     = $secret
    accessKeys       = $keys
    bufferDirectory  = $BufferDirectory
    deployedFile     = $target
}
$configurationPath = Join-Path $repository 'tests-integration\server.json'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $configurationPath) | Out-Null
# Windows PowerShell writes a byte order mark with -Encoding utf8, which breaks JSON parsers.
[System.IO.File]::WriteAllText(
    $configurationPath,
    ($configuration | ConvertTo-Json -Depth 4),
    (New-Object System.Text.UTF8Encoding($false)))
Write-Host "config    $configurationPath"
