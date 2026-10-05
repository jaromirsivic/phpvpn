<#
.SYNOPSIS
    Deploys phpvpn.php into the web root of the private test server for integration testing.

.DESCRIPTION
    Copies phpvpn.php to $WebRoot and rewrites the configuration header so that
    the deployed instance uses a known test secret, known access keys and a
    buffer directory placed outside the web root. Also writes the matching
    deployment description used by the Python integration tests.

    Omitted parameters come from the testServer section of the global
    configuration phpvpn.config.json (tools/phpvpn-config.ps1). Nothing outside
    the deployment target and the description is modified.
#>

[CmdletBinding()]
param(
    [string]$WebRoot,
    [string]$BufferDirectory,
    [string]$BaseUrl,
    [string]$LargeBodyBaseUrl,
    [ValidateSet('integration', 'wizard-default')][string]$Profile = 'integration',
    [string]$ConfigurationPath
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
if (-not ($WebRoot -and $BufferDirectory -and $BaseUrl -and $LargeBodyBaseUrl -and $ConfigurationPath)) {
    . (Join-Path $PSScriptRoot 'phpvpn-config.ps1')
    if (-not $WebRoot) { $WebRoot = Resolve-PhpVpnConfigPath 'testServer.webRoot' }
    if (-not $BufferDirectory) { $BufferDirectory = Resolve-PhpVpnConfigPath 'testServer.bufferDirectory' }
    if (-not $BaseUrl) { $BaseUrl = "http://$(Get-PhpVpnConfigValue 'testServer.nginxListen')/phpvpn.php" }
    if (-not $LargeBodyBaseUrl) { $LargeBodyBaseUrl = "http://$(Get-PhpVpnConfigValue 'testServer.largeBodyListen')/phpvpn.php" }
    if (-not $ConfigurationPath) { $ConfigurationPath = Resolve-PhpVpnConfigPath 'testServer.deploymentDescription' }
    foreach ($required in 'WebRoot', 'BufferDirectory', 'ConfigurationPath') {
        if (-not (Get-Variable -Name $required -ValueOnly)) { throw "The testServer value for $required is missing in $(Get-PhpVpnConfigFile)." }
    }
}
$WebRoot = [IO.Path]::GetFullPath($WebRoot)
$BufferDirectory = [IO.Path]::GetFullPath($BufferDirectory)

$secret = 'phpvpn-integration-test-secret-2026'
$keys = [ordered]@{
    '10.99.99.2' = 'a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90'
    '10.99.99.3' = 'b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90a1'
    '10.99.99.4' = 'c3d4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2'
    '10.99.99.5' = 'd4e5f60718293a4b5c6d7e8f90a1b2c3d4e5f60718293a4b5c6d7e8f90a1b2c3'
}

if ($Profile -eq 'wizard-default') {
    $secret = 'CHANGE_THIS_SERVER_SECRET'
    $keys = [ordered]@{}
    foreach ($octet in 2..15) { $keys["10.99.99.$octet"] = ('{0:x2}' -f $octet) * 32 }
}
$source = Join-Path $repository 'phpvpn.php'
$content = [System.IO.File]::ReadAllText($source).Replace("`r`n", "`n")
function Replace-ExactlyOnce {
    param([string]$Text, [string]$Expected, [string]$Replacement, [string]$Label)
    if ([regex]::Matches($Text, [regex]::Escape($Expected)).Count -ne 1) {
        throw "The $Label header does not match exactly one expected default."
    }
    return $Text.Replace($Expected, $Replacement)
}
$content = Replace-ExactlyOnce $content "const SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET';" "const SERVER_SECRET = '$secret';" 'SERVER_SECRET'
$defaultKeyLines = (2..15 | ForEach-Object { "    '10.99.99.$_' => '$(('{0:x2}' -f $_) * 32)'," }) -join "`n"
$oldKeys = "const ACCESS_KEYS = [`n$defaultKeyLines`n];"
$newKeyLines = ($keys.Keys | ForEach-Object { "    '$_' => '$($keys[$_])'," }) -join "`n"
$newKeys = "const ACCESS_KEYS = [`n$newKeyLines`n];"
$content = Replace-ExactlyOnce $content $oldKeys $newKeys 'ACCESS_KEYS'
$escapedBuffer = $BufferDirectory.Replace('\', '/').Replace("'", "\'")
$content = Replace-ExactlyOnce $content "const BUFFER_DIRECTORY = './phpvpn_data';" "const BUFFER_DIRECTORY = '$escapedBuffer';" 'BUFFER_DIRECTORY'

[IO.Directory]::CreateDirectory($WebRoot) | Out-Null
$target = Join-Path $WebRoot 'phpvpn.php'
[System.IO.File]::WriteAllText($target, $content, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "deployed  $target"

if (-not (Test-Path $BufferDirectory)) {
    New-Item -ItemType Directory -Path $BufferDirectory | Out-Null
}
Write-Host "buffer    $BufferDirectory"

$configuration = [ordered]@{
    baseUrl          = $BaseUrl
    # nginx caps request bodies (testServer.nginxBodyLimitBytes), this endpoint has no such cap.
    largeBodyBaseUrl = $LargeBodyBaseUrl
    serverSecret     = $secret
    accessKeys       = $keys
    bufferDirectory  = $BufferDirectory
    deployedFile     = $target
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $configurationPath) | Out-Null
# Windows PowerShell writes a byte order mark with -Encoding utf8, which breaks JSON parsers.
[System.IO.File]::WriteAllText(
    $configurationPath,
    ($configuration | ConvertTo-Json -Depth 4),
    (New-Object System.Text.UTF8Encoding($false)))
Write-Host "config    $configurationPath"
