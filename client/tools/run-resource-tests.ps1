<# Runs explicit real IPC RSS and process-local DNS tests without elevation. #>
[CmdletBinding()]
param([ValidateSet('debug', 'release')][string]$Preset = 'release')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = Join-Path $root 'work/build'
if ([Environment]::OSVersion.Version.Build -lt 22000) { throw 'Process-local DNS test requires Windows 11 build 22000 or later.' }
function Read-HostBaseline {
    [ordered]@{
        dns = @(Get-DnsClientServerAddress | Sort-Object InterfaceIndex,AddressFamily |
            Select-Object InterfaceIndex,AddressFamily,ServerAddresses)
        defaultRoutes = @(Get-NetRoute | Where-Object { $_.DestinationPrefix -in @('0.0.0.0/0','::/0') } |
            Sort-Object InterfaceIndex,DestinationPrefix,NextHop | Select-Object InterfaceIndex,DestinationPrefix,NextHop,RouteMetric)
        productionService = @(Get-CimInstance Win32_Service -Filter "Name='phpvpn'" | Select-Object Name,State,ProcessId,PathName)
    } | ConvertTo-Json -Depth 8
}
$before = Read-HostBaseline
[IO.File]::WriteAllText((Join-Path $output 'resource-host-before.json'), $before)
& (Join-Path $PSScriptRoot 'build-x64.ps1') -Preset $Preset -ResourceTests
if ($LASTEXITCODE) { throw 'Resource test build failed.' }
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = Join-Path $output "$Preset/tests/phpvpn-resource-tests.exe"
$start.Arguments = 'resource_'
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = New-Object Diagnostics.Process
$process.StartInfo = $start
try {
    if (-not $process.Start()) { throw 'Resource test process failed to start.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(90000)) {
        $process.Kill() # Only this newly created test process.
        $process.WaitForExit()
        throw 'Resource test exceeded its 90 second outer deadline.'
    }
    $exitCode = $process.ExitCode
    $log = $stdout.Result + $stderr.Result
    [IO.File]::WriteAllText((Join-Path $output 'resource-plan3.log'), $log)
    [IO.File]::WriteAllText((Join-Path $output 'resource-plan3.exit'), [string]$exitCode)
    Write-Output $log
    if ($exitCode) { throw "Resource test failed: $exitCode" }
} finally {
    $process.Dispose()
    $after = Read-HostBaseline
    [IO.File]::WriteAllText((Join-Path $output 'resource-host-after.json'), $after)
    if ($before -cne $after) { throw 'Host DNS/default routes/production service baseline changed during the test.' }
    if (Get-NetUDPEndpoint -LocalAddress '127.0.0.2' -LocalPort 53 -ErrorAction SilentlyContinue) {
        throw 'The private DNS listener is still present.'
    }
    [IO.File]::WriteAllText((Join-Path $output 'resource-cleanup.log'), 'PASS: host DNS, default routes and production service unchanged, private DNS listener absent.')
}
