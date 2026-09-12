@echo off
setlocal
set "GRAFT_LAUNCHER=%~f0"
set "GRAFT_PROJECT_ROOT=%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$text=[IO.File]::ReadAllText($env:GRAFT_LAUNCHER);$marker=':POWERSHELL';Invoke-Expression $text.Substring($text.LastIndexOf($marker)+$marker.Length)"
set "GRAFT_RESULT=%ERRORLEVEL%"
endlocal & exit /b %GRAFT_RESULT%

:POWERSHELL
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($env:GRAFT_PROJECT_ROOT).TrimEnd([IO.Path]::DirectorySeparatorChar)
$runtime = Join-Path $root 'graft\.viz-runtime'
$metadataPath = Join-Path $runtime 'server.json'
$stdoutPath = Join-Path $runtime 'server.stdout.log'
$stderrPath = Join-Path $runtime 'server.stderr.log'

function Fail([string]$message) {
    Write-Host "Chyba: $message" -ForegroundColor Red
    exit 1
}

function Remove-OwnedRuntime {
    Remove-Item -LiteralPath $metadataPath, $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $runtime -PathType Container) -and
        -not (Get-ChildItem -LiteralPath $runtime -Force -ErrorAction SilentlyContinue)) {
        Remove-Item -LiteralPath $runtime -Force -ErrorAction SilentlyContinue
    }
}

function Get-RecordedProcess($record) {
    if (-not $record -or -not $record.pid -or -not $record.cli -or -not $record.root) { return $null }
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($record.pid)" -ErrorAction SilentlyContinue
    if (-not $process -or -not $process.CommandLine -or $process.Name -notlike 'node*') { return $null }
    if ($process.CommandLine.IndexOf([string]$record.cli, [StringComparison]::OrdinalIgnoreCase) -lt 0) { return $null }
    if ($process.CommandLine.IndexOf([string]$record.root, [StringComparison]::OrdinalIgnoreCase) -lt 0) { return $null }
    return $process
}

function Wait-ForGraph($record, [int]$seconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (-not (Get-RecordedProcess $record)) { return $false }
        try {
            $response = Invoke-WebRequest -UseBasicParsing -Uri $record.url -TimeoutSec 1
            if ($response.StatusCode -eq 200) { return $true }
        } catch {}
        Start-Sleep -Milliseconds 200
    }
    return $false
}

function Open-Graph([string]$url) {
    if ($env:GRAFT_VIZ_NO_OPEN -eq '1') { return }
    try {
        Start-Process $url | Out-Null
    } catch {
        Fail "Graf běží na $url, ale výchozí prohlížeč se nepodařilo otevřít: $($_.Exception.Message)"
    }
}

function Find-GraftCli {
    if ($env:GRAFT_CLI) {
        if (Test-Path -LiteralPath $env:GRAFT_CLI -PathType Leaf) {
            return [IO.Path]::GetFullPath($env:GRAFT_CLI)
        }
        Fail "Graft CLI nebyl nalezen v cestě GRAFT_CLI: $env:GRAFT_CLI"
    }

    $candidates = [Collections.Generic.List[string]]::new()
    foreach ($name in 'graft.cmd', 'graft.ps1', 'graft') {
        $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($command -and $command.Source) {
            $candidates.Add((Join-Path (Split-Path -Parent $command.Source) 'node_modules\@nanonets\graft\dist\cli.js'))
        }
    }
    foreach ($base in @($env:APPDATA, $env:LOCALAPPDATA)) {
        if ($base) { $candidates.Add((Join-Path $base 'npm\node_modules\@nanonets\graft\dist\cli.js')) }
    }
    $packages = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Packages'
    if (Test-Path -LiteralPath $packages -PathType Container) {
        foreach ($package in Get-ChildItem -LiteralPath $packages -Directory -Filter 'OpenAI.Codex_*' -ErrorAction SilentlyContinue) {
            $candidates.Add((Join-Path $package.FullName 'LocalCache\Roaming\npm\node_modules\@nanonets\graft\dist\cli.js'))
        }
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    Fail 'Graft CLI nebyl nalezen. Nainstaluj Graft nebo nastav GRAFT_CLI na úplnou cestu k dist\cli.js.'
}

function Find-Node {
    if ($env:GRAFT_NODE) {
        if (Test-Path -LiteralPath $env:GRAFT_NODE -PathType Leaf) { return [IO.Path]::GetFullPath($env:GRAFT_NODE) }
        Fail "Node.js nebyl nalezen v cestě GRAFT_NODE: $env:GRAFT_NODE"
    }
    $fixed = 'C:\Program Files\nodejs\node.exe'
    if (Test-Path -LiteralPath $fixed -PathType Leaf) { return $fixed }
    $command = Get-Command node.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command -and $command.Source) { return $command.Source }
    Fail 'Node.js nebyl nalezen. Nastav GRAFT_NODE na úplnou cestu k node.exe.'
}

$hashProvider = [Security.Cryptography.SHA256]::Create()
try {
    $hash = [BitConverter]::ToString($hashProvider.ComputeHash([Text.Encoding]::UTF8.GetBytes($root))).Replace('-', '')
} finally {
    $hashProvider.Dispose()
}
$mutex = [Threading.Mutex]::new($false, "Local\GraftViz-$($hash.Substring(0, 24))")
$locked = $false
try {
    $locked = $mutex.WaitOne([TimeSpan]::FromSeconds(20))
    if (-not $locked) { Fail 'Jiný start nebo stop Graft serveru se nedokončil včas.' }

    if (Test-Path -LiteralPath $metadataPath -PathType Leaf) {
        try { $record = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json } catch { $record = $null }
        if ($record -and (Get-RecordedProcess $record)) {
            if (Wait-ForGraph $record 20) {
                Write-Host "Graft server už běží (PID $($record.pid)) na $($record.url)"
                Open-Graph $record.url
                exit 0
            }
            Stop-Process -Id $record.pid -Force -ErrorAction SilentlyContinue
        }
        Remove-OwnedRuntime
    }

    $cli = Find-GraftCli
    $node = Find-Node
    New-Item -ItemType Directory -Path $runtime -Force | Out-Null

    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    try {
        $listener.Start()
        $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    } finally {
        $listener.Stop()
    }
    $url = "http://127.0.0.1:$port/"
    $quotedCli = '"' + $cli.Replace('"', '\"') + '"'
    $quotedRoot = '"' + $root.Replace('"', '\"') + '"'
    $arguments = "$quotedCli viz $quotedRoot --no-open --port $port"
    $server = Start-Process -FilePath $node -ArgumentList $arguments -WorkingDirectory $root `
        -WindowStyle Hidden -PassThru
    $record = [pscustomobject]@{
        pid = $server.Id
        port = $port
        url = $url
        root = $root
        cli = $cli
        startedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $record | ConvertTo-Json | Set-Content -LiteralPath $metadataPath -Encoding UTF8

    if (-not (Wait-ForGraph $record 30)) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        Remove-OwnedRuntime
        Fail 'Graft server se do 30 sekund nezpřístupnil.'
    }

    Write-Host "Graft server běží na pozadí (PID $($record.pid)) na $url"
    Open-Graph $url
    exit 0
} catch {
    Fail $_.Exception.Message
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
