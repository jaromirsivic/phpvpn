# Explicit desktop regression for F09/A32. No production executable is run.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$logs = Join-Path $root ('work/logs/hidden-launch/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N'))
$otherCwd = Join-Path $logs 'other cwd'
$copyRoot = Join-Path $logs 'project with spaces'
New-Item -ItemType Directory -Path $otherCwd,$copyRoot -Force | Out-Null
$results = New-Object 'Collections.Generic.List[object]'
$powershell = Join-Path $PSHOME 'powershell.exe'
if (!(Test-Path -LiteralPath $powershell)) { throw 'Run this Windows regression with Windows PowerShell 5.1.' }
function Quote-Argument([string]$Value) {
    return '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
function Invoke-Runner([string]$Project, [string]$Name, [string[]]$Arguments, [bool]$Pass,
                       [string]$Expected, [bool]$NoChild = $false) {
    $runner = Join-Path $Project 'client/tools/dev-tray.ps1'
    $argv = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$runner) + $Arguments
    $before = @(Get-ChildItem -LiteralPath (Join-Path $Project 'work/logs/tray-dev') -Directory -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $process = Start-Process -FilePath $powershell -ArgumentList (($argv | ForEach-Object { Quote-Argument $_ }) -join ' ') -WorkingDirectory $otherCwd -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logs "$Name.stdout.log") -RedirectStandardError (Join-Path $logs "$Name.stderr.log")
    try {
        $null = $process.Handle
        if (!$process.WaitForExit(240000)) { throw "Runner timed out: $Name, PID $($process.Id). Inspect this owned process before cleanup." }
        $code = $process.ExitCode
    } finally { $process.Dispose() }
    $output = [IO.File]::ReadAllText((Join-Path $logs "$Name.stdout.log")) + [IO.File]::ReadAllText((Join-Path $logs "$Name.stderr.log"))
    $created = @(Get-ChildItem -LiteralPath (Join-Path $Project 'work/logs/tray-dev') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notin $before })
    $summary = $null
    if ($created.Count -eq 1) { $summary = Get-Content -Raw -LiteralPath (Join-Path $created[0].FullName 'summary.json') | ConvertFrom-Json }
    $hasChild = $summary -and $summary.PSObject.Properties.Name -contains 'ChildProcessId'
    $ok = ($null -ne $code) -and (($code -eq 0) -eq $Pass) -and ($output -match $Expected) -and (!$NoChild -or !$hasChild)
    if ($hasChild -and (Get-Process -Id $summary.ChildProcessId -ErrorAction SilentlyContinue)) { $ok = $false }
    $results.Add([pscustomobject]@{Name=$Name;ExitCode=$code;Passed=$ok;RunnerLogs=@($created | ForEach-Object FullName);NoChildRequired=$NoChild;ChildStarted=[bool]$hasChild})
    $results | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $logs 'results.json')
    Write-Host "$Name : exit=$code passed=$ok"
    if (!$ok) { throw "Hidden launcher regression failed: $Name. Evidence: $logs" }
    return $summary
}
# A fresh source copy exercises the real build and executable paths with spaces.
# Negative mutations below are confined to this unique copy, never the checkout.
Copy-Item -LiteralPath (Join-Path $root 'client') -Destination $copyRoot -Recurse
foreach ($project in @($root,$copyRoot)) {
    $label = if ($project -eq $root) { 'checkout' } else { 'spaces' }
    foreach ($configuration in @('Debug','Release')) {
        $runArguments = @('-Mode','Test','-Configuration',$configuration,'-Filter','editor_reset_failure_generation')
        $summary = Invoke-Runner $project "$label-$configuration-editor" $runArguments $true '\[ EDITOR LAUNCH \] startupHidden=0 console=0 visible=1 focus=1[\s\S]*1 real Win32 UI tests, 0 failed\.'
        if (!$summary -or $summary.ExitCode -ne 0) { throw 'Missing successful runner summary.' }
        $summary = Invoke-Runner $project "$label-$configuration-noop" @('-Mode','Build','-Configuration',$configuration) $true 'ninja: no work to do\.' $true
        $null = Invoke-Runner $project "$label-$configuration-zero" @('-Mode','Test','-Configuration',$configuration,'-Filter','no_such_hidden_launch_test') $false '0 real Win32 UI tests, 0 failed\.'
    }
}
$null = Invoke-Runner $root 'invalid-scene' @('-View','Editor','-Scene','Wrong') $false 'Invalid View/Scene' $true
$null = Invoke-Runner $root 'missing-filter' @('-Mode','Test') $false 'Test requires a nonempty substring Filter' $true
$null = Invoke-Runner $root 'wrong-mode' @('-Mode','Build','-View','Editor') $false 'View and Scene are only valid in Preview mode' $true
$source = Join-Path $copyRoot 'client/tests/ui/tray_ui_tests.cpp'
$original = [IO.File]::ReadAllBytes($source)
try {
    [IO.File]::AppendAllText($source, "`n#error hidden_launch_expected_compile_failure`n")
    $null = Invoke-Runner $copyRoot 'compile-failure' @('-View','Editor') $false 'build failed with exit code' $true
} finally { [IO.File]::WriteAllBytes($source, $original) }
$null = Invoke-Runner $copyRoot 'rebuild-after-failure' @('-Mode','Build') $true 'Build:' $true
$exe = Join-Path $copyRoot 'work/build/tray-dev-debug/tests/phpvpn-tray-ui-tests.exe'
$originalPe = [IO.File]::ReadAllBytes($exe)
try {
    [IO.File]::WriteAllBytes($exe, [byte[]]@(0,0))
    $null = Invoke-Runner $copyRoot 'invalid-pe' @('-View','Editor') $false 'Missing MZ header' $true
} finally { [IO.File]::WriteAllBytes($exe, $originalPe) }
$locked = [IO.File]::Open($exe, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
try {
    $null = Invoke-Runner $copyRoot 'locked-executable' @('-View','Editor') $false 'OpenRead|being used by another process' $true
} finally { $locked.Dispose() }
Write-Output "Hidden launcher regression passed. Evidence: $logs"
