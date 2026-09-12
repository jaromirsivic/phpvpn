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

    if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
        Remove-OwnedRuntime
        Write-Host 'Graft server spravovaný tímto projektem neběží.'
        exit 0
    }

    try { $record = Get-Content -Raw -LiteralPath $metadataPath | ConvertFrom-Json } catch { $record = $null }
    $process = Get-RecordedProcess $record
    if (-not $process) {
        Remove-OwnedRuntime
        Write-Host 'PID metadata byla zastaralá nebo neplatná. Žádný proces nebyl ukončen.'
        exit 0
    }

    Stop-Process -Id $record.pid -Force
    try { Wait-Process -Id $record.pid -Timeout 10 -ErrorAction SilentlyContinue } catch {}
    Remove-OwnedRuntime
    Write-Host "Graft server (PID $($record.pid)) byl ukončen. Prohlížeč zůstal beze změny."
    exit 0
} catch {
    Fail $_.Exception.Message
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
