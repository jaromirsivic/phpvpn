[CmdletBinding()]
param(
    [ValidateSet('Preview','Test','Build')][string]$Mode = 'Preview',
    [ValidateSet('Tray','Connect','Editor','Statistics','Wizard','Contracts','Password')][string]$View = 'Contracts',
    [string]$Scene = 'Default',
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$Filter,
    [switch]$Reconfigure
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$scenes = @{
    Tray=@('Default'); Connect=@('Default','Connecting','Connected','Failed')
    Editor=@('Default','Invalid'); Statistics=@('Default','Unavailable')
    Wizard=@('Default','Gate','Url','Secret','Identity','Contracts')
    Contracts=@('Default','Invalid','Saving','SaveUnknown'); Password=@('Default')
}
if ($Mode -ne 'Preview' -and ($PSBoundParameters.ContainsKey('View') -or $PSBoundParameters.ContainsKey('Scene'))) {
    throw 'View and Scene are only valid in Preview mode.'
}
if ($Mode -eq 'Test') {
    if ([string]::IsNullOrWhiteSpace($Filter) -or $Filter.StartsWith('--')) { throw 'Test requires a nonempty substring Filter, not a CLI option.' }
} elseif ($PSBoundParameters.ContainsKey('Filter')) { throw 'Filter is only valid in Test mode.' }
if ($Mode -eq 'Preview' -and $scenes[$View] -cnotcontains $Scene) { throw "Invalid View/Scene: $View/$Scene" }
# Canonical casing is passed to the strict native CLI.
$View = @($scenes.Keys | Where-Object { $_ -ieq $View })[0]
$Configuration = if ($Configuration -ieq 'Release') { 'Release' } else { 'Debug' }
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = Join-Path $root 'client'
$build = Join-Path $root ('work/build/tray-dev-' + $Configuration.ToLowerInvariant())
$log = Join-Path $root ('work/logs/tray-dev/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $log -Force | Out-Null
$exe = Join-Path $build 'tests/phpvpn-tray-ui-tests.exe'
$summary = [ordered]@{ Mode=$Mode; Configuration=$Configuration; Target='phpvpn-tray-ui-tests'; Source=$source; Build=$build; Executable=$exe; Logs=$log }
Write-Host "Target: phpvpn-tray-ui-tests ($Configuration)`nBuild: $build`nExecutable: $exe`nLogs: $log"

function Invoke-Logged([string]$Tool, [string[]]$Arguments, [string]$Name) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    # Native stderr must be logged without PowerShell 5 treating it as terminating.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Tool @Arguments 2>&1 | Out-File -LiteralPath (Join-Path $log ($Name + '.log')) -Encoding utf8; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $savedPreference }
    $watch.Stop()
    $summary[$Name + 'Seconds'] = $watch.Elapsed.TotalSeconds
    $summary[$Name + 'ExitCode'] = $code
    if ($Name -eq 'compiler-version') { Get-Content -LiteralPath (Join-Path $log ($Name + '.log')) -TotalCount 2 | Write-Host }
    else { Get-Content -LiteralPath (Join-Path $log ($Name + '.log')) | Write-Host }
    if ($code -ne 0) {
        if ($Name -eq 'build' -and (Get-Content -Raw -LiteralPath (Join-Path $log 'build.log')) -match 'LNK1104|LNK1168|Permission denied|Access is denied') { Write-Host 'Close the existing GUI preview and run the command again.' }
        throw "$Name failed with exit code $code. No preview was launched."
    }
}
function Assert-Cache {
    $cachePath = Join-Path $build 'CMakeCache.txt'
    if (!(Test-Path -LiteralPath $cachePath)) { return }
    $cache = @{}
    Get-Content -LiteralPath $cachePath | ForEach-Object { if ($_ -match '^([^#/][^:]*):[^=]+=(.*)$') { $cache[$matches[1]]=$matches[2] } }
    if ($cache['CMAKE_GENERATOR'] -cne 'Ninja' -or $cache['CMAKE_BUILD_TYPE'] -cne $Configuration -or
        !$cache['CMAKE_HOME_DIRECTORY'] -or [IO.Path]::GetFullPath($cache['CMAKE_HOME_DIRECTORY']) -ine $source) {
        throw "Foreign source, generator or configuration in the dev cache: $build. Cache was preserved."
    }
    foreach ($flag in @('BUILD_TESTING','PHPVPN_BUILD_UI_TESTS')) {
        if ($cache[$flag] -cne 'ON') { throw "Dev cache requires $flag=ON: $build. Cache was preserved." }
    }
    foreach ($flag in @('PHPVPN_BUILD_RESOURCE_TESTS','PHPVPN_BUILD_SERVICE_E2E','PHPVPN_BUILD_CONTRACT_TESTS')) {
        if ($cache[$flag] -cne 'OFF') { throw "Dev cache requires $flag=OFF: $build. Cache was preserved." }
    }
    if ($cache['PHPVPN_ENABLE_ASAN'] -eq 'ON') { throw "ASAN is not a Debug/Release dev cache: $build. Cache was preserved." }
    $compilerFiles = @(Get-ChildItem -LiteralPath (Join-Path $build 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -Recurse -ErrorAction SilentlyContinue)
    if (!$compilerFiles.Count) { throw 'Cannot verify AMD64 architecture of the existing cache. Cache was preserved.' }
    foreach ($file in $compilerFiles) {
        if ((Get-Content -Raw -LiteralPath $file.FullName) -notmatch 'set\(CMAKE_CXX_COMPILER_ARCHITECTURE_ID "?x64"?\)') {
            throw 'Non-AMD64 or unverifiable compiler cache. Cache was preserved.'
        }
    }
}
function Assert-Pe([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw 'Missing MZ header.' }
        $stream.Position=0x3c; $offset=$reader.ReadUInt32()
        if ($offset -gt $stream.Length - 24) { throw 'Invalid PE offset.' }
        $stream.Position=$offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'UI executable is not AMD64 PE (0x8664).' }
    } finally { $reader.Dispose(); $stream.Dispose() }
}
# Windows command-line quoting, including trailing backslashes and literal quotes.
function Quote-Argument([string]$Value) {
    return '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
try {
    Assert-Cache
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe was not found. Install Visual Studio C++ build tools.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$installation) { throw 'MSVC x64 installation was not found.' }
    $devcmd = Join-Path ([string]$installation) 'Common7/Tools/VsDevCmd.bat'
    if (!(Test-Path -LiteralPath $devcmd)) { throw 'VsDevCmd.bat was not found.' }
    $devCommand = '"' + $devcmd + '" -no_logo -arch=x64 -host_arch=x64 >nul && set'
    $environmentLines = & $env:ComSpec /d /c $devCommand
    if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd failed.' }
    foreach ($line in $environmentLines) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
    if ($env:VSCMD_ARG_TGT_ARCH -cne 'x64' -or $env:VSCMD_ARG_HOST_ARCH -cne 'x64') { throw 'VsDevCmd did not select AMD64 host and target.' }
    $cmake=(Get-Command cmake.exe -ErrorAction Stop).Source
    $ninja=(Get-Command ninja.exe -ErrorAction Stop).Source
    $compiler=(Get-Command cl.exe -ErrorAction Stop).Source
    if ($compiler -notmatch '[\\/]Hostx64[\\/]x64[\\/]cl\.exe$') { throw 'cl.exe is not the MSVC AMD64 compiler.' }
    $versionOutput = & $cmake --version
    if ($LASTEXITCODE -ne 0 -or ($versionOutput -join "`n") -notmatch 'cmake version (\d+\.\d+\.\d+)' -or [version]$matches[1] -lt [version]'3.28') { throw 'CMake >= 3.28 is required.' }
    Invoke-Logged $ninja @('--version') 'ninja-version'
    Invoke-Logged $compiler @('/?') 'compiler-version'
    $summary['Compiler']=$compiler; $summary['CMake']=$cmake; $summary['Ninja']=$ninja
    if ($Reconfigure -or !(Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt')) -or !(Test-Path -LiteralPath (Join-Path $build 'build.ninja'))) {
        Invoke-Logged $cmake @('-S',$source,'-B',$build,'-G','Ninja','-DCMAKE_CXX_COMPILER=cl.exe',"-DCMAKE_BUILD_TYPE=$Configuration",'-DBUILD_TESTING=ON','-DPHPVPN_BUILD_UI_TESTS=ON','-DPHPVPN_BUILD_RESOURCE_TESTS=OFF','-DPHPVPN_BUILD_SERVICE_E2E=OFF','-DPHPVPN_BUILD_CONTRACT_TESTS=OFF') 'configure'
    }
    Assert-Cache
    Invoke-Logged $cmake @('--build',$build,'--target','phpvpn-tray-ui-tests','--parallel','4') 'build'
    if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'The successful build did not produce the UI executable.' }
    Assert-Pe $exe
    if ($Mode -eq 'Build') { $summary['ExitCode']=0; Write-Host ("Build: {0:N3} s." -f $summary['buildSeconds']) }
    else {
        if ($Mode -eq 'Preview') { Write-Host 'GUI preview uses isolated synthetic data. Close the window (or choose Tray Exit) to finish.' }
        $arguments = if ($Mode -eq 'Preview') { @('--dev-preview',$View,'--scene',$Scene) } else { @($Filter) }
        $runName = $Mode.ToLowerInvariant()
        $watch=[Diagnostics.Stopwatch]::StartNew()
        # Suppress the console without STARTF_USESHOWWINDOW/SW_HIDE, which
        # overrides the first ShowWindow call in the real native GUI.
        $process = New-Object Diagnostics.Process
        $process.StartInfo.FileName = $exe
        $process.StartInfo.Arguments = (($arguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
        $process.StartInfo.WorkingDirectory = $root
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.CreateNoWindow = $true
        $process.StartInfo.RedirectStandardOutput = $true
        $process.StartInfo.RedirectStandardError = $true
        $stdout = $null; $stderr = $null
        try {
            $stdout = [IO.File]::Create((Join-Path $log "$runName.stdout.log"))
            $stderr = [IO.File]::Create((Join-Path $log "$runName.stderr.log"))
            if (!$process.Start()) { throw 'Could not start the UI executable.' }
            $summary['ChildProcessId'] = $process.Id
            # Drain both pipes concurrently and directly to disk. A full pipe
            # must not deadlock WaitForExit or buffer a long preview in memory.
            $stdoutCopy = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
            $stderrCopy = $process.StandardError.BaseStream.CopyToAsync($stderr)
            $process.WaitForExit()
            $null = $stdoutCopy.GetAwaiter().GetResult()
            $null = $stderrCopy.GetAwaiter().GetResult()
            $code = $process.ExitCode
        } finally {
            if ($stdout) { $stdout.Dispose() }
            if ($stderr) { $stderr.Dispose() }
            $process.Dispose()
        }
        if ($null -eq $code) { throw 'Could not read the child process exit code.' }
        $summary[$runName + 'Seconds']=$watch.Elapsed.TotalSeconds
        $summary['ExitCode']=$code
        Write-Host ("Build: {0:N3} s. {1}: {2:N3} s." -f $summary['buildSeconds'],$Mode,$watch.Elapsed.TotalSeconds)
        Get-Content -LiteralPath (Join-Path $log "$runName.stdout.log") | Write-Host
        Get-Content -LiteralPath (Join-Path $log "$runName.stderr.log") | Write-Host
        if ($code -ne 0) { throw "$Mode failed with exit code $code." }
    }
} catch {
    $summary['Error']=$_.Exception.Message
    if (!$summary.Contains('ExitCode') -or $summary['ExitCode'] -eq 0) { $summary['ExitCode']=1 }
    Write-Error $_ -ErrorAction Continue
} finally {
    $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $log 'summary.json') -Encoding UTF8
}
exit $summary['ExitCode']
