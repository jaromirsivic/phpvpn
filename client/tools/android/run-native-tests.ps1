<#
.SYNOPSIS
Cross-compiles the native unit tests for Android and runs them on a device or
emulator through adb (plan-android-v001 WP02, T-N01).

.DESCRIPTION
Configures client/ with the host CMake, Ninja and the NDK toolchain
(RelWithDebInfo, static libc++, no tray, BUILD_TESTING=ON), builds the test
executables (default phpvpn-core-tests and phpvpn-logger-tests), pushes them to
/data/local/tmp/phpvpn-tests and runs each with TMPDIR pointing to a private
directory there.

They run as the adb shell user (SELinux domain shell). The POSIX layer tests
belong inside the application sandbox: the instrumented test of the bridge
module (T-N03, gradlew :bridge:connectedDebugAndroidTest). The same cases as
phpvpn-android-posix-tests can still be run here with
-Tests phpvpn-android-posix-tests for diagnosis; the shell domain is refused
link and fifo_file create on shell_data_file, so the configuration publication
cases fail there by design of the policy, not of the code.

The crypto subset of the portable tests (PHPVPN_PORTABLE_CRYPTO_TEST_SOURCES)
is built only with the prebuilt OpenSSL and curl of WP01, given by -AndroidDeps
or the environment variable PHPVPN_ANDROID_DEPS.

Exit code: 0 when every executable passed, otherwise the first non-zero exit
code of a test executable, 2 for a build or adb failure.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\run-native-tests.ps1

.EXAMPLE
powershell -NoProfile -File client\tools\android\run-native-tests.ps1 -Serial emulator-5556 -Filter config_files
#>
[CmdletBinding()]
param(
    [ValidateSet('x86_64', 'arm64-v8a')]
    [string]$Abi = 'x86_64',
    [string]$Serial = 'emulator-5554',
    # Substring of "suite.name", passed to every executable (harness filter).
    [string]$Filter = '',
    [int]$ApiLevel = 34,
    [string]$BuildDir = '',
    [string]$AndroidDeps = $env:PHPVPN_ANDROID_DEPS,
    [string]$NdkVersion = '28.2.13676358',
    [string[]]$Tests = @('phpvpn-core-tests', 'phpvpn-logger-tests'),
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3

function Fail([string]$message) {
    Write-Host "run-native-tests: $message" -ForegroundColor Red
    exit 2
}

# The filter reaches the device shell as text, so only harness name characters.
if ($Filter -notmatch '^[A-Za-z0-9_.\-]*$') { Fail "the filter may contain only letters, digits, '_', '.' and '-'" }

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$client = Join-Path $repo 'client'
if ($BuildDir -eq '') { $BuildDir = Join-Path $repo "work\build\android-tests-$Abi" }

$sdk = $env:ANDROID_HOME
if (-not $sdk) { $sdk = $env:ANDROID_SDK_ROOT }
if (-not $sdk) { $sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
if (-not (Test-Path $sdk)) { Fail "Android SDK not found: $sdk" }
$ndk = $env:ANDROID_NDK_HOME
if (-not $ndk) { $ndk = Join-Path $sdk "ndk\$NdkVersion" }
$toolchain = Join-Path $ndk 'build\cmake\android.toolchain.cmake'
if (-not (Test-Path $toolchain)) { Fail "NDK toolchain file not found: $toolchain" }
$adb = Join-Path $sdk 'platform-tools\adb.exe'
if (-not (Test-Path $adb)) { Fail "adb not found: $adb" }

if (-not $SkipBuild) {
    foreach ($tool in @('cmake', 'ninja')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { Fail "$tool is not on PATH" }
    }
    $configure = @(
        '-S', $client, '-B', $BuildDir, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DANDROID_ABI=$Abi",
        "-DANDROID_PLATFORM=android-$ApiLevel",
        '-DANDROID_STL=c++_static',
        '-DPHPVPN_BUILD_TRAY=OFF',
        '-DBUILD_TESTING=ON',
        "-DPHPVPN_ANDROID_DEPS=$AndroidDeps"
    )
    Write-Host "== configure ($Abi, android-$ApiLevel) -> $BuildDir"
    & cmake @configure
    if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed ($LASTEXITCODE)" }
    Write-Host "== build $($Tests -join ', ')"
    & cmake --build $BuildDir --target @Tests
    if ($LASTEXITCODE -ne 0) { Fail "cmake build failed ($LASTEXITCODE)" }
}

$state = (& $adb -s $Serial get-state 2>$null)
if ($LASTEXITCODE -ne 0 -or "$state".Trim() -ne 'device') { Fail "device $Serial is not available (adb get-state: '$state')" }
$abis = (& $adb -s $Serial shell getprop ro.product.cpu.abilist)
if ("$abis" -notmatch [regex]::Escape($Abi)) { Fail "device $Serial does not run $Abi (abilist: $abis)" }
$release = ("$(& $adb -s $Serial shell getprop ro.build.version.release)").Trim()
$sdkLevel = ("$(& $adb -s $Serial shell getprop ro.build.version.sdk)").Trim()
Write-Host "== device $Serial, Android $release (API $sdkLevel), $abis"

$remote = '/data/local/tmp/phpvpn-tests'
& $adb -s $Serial shell "rm -rf $remote && mkdir -p $remote/tmp && chmod 700 $remote/tmp"
if ($LASTEXITCODE -ne 0) { Fail "could not prepare $remote" }

$result = 0
foreach ($test in $Tests) {
    $binary = Join-Path $BuildDir "tests\$test"
    if (-not (Test-Path $binary)) { Fail "$binary was not built" }
    & $adb -s $Serial push $binary "$remote/$test" | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "adb push of $test failed" }
    & $adb -s $Serial shell "chmod 755 $remote/$test"
    Write-Host "== run $test $Filter"
    # adb shell (protocol v2, Android 7+) returns the exit status of the command.
    & $adb -s $Serial shell "cd $remote && TMPDIR=$remote/tmp ./$test $Filter"
    $code = $LASTEXITCODE
    Write-Host "== $test exit $code"
    if ($code -ne 0 -and $result -eq 0) { $result = $code }
}

& $adb -s $Serial shell "rm -rf $remote" | Out-Null
if ($result -eq 0) { Write-Host '== all native test executables passed' -ForegroundColor Green }
else { Write-Host "== native tests failed (exit $result)" -ForegroundColor Red }
exit $result
