<#
.SYNOPSIS
Builds and runs an isolated real SCM/Wintun test on Windows AMD64.
.DESCRIPTION
The administrator worker creates only phpvpn-plan3-e2e-d6c8 and the adapter
PHPVPN Plan3 E2E d6c8. It refuses existing test objects and prefix conflicts.
It uses a separate data directory under work/build/e2e-debug/tests, a private
pipe and the loopback PHP fixture on port 18093. The production service and
ProgramData/PHPVPN/config.json are read only. No trust store is changed.
The test temporarily adds only the isolated 10.99.99.0/24 address/route.
It removes the test service and verifies network cleanup after the run.
#>
[CmdletBinding()]
param([switch]$ElevatedWorker, [switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$e2eBuild = Join-Path $repositoryRoot 'work/build/e2e-debug'
$e2eDirectory = Join-Path $e2eBuild 'tests'
$e2eExecutable = Join-Path $e2eDirectory 'phpvpn-service-e2e.exe'
$e2eServiceExecutable = Join-Path $e2eDirectory 'phpvpn-e2e-service.exe'
$e2eData = [IO.Path]::GetFullPath((Join-Path $e2eDirectory 'isolated-data'))
$evidenceDirectory = Join-Path $repositoryRoot 'work/build'
$e2eServiceName = 'phpvpn-plan3-e2e-d6c8'
$e2eAdapterName = 'PHPVPN Plan3 E2E d6c8'
$e2ePnpInstance = 'SWD\WINTUN\{4B67D77C-70A2-49F6-97DB-A5F4D6C8E2E1}'
$exitPath = Join-Path $evidenceDirectory 'service-e2e.exit'

if (-not $ElevatedWorker) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'MSVC AMD64 tools were not found.' }
    $devCommand = '"' + (Join-Path $installation 'Common7/Tools/VsDevCmd.bat') + '" -arch=x64 -host_arch=x64 >nul && set'
    cmd /c $devCommand | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
    & cmake -S (Join-Path $repositoryRoot 'client') -B $e2eBuild -G Ninja '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_CXX_COMPILER=cl.exe' '-DBUILD_TESTING=ON' '-DPHPVPN_BUILD_SERVICE_E2E=ON'
    if ($LASTEXITCODE) { throw 'Service E2E configuration failed.' }
    & cmake --build $e2eBuild --target phpvpn-service-e2e
    if ($LASTEXITCODE) { throw 'Service E2E build failed.' }
    if ($BuildOnly) { return }
    if (Test-Path -LiteralPath $exitPath) { Remove-Item -LiteralPath $exitPath }
    $workerArguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -ElevatedWorker'
    $worker = Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $workerArguments -Verb RunAs -WindowStyle Hidden -PassThru
    Write-Host "Isolated administrator worker PID $($worker.Id). Result: $exitPath"
    return
}

