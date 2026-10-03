# Explicit desktop regression. Each view must be the first GUI in a fresh PID.
# Uses only the private preview backend, never the installed service/config.
[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
& (Join-Path $root 'client/tools/dev-tray.ps1') -Mode Build -Configuration $Configuration
if ($LASTEXITCODE) { throw 'First-open harness build failed' }
$exe = Join-Path $root "work/build/tray-dev-$($Configuration.ToLowerInvariant())/tests/phpvpn-tray-ui-tests.exe"
$logs = Join-Path $root ('work/logs/first-open/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $logs | Out-Null
foreach ($view in @('Wizard', 'Contracts')) {
    foreach ($hidden in @($false, $true)) {
        $name = "$view-hidden-$hidden"
        $stdout = Join-Path $logs "$name.stdout.log"
        $stderr = Join-Path $logs "$name.stderr.log"
        $launch = @{
            FilePath = $exe
            ArgumentList = @('--first-open', $view, '--installation-complete')
            PassThru = $true
            RedirectStandardOutput = $stdout
            RedirectStandardError = $stderr
        }
        # Negative control proves that the old installer launch is detected.
        if ($hidden) { $launch.WindowStyle = 'Hidden' }
        $process = Start-Process @launch
        try {
            $null = $process.Handle
            if (-not $process.WaitForExit(20000)) {
                $process.Kill()
                $process.WaitForExit()
                throw "First-open timed out: $name"
            }
            $code = $process.ExitCode
        } finally { $process.Dispose() }
        $output = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
        $expectedCode = if ($hidden) { 1 } else { 0 }
        $expected = "[ FIRST OPEN ] $view startupHidden=$([int]$hidden) selections=1 visible=$([int](-not $hidden))"
        if ($code -ne $expectedCode -or -not $output.Contains($expected) -or
            ($hidden -and -not $output.Contains('First tray selection created an invisible window'))) {
            throw "First-open regression failed: $name, exit=$code. Evidence: $logs`n$output"
        }
        Write-Output "PASS: $name, exit=$code, $expected"
    }
}
Write-Output "First-open regression passed ($Configuration). Evidence: $logs"
