$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Invoke-Launcher {
    param([string]$Path)
    $output = & $Path 2>&1 | Out-String
    [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

$root = Split-Path -Parent $PSScriptRoot
$start = Join-Path $root 'graft start.bat'
$stop = Join-Path $root 'graft stop.bat'
$runtime = Join-Path $root 'graft\.viz-runtime'
$metadata = Join-Path $runtime 'server.json'
$sleeper = $null
$savedPath = $env:PATH
$savedAppData = $env:APPDATA

Assert-True (Test-Path -LiteralPath $start -PathType Leaf) 'graft start.bat is missing'
Assert-True (Test-Path -LiteralPath $stop -PathType Leaf) 'graft stop.bat is missing'

$env:GRAFT_VIZ_NO_OPEN = '1'
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $env:APPDATA = Join-Path $env:TEMP 'missing-graft-appdata'
    $cleanup = Invoke-Launcher $stop
    Assert-True ($cleanup.ExitCode -eq 0) "Initial stop failed:`n$($cleanup.Output)"

    $first = Invoke-Launcher $start
    Assert-True ($first.ExitCode -eq 0) "First start failed:`n$($first.Output)"
    Assert-True (Test-Path -LiteralPath $metadata -PathType Leaf) 'Start did not write server metadata'
    $firstMetadata = Get-Content -Raw -LiteralPath $metadata | ConvertFrom-Json
    $firstProcess = Get-Process -Id $firstMetadata.pid -ErrorAction SilentlyContinue
    Assert-True ($null -ne $firstProcess) 'Managed server process is not running after start returned'
    $response = Invoke-WebRequest -UseBasicParsing -Uri $firstMetadata.url -TimeoutSec 3
    Assert-True ($response.StatusCode -eq 200) 'Managed graph URL did not return HTTP 200'

    $second = Invoke-Launcher $start
    Assert-True ($second.ExitCode -eq 0) "Second start failed:`n$($second.Output)"
    $secondMetadata = Get-Content -Raw -LiteralPath $metadata | ConvertFrom-Json
    Assert-True ($secondMetadata.pid -eq $firstMetadata.pid) 'Second start replaced the managed server instead of reusing it'
    Assert-True ($null -ne (Get-Process -Id $firstMetadata.pid -ErrorAction SilentlyContinue)) 'Managed server died after second start'

    $sleeper = Start-Process -FilePath powershell.exe -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 120' -WindowStyle Hidden -PassThru
    $stopped = Invoke-Launcher $stop
    Assert-True ($stopped.ExitCode -eq 0) "Stop failed:`n$($stopped.Output)"
    Assert-True ($null -eq (Get-Process -Id $firstMetadata.pid -ErrorAction SilentlyContinue)) 'Stop left the managed server running'
    Assert-True ($null -ne (Get-Process -Id $sleeper.Id -ErrorAction SilentlyContinue)) 'Stop terminated an unrelated process'
    Assert-True (-not (Test-Path -LiteralPath $metadata)) 'Stop left server metadata behind'

    New-Item -ItemType Directory -Path $runtime -Force | Out-Null
    [pscustomobject]@{
        pid = $sleeper.Id
        port = 4400
        url = 'http://127.0.0.1:4400/'
        root = $root
        cli = 'stale-cli.js'
    } | ConvertTo-Json | Set-Content -LiteralPath $metadata -Encoding UTF8
    $stale = Invoke-Launcher $stop
    Assert-True ($stale.ExitCode -eq 0) "Stale-PID stop failed:`n$($stale.Output)"
    Assert-True ($null -ne (Get-Process -Id $sleeper.Id -ErrorAction SilentlyContinue)) 'Stale metadata caused an unrelated process to be terminated'
    Assert-True (-not (Test-Path -LiteralPath $metadata)) 'Stale metadata was not removed'

    $idle = Invoke-Launcher $stop
    Assert-True ($idle.ExitCode -eq 0) "Idle stop failed:`n$($idle.Output)"

    $env:GRAFT_CLI = Join-Path $root 'missing-graft-cli.js'
    $missingCli = Invoke-Launcher $start
    Assert-True ($missingCli.ExitCode -ne 0) 'Invalid GRAFT_CLI unexpectedly started a server'
    Assert-True ($missingCli.Output -like '*Graft CLI nebyl nalezen*') "Missing-CLI error was unclear:`n$($missingCli.Output)"
    Write-Host 'PASS: Graft launchers manage exactly one server and stop only their recorded process.'
} finally {
    Remove-Item Env:GRAFT_VIZ_NO_OPEN -ErrorAction SilentlyContinue
    Remove-Item Env:GRAFT_CLI -ErrorAction SilentlyContinue
    $env:PATH = $savedPath
    $env:APPDATA = $savedAppData
    if ($sleeper -and -not $sleeper.HasExited) { Stop-Process -Id $sleeper.Id -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $stop) { & $stop | Out-Null }
}
