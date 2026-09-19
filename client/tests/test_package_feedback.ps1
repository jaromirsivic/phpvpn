# Isolated script control-flow tests. No real service, registry, UAC, client or
# sleep calls run. Production scripts have no test switch or delay bypass.
[CmdletBinding()]
param(
    [string]$Scenario,
    [ValidateSet('install', 'uninstall')][string]$Operation = 'install',
    [string]$FixtureRoot
)
$ErrorActionPreference = 'Stop'
$packageSource = Join-Path $PSScriptRoot '../tools/package'

if ($Scenario) {
    $env:ProgramFiles = Join-Path $FixtureRoot 'program-files'
    $env:ProgramData = Join-Path $FixtureRoot 'program-data'
    $root = Join-Path $env:ProgramFiles 'phpvpn'
    $generation = Join-Path $root '1.0.0'
    New-Item -ItemType Directory -Path $generation -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $root 'current.generation'), '1.0.0')
    foreach ($name in @('phpvpn.exe', 'phpvpn-service.exe')) {
        [IO.File]::WriteAllText((Join-Path $generation $name), 'test fixture, never executed')
    }
    $data = Join-Path $env:ProgramData 'phpvpn'
    New-Item -ItemType Directory -Path $data -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $data 'config.json'), 'preserved configuration')

    function Write-TestEvent([string]$Event) { Write-Host "TEST:$Event" }
    function Start-Sleep {
        param($Seconds, $Milliseconds)
        if ($Seconds) { Write-TestEvent "sleep:$Seconds" }
    }
    function Start-Process {
        param($FilePath, $ArgumentList, $Verb, [switch]$Wait, [switch]$PassThru, $ErrorAction)
        if ($Verb -eq 'RunAs') {
            if ($Wait -or -not $PassThru) { throw 'Elevation must wait for the installer only, not the tray process tree' }
            Write-TestEvent "elevation:$($ArgumentList -join ' ')"
            if ($Scenario -eq 'uac-cancelled') { throw 'Synthetic UAC cancellation' }
            $process = [pscustomobject]@{ ExitCode = $(if ($Scenario -eq 'uac-failure') { 37 } else { 0 }) }
            $process | Add-Member ScriptMethod WaitForExit { Write-TestEvent 'elevation-wait' }
            return $process
        }
        Write-TestEvent 'tray-start'
        if ($Scenario -eq 'tray-failure') { throw 'Synthetic tray launch failure' }
    }
    function Get-Process { param($Name, $ErrorAction) return @() }
    function Stop-Process {
        param([switch]$Force, [Parameter(ValueFromPipeline=$true)]$InputObject)
        process { if ($null -ne $InputObject) { throw 'No live process may be stopped by this test' } }
    }
    function Get-Service {
        param($Name, $ErrorAction)
        if ($Operation -eq 'uninstall' -and $Scenario -ne 'already-absent') {
            return [pscustomobject]@{ Status = 'Running' }
        }
        return $null
    }
    function Start-Service {
        param($Name)
        Write-TestEvent 'service-start'
        if ($Scenario -eq 'service-start-failure') { throw 'Synthetic service start failure' }
    }
    function Stop-Service {
        param($Name, [switch]$Force, $ErrorAction)
        Write-TestEvent 'service-stop'
        if ($Scenario -eq 'service-stop-failure') { throw 'Synthetic service stop failure' }
    }
    function New-ItemProperty {
        param($Path, $Name, $Value, $PropertyType, [switch]$Force)
        Write-TestEvent 'autostart-create'
        if ($Scenario -eq 'autostart-failure') { throw 'Synthetic autostart failure' }
    }
    function Get-ItemProperty { param($Path, $Name, $ErrorAction) return [pscustomobject]@{} }
    function Remove-ItemProperty {
        param($Path, $Name)
        Write-TestEvent 'autostart-remove'
        if ($Scenario -eq 'autostart-failure') { throw 'Synthetic autostart failure' }
    }
    function Copy-Item {
        param($LiteralPath, $Destination, [switch]$Force)
        if ($Scenario -eq 'copy-failure') { throw 'Synthetic copy failure' }
    }
    function Test-Path {
        param($LiteralPath, $PathType)
        if ($LiteralPath -eq '\\.\pipe\phpvpn-control-v1') { return $Scenario -ne 'pipe-failure' }
        if ($Scenario -in @('fallback', 'fallback-autostart-failure', 'sc-failure', 'service-stop-failure') -and $LiteralPath -like '*phpvpn-service.exe') { return $false }
        return Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
    }
    function Get-Item {
        param($LiteralPath, [switch]$Force)
        if ($Scenario -eq 'reparse') { return [pscustomobject]@{ Attributes = [IO.FileAttributes]::ReparsePoint } }
        return Microsoft.PowerShell.Management\Get-Item @PSBoundParameters
    }
    function Remove-Item {
        param($LiteralPath, [switch]$Recurse, [switch]$Force)
        if (-not ([IO.Path]::GetFullPath($LiteralPath)).StartsWith($FixtureRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Test attempted removal outside its fixture'
        }
        if ($Scenario -eq 'remove-failure') { throw 'Synthetic file removal failure' }
        Microsoft.PowerShell.Management\Remove-Item @PSBoundParameters
        Write-TestEvent 'files-remove'
    }
    function Invoke-TestNative {
        param([string]$Action)
        Write-TestEvent "native:$Action"
        $global:LASTEXITCODE = 0
        if ($Action -in @('--install', '--uninstall')) {
            Write-TestEvent 'autostart-owned-by-service'
            if ($Scenario -eq 'autostart-failure') { Write-Output 'WARNING: phpvpn-tray autostart failed (Win32 5).' }
        }
        if (($Scenario -eq 'disconnect-failure' -and $Action -eq 'disconnect') -or
            ($Scenario -eq 'native-failure' -and $Action -in @('--install', '--uninstall')) -or
            ($Scenario -eq 'sc-failure' -and $Action -eq 'delete')) { $global:LASTEXITCODE = 19 }
    }

    $source = Join-Path $packageSource "$Operation.ps1"
    $tokens = $null
    $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($source, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw "Parse errors in $source" }
    $text = [IO.File]::ReadAllText($source)
    $edits = @()
    foreach ($function in $ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
        if ($function.Name -eq 'Assert-Administrator') {
            $edits += @{ Start = $function.Extent.StartOffset; End = $function.Extent.EndOffset; Text = 'function Assert-Administrator { return $Scenario -notlike "uac-*" }' }
        }
        if ($function.Name -eq 'Remove-TrayAutostartFallback') {
            $edits += @{ Start = $function.Extent.StartOffset; End = $function.Extent.EndOffset; Text = 'function Remove-TrayAutostartFallback { Write-TestEvent "autostart-fallback"; if ($Scenario -eq "fallback-autostart-failure") { Write-Warning "phpvpn-tray autostart cleanup failed (HRESULT -2147024891)." } }' }
        }
        if ($function.Name -eq 'New-ProtectedDirectory') {
            $edits += @{ Start = $function.Extent.StartOffset; End = $function.Extent.EndOffset; Text = 'function New-ProtectedDirectory { param($Path) New-Item -ItemType Directory -Path $Path -Force | Out-Null }' }
        }
    }
    if ($Operation -eq 'install') {
        # Replace only package cryptography/driver preflight with fixture inputs.
        # Real signed-package preflight is exercised by verify-package.ps1.
        $start = $ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$manifestPath' }
        $verify = $ast.Find({ param($n) $n -is [Management.Automation.Language.IfStatementAst] -and $n.Clauses[0].Item1.Extent.Text -eq '$VerifyOnly' }, $true)
        $edits += @{ Start = $start.Extent.StartOffset; End = $verify.Extent.StartOffset; Text = @'
$manifest = [pscustomobject]@{ releaseVersion = '1.0.0'; files = @() }
$manifestPath = $signaturePath = $certificatePath = 'fixture'
if ($Scenario -eq 'preflight-failure') { Fail 'Synthetic preflight failure' }

'@ }
    }
    foreach ($command in $ast.FindAll({ param($n) $n -is [Management.Automation.Language.CommandAst] }, $true)) {
        if ($command.InvocationOperator -eq 'Ampersand' -or $command.GetCommandName() -eq 'sc.exe') {
            if ($Operation -eq 'install' -and $command.Extent.StartOffset -lt $verify.Extent.EndOffset) { continue }
            $action = $command.CommandElements[1].Extent.Text
            $edits += @{ Start = $command.Extent.StartOffset; End = $command.Extent.EndOffset; Text = "Invoke-TestNative '$action'" }
        }
    }
    foreach ($edit in $edits | Sort-Object { $_.Start } -Descending) {
        $text = $text.Substring(0, $edit.Start) + $edit.Text + $text.Substring($edit.End)
    }
    $arguments = @{}
    if ($Scenario -eq 'verify') { $arguments.VerifyOnly = $true }
    if ($Scenario -in @('remove-data', 'uac-remove-data')) { $arguments.RemoveData = $true }
    if ($Operation -eq 'install' -and $Scenario -like 'uac-*') { $arguments.ExpectedThumbprint = '0123456789ABCDEF0123456789ABCDEF01234567' }
    $instrumented = Join-Path $FixtureRoot "$Operation.ps1"
    [IO.File]::WriteAllText($instrumented, $text)
    & $instrumented @arguments
    exit $LASTEXITCODE
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ('phpvpn-feedback-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
$passed = 0
try {
    foreach ($operationName in @('install', 'uninstall')) {
        $success = if ($operationName -eq 'install') { 'PHPVPN installed successfully' } else { 'PHPVPN uninstalled successfully' }
        $cases = @('success', 'native-failure', 'autostart-failure', 'uac-success', 'uac-failure', 'uac-cancelled')
        if ($operationName -eq 'install') { $cases += @('verify', 'preflight-failure', 'copy-failure', 'service-start-failure', 'pipe-failure', 'tray-failure') }
        else { $cases += @('remove-data', 'uac-remove-data', 'already-absent', 'fallback', 'fallback-autostart-failure', 'disconnect-failure', 'service-stop-failure', 'sc-failure', 'reparse', 'remove-failure') }
        foreach ($case in $cases) {
            $fixture = Join-Path $temp "$operationName-$case"
            $ErrorActionPreference = 'Continue' # Capture expected native stderr as diagnostics.
            $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Scenario $case -Operation $operationName -FixtureRoot $fixture 2>&1
            $code = $LASTEXITCODE
            $ErrorActionPreference = 'Stop'
            $text = $output -join "`n"
            $showBox = $case -in @('success', 'remove-data', 'already-absent', 'fallback', 'fallback-autostart-failure', 'autostart-failure')
            $expectedCode = if ($case -eq 'uac-failure') { 37 } elseif ($showBox -or $case -in @('verify', 'uac-success', 'uac-remove-data')) { 0 } else { 1 }
            if ($code -ne $expectedCode) { throw "$operationName/$case returned $code instead of ${expectedCode}: $text" }
            if ($case -in @('autostart-failure', 'fallback-autostart-failure') -and $text -notmatch 'WARNING: phpvpn-tray') { throw 'Autostart warning was lost' }
            if ($operationName -eq 'uninstall' -and $case -in @('fallback', 'fallback-autostart-failure', 'sc-failure', 'service-stop-failure') -and
                $text -notmatch 'TEST:autostart-fallback') { throw 'Fallback cleanup skipped after SCM outcome' }
            $boxCount = [regex]::Matches($text, [regex]::Escape($success)).Count
            $sleepCount = [regex]::Matches($text, 'TEST:sleep:3\b').Count
            if ($boxCount -ne [int]$showBox -or $sleepCount -ne [int]$showBox) { throw "Wrong success/delay path for $operationName/${case}: $text" }
            if ($showBox) {
                $lines = @($output | ForEach-Object { [string]$_ } | Where-Object { $_ -match '^[+|]' })
                if ($lines.Count -ne 4 -or @($lines | Where-Object { $_.Length -ne 62 -or $_ -match '[^\x20-\x7E]' }).Count) { throw "Malformed ASCII box: $text" }
                if ($text.LastIndexOf('TEST:sleep:3') -lt $text.LastIndexOf('+---')) { throw 'Delay precedes the box' }
            }
            if ($case -eq 'verify' -and $text -notmatch 'nothing was installed') { throw 'VerifyOnly lost its diagnostic' }
            if ($case -eq 'uac-remove-data' -and $text -notmatch '-RemoveData') { throw 'UAC lost RemoveData' }
            if ($case -like 'uac-*' -and $operationName -eq 'install' -and $text -notmatch '0123456789ABCDEF0123456789ABCDEF01234567') { throw 'UAC lost the signing pin' }
            if ($case -like 'uac-*' -and $case -ne 'uac-cancelled' -and $text -notmatch 'TEST:elevation-wait') { throw 'Parent did not wait for the elevated result' }
            if ($expectedCode -eq 1 -and $text -notmatch '(Synthetic|could not|incomplete|did not publish)') { throw "Missing error diagnostic: $text" }
            $config = Join-Path $fixture 'program-data/phpvpn/config.json'
            if ($case -eq 'remove-data') {
                if (Test-Path -LiteralPath $config) { throw 'RemoveData kept the fixture data' }
            } elseif ([IO.File]::ReadAllText($config) -ne 'preserved configuration') { throw 'Configuration changed' }
            $passed++
        }
    }
    # Exercise the unmodified supported BAT wrappers in a directory with spaces.
    foreach ($operationName in @('install', 'uninstall')) {
        $batRoot = Join-Path $temp "$operationName entry point"
        New-Item -ItemType Directory -Path $batRoot | Out-Null
        $bat = Join-Path $batRoot "$operationName.bat"
        Copy-Item -LiteralPath (Join-Path $packageSource "$operationName.bat") -Destination $bat
        $helper = Join-Path $batRoot "$operationName.ps1"
        [IO.File]::WriteAllText($helper, @'
param([int]$Result, [switch]$VerifyOnly, [switch]$RemoveData)
Write-Output "forwarded:$VerifyOnly/$RemoveData"
exit $Result
'@)
        foreach ($result in @(0, 37)) {
            $output = & $bat -Result $result -VerifyOnly -RemoveData
            if ($LASTEXITCODE -ne $result -or ($output -join '') -notmatch 'forwarded:True/True') { throw 'BAT lost arguments or exit status' }
            $passed++
        }
        Remove-Item -LiteralPath $helper
        $output = & $bat
        if ($LASTEXITCODE -ne 1 -or ($output -join '') -notmatch 'is missing') { throw 'BAT missing-helper failure changed' }
        $passed++
    }
    Write-Output "PASS: $passed package feedback/BAT cases, ASCII layout, success-only three-second delay, VerifyOnly, UAC exit codes, failures and configuration preservation."
} finally {
    $resolved = [IO.Path]::GetFullPath($temp)
    if ($resolved.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -and (Split-Path $resolved -Leaf) -like 'phpvpn-feedback-*') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