function Get-ProductionBaseline {
    $production = Get-CimInstance Win32_Service -Filter "Name='phpvpn'"
    $productionConfig = Join-Path $env:ProgramData 'PHPVPN/config.json'
    [ordered]@{
        servicePath = if ($production) { $production.PathName } else { $null }
        serviceState = if ($production) { $production.State } else { $null }
        servicePid = if ($production) { $production.ProcessId } else { $null }
        configExists = Test-Path -LiteralPath $productionConfig -PathType Leaf
        configHash = if (Test-Path -LiteralPath $productionConfig -PathType Leaf) { (Get-FileHash -LiteralPath $productionConfig -Algorithm SHA256).Hash } else { $null }
        defaultRoutes = @(Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' | Sort-Object InterfaceIndex,NextHop | Select-Object InterfaceIndex,NextHop,RouteMetric)
    } | ConvertTo-Json -Depth 5 -Compress
}
function Convert-Ipv4([string]$Address) {
    $octets = [Net.IPAddress]::Parse($Address).GetAddressBytes()
    if ($octets.Length -ne 4) { throw 'Expected IPv4.' }
    return [uint64]$octets[0] * 16777216 + [uint64]$octets[1] * 65536 + [uint64]$octets[2] * 256 + [uint64]$octets[3]
}
function Assert-NoPrefixConflict {
    $testStart = Convert-Ipv4 '10.99.99.0'
    $testEnd = $testStart + 255
    foreach ($route in Get-NetRoute -AddressFamily IPv4) {
        $parts = $route.DestinationPrefix.Split('/')
        $prefixLength = [int]$parts[1]
        if ($prefixLength -eq 0) { continue }
        $routeStart = Convert-Ipv4 $parts[0]
        $routeEnd = $routeStart + [uint64][Math]::Pow(2, 32 - $prefixLength) - 1
        if ($routeStart -le $testEnd -and $routeEnd -ge $testStart) { throw "IPv4 route conflict: $($route.DestinationPrefix) on $($route.InterfaceAlias)" }
    }
    foreach ($address in Get-NetIPAddress -AddressFamily IPv4) {
        $value = Convert-Ipv4 $address.IPAddress
        if ($value -ge $testStart -and $value -le $testEnd) { throw 'An existing interface owns an address in the test prefix.' }
    }
}

$outcome = 1
$ownedRun = $false
$baseline = $null
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administrator token required.' }
    if (Get-Service -Name $e2eServiceName -ErrorAction SilentlyContinue) { throw 'Test service already exists, refusing to adopt it.' }
    if (Get-NetAdapter -Name $e2eAdapterName -IncludeHidden -ErrorAction SilentlyContinue) { throw 'Test adapter already exists, refusing to adopt it.' }
    if (Get-PnpDevice -InstanceId $e2ePnpInstance -ErrorAction SilentlyContinue) { throw 'Test PnP record already exists, including a non-present phantom. Explicit isolated cleanup is required.' }
    if (Test-Path -LiteralPath $e2eData) { throw 'Test data already exists, preserve it for explicit inspection.' }
    Assert-NoPrefixConflict
    $baseline = Get-ProductionBaseline
    [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e-baseline.json'), $baseline)
    $ownedRun = $true
    $controller = New-Object Diagnostics.Process
    $controller.StartInfo.FileName = $e2eExecutable
    $controller.StartInfo.Arguments = '--run-isolated-with-udp'
    $controller.StartInfo.UseShellExecute = $false
    $controller.StartInfo.CreateNoWindow = $true
    $controller.StartInfo.RedirectStandardOutput = $true
    $controller.StartInfo.RedirectStandardError = $true
    if (-not $controller.Start()) { throw 'Could not start the isolated controller.' }
    # Keep ownership of the native handle through the exit-code read. Drain
    # both streams asynchronously so a full redirected pipe cannot block it.
    $controllerHandle = $controller.Handle
    $standardOutput = $controller.StandardOutput.ReadToEndAsync()
    $standardError = $controller.StandardError.ReadToEndAsync()
    if (-not $controller.WaitForExit(240000)) {
        $controller.Kill()
        throw 'The isolated controller exceeded its 240 second outer deadline.'
    }
    $controller.WaitForExit()
    $controllerExitCode = $controller.ExitCode
    [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e.log'), $standardOutput.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e.stderr.log'), $standardError.GetAwaiter().GetResult())
    $controller.Dispose()
    if ($controllerExitCode -ne 0) { throw "The isolated controller failed, exit $controllerExitCode." }
    $outcome = 0
} catch {
    [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e.error'), $_.Exception.Message)
} finally {
    if ($ownedRun) {
        try {
            # Cleanup is restricted to the freshly created fixed test service
            # and the exact executable that was prepared in this workspace.
            $testService = Get-CimInstance Win32_Service -Filter "Name='$e2eServiceName'"
            if ($testService) {
                if ($testService.PathName.Trim('"') -ne $e2eServiceExecutable) { throw 'Test service executable changed, refusing cleanup.' }
                $serviceControl = Get-Service -Name $e2eServiceName
                if ($serviceControl.Status -ne 'Stopped') {
                    $serviceControl.Stop()
                    $serviceControl.WaitForStatus('Stopped', [TimeSpan]::FromSeconds(20))
                }
                & sc.exe delete $e2eServiceName | Out-Null
                if ($LASTEXITCODE) { throw 'Could not remove isolated service registration.' }
            }
            if (Get-NetAdapter -Name $e2eAdapterName -IncludeHidden -ErrorAction SilentlyContinue) { throw 'Test adapter remains after service cleanup.' }
            $testPnp = Get-PnpDevice -InstanceId $e2ePnpInstance -ErrorAction SilentlyContinue
            if ($testPnp) {
                if ($testPnp.Present -ne $false) { throw 'A live test PnP device remains, refusing phantom cleanup.' }
                & $e2eExecutable --cleanup-owned-phantom *> (Join-Path $evidenceDirectory 'service-e2e-phantom-cleanup.log')
                if ($LASTEXITCODE) { throw 'Exact test phantom cleanup failed.' }
                if (Get-PnpDevice -InstanceId $e2ePnpInstance -ErrorAction SilentlyContinue) { throw 'Test PnP record remains after exact phantom cleanup.' }
            }
            Assert-NoPrefixConflict
            $after = Get-ProductionBaseline
            [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e-after.json'), $after)
            if ($baseline -ne $after) { throw 'Production service, configuration or default routes changed during the test.' }
            [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e-cleanup.log'), 'PASS: test service/adapter/PnP record/prefix absent. Production service PID/state/path/config hash and default routes unchanged.')
        } catch {
            $outcome = 1
            [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e-cleanup.error'), $_.Exception.Message)
        }
        try {
            if (Test-Path -LiteralPath $e2eData) {
                $resolvedData = (Resolve-Path -LiteralPath $e2eData).Path
                $expectedParent = [IO.Path]::GetFullPath($e2eDirectory).TrimEnd('\') + '\'
                if (-not $resolvedData.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path -Leaf $resolvedData) -ne 'isolated-data') { throw 'Unexpected test data path, refusing cleanup.' }
                $logPath = Join-Path $resolvedData 'logs'
                if (Test-Path -LiteralPath $logPath) {
                    Get-ChildItem -LiteralPath $logPath -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $evidenceDirectory ('service-e2e-' + $_.Name)) }
                }
                if ($outcome -eq 0) { Remove-Item -LiteralPath $resolvedData -Recurse -Force }
            }
        } catch {
            $outcome = 1
            [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'service-e2e-data.error'), $_.Exception.Message)
        }
    }
    [IO.File]::WriteAllText($exitPath, [string]$outcome)
}
