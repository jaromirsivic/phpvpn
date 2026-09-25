# Bootstrap deliberately uses only Windows PowerShell until uv supplies Python.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
$lock = Join-Path $root '.bootstrap.lock'
$owned = $false
$installer = $null

function Assert-NoReparse([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse path is not allowed: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Invoke-Uv([string[]]$Arguments) {
    & $script:uv --no-config @Arguments
    if ($LASTEXITCODE -ne 0) { throw "uv failed with exit code $LASTEXITCODE." }
}

try {
    Assert-NoReparse $root
    foreach ($name in @('phpvpn.py', 'requirements-server.txt', 'server-control.py')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf)) { throw "Missing distribution file: $name" }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while (-not $owned) {
        try {
            New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null
            $owned = $true
        } catch {
            if (-not (Test-Path -LiteralPath $lock)) { throw }
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Initialization lock is busy. If a previous init was interrupted, inspect it before removing .bootstrap.lock.' }
            Start-Sleep -Milliseconds 200
        }
    }
    $runtimeLock = Join-Path $root '.runtime/instance.lock'
    Assert-NoReparse $runtimeLock
    if (Test-Path -LiteralPath $runtimeLock) {
        $stream = [IO.File]::Open($runtimeLock, 'Open', 'ReadWrite', 'ReadWrite')
        try { $stream.Lock(0, 1); $stream.Unlock(0, 1) }
        catch { throw 'Server is running or its instance lock is inaccessible. Stop it before init.' }
        finally { $stream.Dispose() }
    }
    $uv = $null
    $existing = Get-Command uv -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $privateUv = Join-Path $root '.tools/uv/uv.exe'
    Assert-NoReparse $privateUv
    foreach ($candidate in @($(if ($existing) { $existing.Source }), $privateUv)) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            & $candidate --version
            if ($LASTEXITCODE -eq 0) { $uv = $candidate; break }
        }
    }
    $env:UV_HTTP_TIMEOUT = '30'
    $env:UV_HTTP_RETRIES = '2'
    $env:UV_NO_MODIFY_PATH = '1'
    $env:UV_INSTALL_DIR = Join-Path $root '.tools/uv'
    Remove-Item Env:VIRTUAL_ENV, Env:PYTHONHOME, Env:PYTHONPATH -ErrorAction SilentlyContinue
    if (-not $uv) {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $installer = Join-Path $lock 'install-uv.ps1'
        Invoke-WebRequest -UseBasicParsing -Uri 'https://astral.sh/uv/install.ps1' -OutFile $installer -TimeoutSec 60
        $process = Start-Process powershell.exe -WindowStyle Hidden -PassThru -ArgumentList @(
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $installer + '"'))
        if (-not $process.WaitForExit(300000)) { $process.Kill(); throw 'uv installer timed out.' }
        if ($process.ExitCode -ne 0) { throw 'Official uv installer failed.' }
        $uv = $privateUv
        & $uv --version
        if ($LASTEXITCODE -ne 0) { throw 'Installed uv is not usable.' }
    }
    Invoke-Uv @('python', 'install', '--no-bin', '--no-registry', '3.13')
    $python = & $uv --no-config python find --managed-python 3.13
    if ($LASTEXITCODE -ne 0 -or -not $python) { throw 'Managed Python 3.13 was not found.' }
    & $python -I -B (Join-Path $root 'server-control.py') init $uv
    if ($LASTEXITCODE -ne 0) { throw 'Environment initialization failed.' }
    exit 0
} catch {
    [Console]::Error.WriteLine('ERROR: ' + $_.Exception.Message)
    exit 1
} finally {
    if ($owned) {
        if ($installer -and (Test-Path -LiteralPath $installer)) { Remove-Item -LiteralPath $installer -Force }
        # Only the exact directory created by this invocation, never recursive.
        [IO.Directory]::Delete($lock)
    }
}
