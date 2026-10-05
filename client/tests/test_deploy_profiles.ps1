# Isolated regression for the two explicit fixture profiles and fail-closed header checks.
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sandbox = Join-Path $repositoryRoot ('work\build\deploy-profile-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Join-Path $sandbox 'tools'), (Join-Path $sandbox 'web') | Out-Null
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'tools\deploy-test.ps1') -Destination (Join-Path $sandbox 'tools\deploy-test.ps1')
$source = [IO.File]::ReadAllText((Join-Path $repositoryRoot 'phpvpn.php'))
[IO.File]::WriteAllText((Join-Path $sandbox 'phpvpn.php'), $source)
$deploy = Join-Path $sandbox 'tools\deploy-test.ps1'
$fixture = Join-Path $sandbox 'fixture.json'
# Explicit URLs keep the sandbox independent of the global configuration.
$urls = @{ BaseUrl = 'http://127.0.0.1:1/phpvpn.php'; LargeBodyBaseUrl = 'http://127.0.0.1:2/phpvpn.php' }
foreach ($profile in 'integration', 'wizard-default') {
    & $deploy -WebRoot (Join-Path $sandbox 'web') -BufferDirectory (Join-Path $sandbox 'buffer') -Profile $profile -ConfigurationPath $fixture @urls
    $config = [IO.File]::ReadAllText($fixture) | ConvertFrom-Json
    $expectedCount = if ($profile -eq 'integration') { 4 } else { 14 }
    if (@($config.accessKeys.PSObject.Properties).Count -ne $expectedCount) { throw 'Wrong profile key count.' }
    if ($profile -eq 'wizard-default') {
        foreach ($octet in 2..15) {
            if ($config.accessKeys."10.99.99.$octet" -ne (('{0:x2}' -f $octet) * 32)) { throw 'Wrong default access key.' }
        }
    } elseif ($config.serverSecret -ne 'phpvpn-integration-test-secret-2026') { throw 'Custom fixture secret changed.' }
}
$target = Join-Path $sandbox 'web\phpvpn.php'
$before = [IO.File]::ReadAllText($target)
foreach ($mutation in 'secret', 'keys', 'buffer') {
    $invalid = if ($mutation -eq 'secret') { $source.Replace("const SERVER_SECRET = 'CHANGE_THIS_SERVER_SECRET';", "const SERVER_SECRET = 'unexpected';") }
               elseif ($mutation -eq 'buffer') { $source.Replace("const BUFFER_DIRECTORY = './phpvpn_data';", "const BUFFER_DIRECTORY = './unexpected';") }
               else { $source.Replace("'10.99.99.15' =>", "'10.99.99.16' =>") }
    [IO.File]::WriteAllText((Join-Path $sandbox 'phpvpn.php'), $invalid)
    $rejected = $false
    try { & $deploy -WebRoot (Join-Path $sandbox 'web') -BufferDirectory (Join-Path $sandbox 'buffer') -ConfigurationPath $fixture @urls }
    catch { $rejected = $true }
    if (-not $rejected -or [IO.File]::ReadAllText($target) -ne $before) { throw 'Unexpected header was not rejected before publication.' }
}
Write-Output 'PASS: integration/default profiles and unexpected header rejection.'
Write-Output "Evidence directory: $sandbox"
