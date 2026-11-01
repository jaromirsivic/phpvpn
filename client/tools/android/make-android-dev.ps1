<#
.SYNOPSIS
    Builds the development APKs of the Android client into
    work/android/<version> and checks them (plan-android-v001 WP12).

.DESCRIPTION
    Decision R2 of plan-android-v001: the Android client is not distributed
    yet. This script is the development-phase part of WP12. It runs
    `gradlew assembleDebug assembleRelease` in client/android with
    -Pphpvpn.abiSplits=true (app/build.gradle.kts), copies

        phpvpn-<version>-debug.apk              universal, debug signed
        phpvpn-<version>-<abi>-debug.apk        arm64-v8a and x86_64, debug signed
        phpvpn-<version>-release-unsigned.apk   universal, unsigned (no release
                                                signing config exists yet, R2)
        symbols/<abi>/libphpvpn-android.so      non-stripped release library

    to work/android/<version>, writes SHA256SUMS and manifest.json and runs
    the WP12 checks: apksigner, zipalign -P 16, aapt2 badging and manifest
    tree, the packaged native libraries (exports, LOAD alignment, stripping,
    no test library) and Android lint (:app:lintDebug). Any failed check
    fails the script; every result is printed and kept in manifest.json.

    Release signing, R8, AAB, dist/ outputs and the Google Play material stay
    deferred to the R2 review (plan step 12).

    Runs on Windows PowerShell 5.1 and on PowerShell 7 (the client-android
    workflow runs it with pwsh on ubuntu-24.04).

    Exit code: 0 when every check passed, 1 when a check failed, 2 when the
    build or the environment failed.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\make-android-dev.ps1

.EXAMPLE
    pwsh -NoProfile -Command "client/tools/android/make-android-dev.ps1 -GradleArguments '--no-daemon'"
#>
[CmdletBinding()]
param(
    # JDK for Gradle and apksigner. Default: D:/androidstudio/jbr when it
    # exists, otherwise the JAVA_HOME of the environment.
    [string]$JavaHome = '',
    # Default: ANDROID_HOME, ANDROID_SDK_ROOT, sdk.dir of local.properties,
    # %LOCALAPPDATA%\Android\Sdk.
    [string]$AndroidSdk = '',
    # Build tools of apksigner, zipalign and aapt2 (35+ for zipalign -P).
    # Default: the newest installed.
    [string]$BuildToolsVersion = '',
    # Extra Gradle arguments, e.g. --no-daemon on CI.
    [string[]]$GradleArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
    Write-Host "make-android-dev: $message" -ForegroundColor Red
    exit 2
}

$onWindows = [IO.Path]::DirectorySeparatorChar -eq '\'
$exe = ''
$bat = ''
if ($onWindows) { $exe = '.exe'; $bat = '.bat' }

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$clientRoot = (Resolve-Path (Join-Path $scriptRoot '../..')).Path
$repositoryRoot = (Resolve-Path (Join-Path $clientRoot '..')).Path
$androidRoot = Join-Path $clientRoot 'android'
$utf8 = New-Object Text.UTF8Encoding $false

# Runs a native tool and returns its exit code and output lines (stdout and
# stderr). Windows PowerShell 5.1 turns stderr lines into errors, so the
# preference is relaxed around the call.
function Invoke-Tool([string]$File, [string[]]$Arguments) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = @(& $File @Arguments 2>&1 | ForEach-Object { "$_" })
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    return [pscustomobject]@{ ExitCode = $code; Lines = $lines; Text = ($lines -join "`n") }
}

function Read-Properties([string]$path) {
    $result = @{}
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $result }
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        if ($line -match '^\s*([#!]|$)') { continue }
        if ($line -match '^\s*([^=:\s]+)\s*[=:]\s*(.*)$') {
            # The escapes local.properties uses: \: \= \\ (Android Studio writes them).
            $result[$Matches[1]] = $Matches[2] -replace '\\(.)', '$1'
        }
    }
    return $result
}

# ------------------------------------------------------------- environment

$localProperties = Read-Properties (Join-Path $androidRoot 'local.properties')

if ($JavaHome -eq '') {
    if (Test-Path -LiteralPath 'D:/androidstudio/jbr/bin') { $JavaHome = 'D:/androidstudio/jbr' }
    elseif ($env:JAVA_HOME) { $JavaHome = $env:JAVA_HOME }
    else { Fail 'no JDK: pass -JavaHome or set JAVA_HOME' }
}
$JavaHome = [IO.Path]::GetFullPath($JavaHome)
$java = Join-Path $JavaHome "bin/java$exe"
if (-not (Test-Path -LiteralPath $java)) { Fail "java not found: $java" }
$env:JAVA_HOME = $JavaHome
$env:PATH = (Join-Path $JavaHome 'bin') + [IO.Path]::PathSeparator + $env:PATH

if ($AndroidSdk -eq '') {
    if ($env:ANDROID_HOME) { $AndroidSdk = $env:ANDROID_HOME }
    elseif ($env:ANDROID_SDK_ROOT) { $AndroidSdk = $env:ANDROID_SDK_ROOT }
    elseif ($localProperties.ContainsKey('sdk.dir')) { $AndroidSdk = $localProperties['sdk.dir'] }
    elseif ($env:LOCALAPPDATA) { $AndroidSdk = Join-Path $env:LOCALAPPDATA 'Android/Sdk' }
    else { Fail 'Android SDK not found: pass -AndroidSdk or set ANDROID_HOME' }
}
if (-not (Test-Path -LiteralPath $AndroidSdk -PathType Container)) { Fail "Android SDK not found: $AndroidSdk" }

$buildToolsRoot = Join-Path $AndroidSdk 'build-tools'
if ($BuildToolsVersion -eq '') {
    $newest = Get-ChildItem -LiteralPath $buildToolsRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $newest) { Fail "no build tools in $buildToolsRoot" }
    $BuildToolsVersion = $newest.Name
}
if ([version]$BuildToolsVersion -lt [version]'35.0.0') { Fail "build tools $BuildToolsVersion lack zipalign -P, 35.0.0 or newer is needed" }
$buildTools = Join-Path $buildToolsRoot $BuildToolsVersion
$apksigner = Join-Path $buildTools "apksigner$bat"
$zipalign = Join-Path $buildTools "zipalign$exe"
$aapt2 = Join-Path $buildTools "aapt2$exe"
foreach ($tool in @($apksigner, $zipalign, $aapt2)) {
    if (-not (Test-Path -LiteralPath $tool)) { Fail "tool not found: $tool" }
}

# The single sources of truth the Gradle build reads too.
$bridgeScript = [IO.File]::ReadAllText((Join-Path $androidRoot 'bridge/build.gradle.kts'))
$appScript = [IO.File]::ReadAllText((Join-Path $androidRoot 'app/build.gradle.kts'))
function Get-ScriptValue([string]$text, [string]$pattern, [string]$what) {
    $match = [regex]::Match($text, $pattern)
    if (-not $match.Success) { Fail "$what not found in the Gradle scripts" }
    return $match.Groups[1].Value
}
$ndkVersion = Get-ScriptValue $bridgeScript 'ndkVersion\s*=\s*"([^"]+)"' 'ndkVersion'
$cmakeVersion = Get-ScriptValue $bridgeScript 'version\s*=\s*"(\d+\.\d+\.\d+)"' 'the CMake version'
$compileSdk = Get-ScriptValue $appScript 'compileSdk\s*=\s*(\d+)' 'compileSdk'
$compileSdkMinor = Get-ScriptValue $appScript 'compileSdkMinor\s*=\s*(\d+)' 'compileSdkMinor'
$minSdk = Get-ScriptValue $appScript 'minSdk\s*=\s*(\d+)' 'minSdk'
$targetSdk = Get-ScriptValue $appScript 'targetSdk\s*=\s*(\d+)' 'targetSdk'
$applicationId = Get-ScriptValue $appScript 'applicationId\s*=\s*"([^"]+)"' 'applicationId'
$versionCatalog = [IO.File]::ReadAllText((Join-Path $androidRoot 'gradle/libs.versions.toml'))
$agpVersion = Get-ScriptValue $versionCatalog '(?m)^agp\s*=\s*"([^"]+)"' 'the AGP version'
$kotlinVersion = Get-ScriptValue $versionCatalog '(?m)^kotlin\s*=\s*"([^"]+)"' 'the Kotlin version'
$wrapper = Read-Properties (Join-Path $androidRoot 'gradle/wrapper/gradle-wrapper.properties')
$gradleVersion = ''
if ($wrapper.ContainsKey('distributionUrl') -and $wrapper['distributionUrl'] -match 'gradle-([0-9.]+(-[a-z0-9]+)?)-(bin|all)\.zip') { $gradleVersion = $Matches[1] }

$ndk = Join-Path $AndroidSdk "ndk/$ndkVersion"
$llvmPrebuilt = Get-ChildItem -LiteralPath (Join-Path $ndk 'toolchains/llvm/prebuilt') -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $llvmPrebuilt) { Fail "NDK $ndkVersion not found in $AndroidSdk" }
$llvmNm = Join-Path $llvmPrebuilt.FullName "bin/llvm-nm$exe"
$llvmReadelf = Join-Path $llvmPrebuilt.FullName "bin/llvm-readelf$exe"

# Same order as bridge/build.gradle.kts: local.properties, then the environment.
$androidDeps = ''
if ($localProperties.ContainsKey('phpvpn.androidDeps')) { $androidDeps = $localProperties['phpvpn.androidDeps'] }
elseif ($env:PHPVPN_ANDROID_DEPS) { $androidDeps = $env:PHPVPN_ANDROID_DEPS }
$depsManifestPath = Join-Path $androidDeps 'manifest.json'
if ($androidDeps -eq '' -or -not (Test-Path -LiteralPath $depsManifestPath -PathType Leaf)) {
    Fail 'the prebuilt OpenSSL and curl are missing (phpvpn.androidDeps or PHPVPN_ANDROID_DEPS, client/tools/android/README.md); without them the native library has no crypto provider and no transport'
}
$depsManifest = [IO.File]::ReadAllText($depsManifestPath) | ConvertFrom-Json

$cmakeText = [IO.File]::ReadAllText((Join-Path $clientRoot 'CMakeLists.txt'))
$versionMatch = [regex]::Match($cmakeText, 'project\(phpvpn-client\s+VERSION\s+(\d+)\.(\d+)\.(\d+)')
if (-not $versionMatch.Success) { Fail 'client/CMakeLists.txt has no project VERSION' }
$version = '{0}.{1}.{2}' -f $versionMatch.Groups[1].Value, $versionMatch.Groups[2].Value, $versionMatch.Groups[3].Value
$versionCode = [int]$versionMatch.Groups[1].Value * 10000 + [int]$versionMatch.Groups[2].Value * 100 + [int]$versionMatch.Groups[3].Value

$abis = @('arm64-v8a', 'x86_64')
$expectedPermissions = @(
    'android.permission.ACCESS_NETWORK_STATE',
    'android.permission.FOREGROUND_SERVICE',
    'android.permission.FOREGROUND_SERVICE_SYSTEM_EXEMPTED',
    'android.permission.INTERNET',
    'android.permission.POST_NOTIFICATIONS',
    'android.permission.RECEIVE_BOOT_COMPLETED'
)
# Exported components of every build type (manifest of app/src/main).
$releaseExported = @(
    'activity phpvpn.android.MainActivity',
    'receiver phpvpn.android.service.BootReceiver',
    'service phpvpn.android.service.PhpvpnVpnService'
)
# Debug-only exported components: the WP07 catalog and route switcher, the
# WP05 command receiver of service-lifecycle-tests.sh and the activities of
# the debugImplementation Compose tooling and test manifest artifacts.
$debugOnlyExported = @(
    'activity androidx.activity.ComponentActivity',
    'activity androidx.compose.ui.tooling.PreviewActivity',
    'activity phpvpn.android.debug.CatalogActivity',
    'activity phpvpn.android.debug.DebugRouteActivity',
    'receiver phpvpn.android.debug.DebugVpnCommandReceiver'
)

$outputRoot = Join-Path $repositoryRoot 'work/android'
$outputDirectory = Join-Path $outputRoot $version
$scratch = Join-Path $repositoryRoot 'work/build/android-dev-checks'

Write-Host "Version       : $version (versionCode $versionCode)"
Write-Host "JDK           : $JavaHome"
Write-Host "Android SDK   : $AndroidSdk (build tools $BuildToolsVersion, NDK $ndkVersion)"
Write-Host "Dependencies  : $androidDeps"
Write-Host "Output        : $outputDirectory"

# ------------------------------------------------------------------- build

function Invoke-Gradle([string[]]$Tasks) {
    $gradlew = Join-Path $androidRoot "gradlew$bat"
    $arguments = @('-p', $androidRoot, '-Pphpvpn.abiSplits=true') + $GradleArguments + $Tasks
    Write-Host "gradlew $($arguments -join ' ')"
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $gradlew @arguments | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    return $code
}

if ((Invoke-Gradle @('assembleDebug', 'assembleRelease')) -ne 0) { Fail 'gradlew assembleDebug assembleRelease failed' }
# Lint runs separately so that its result is one of the checks below.
$lintExitCode = Invoke-Gradle @(':app:lintDebug')

# ------------------------------------------------------------ the outputs

function Get-ApkOutputs([string]$buildType) {
    $directory = Join-Path $androidRoot "app/build/outputs/apk/$buildType"
    $metadataPath = Join-Path $directory 'output-metadata.json'
    if (-not (Test-Path -LiteralPath $metadataPath)) { Fail "no output-metadata.json in $directory" }
    $metadata = [IO.File]::ReadAllText($metadataPath) | ConvertFrom-Json
    $outputs = @{}
    foreach ($element in $metadata.elements) {
        $abi = 'universal'
        foreach ($filter in @($element.filters)) {
            if ($filter -and $filter.filterType -eq 'ABI') { $abi = $filter.value }
        }
        $outputs[$abi] = [pscustomobject]@{
            Path = Join-Path $directory $element.outputFile
            VersionCode = [int]$element.versionCode
            VersionName = [string]$element.versionName
        }
    }
    foreach ($required in @('universal') + $abis) {
        if (-not $outputs.ContainsKey($required)) { Fail "the $buildType build has no $required APK (are the ABI splits enabled?)" }
    }
    return $outputs
}

$debugOutputs = Get-ApkOutputs 'debug'
$releaseOutputs = Get-ApkOutputs 'release'

# Never erase anything but a version folder below work/android.
$outputDirectory = [IO.Path]::GetFullPath($outputDirectory)
if (-not $outputDirectory.StartsWith([IO.Path]::GetFullPath($outputRoot) + [IO.Path]::DirectorySeparatorChar)) { Fail "unexpected output directory $outputDirectory" }
if (Test-Path -LiteralPath $outputDirectory) { Remove-Item -LiteralPath $outputDirectory -Recurse -Force }
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force }
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

$apks = New-Object System.Collections.ArrayList
function Add-Apk([string]$source, [string]$name, [string]$buildType, [string]$abi) {
    $target = Join-Path $outputDirectory $name
    Copy-Item -LiteralPath $source -Destination $target
    [void]$apks.Add([pscustomobject]@{ Name = $name; Path = $target; BuildType = $buildType; Abi = $abi })
}
Add-Apk $debugOutputs['universal'].Path "phpvpn-$version-debug.apk" 'debug' 'universal'
foreach ($abi in $abis) { Add-Apk $debugOutputs[$abi].Path "phpvpn-$version-$abi-debug.apk" 'debug' $abi }
Add-Apk $releaseOutputs['universal'].Path "phpvpn-$version-release-unsigned.apk" 'release' 'universal'

# Non-stripped release libraries for crash symbolisation: the merged native
# libraries of the release variant, before stripReleaseDebugSymbols.
$symbolsSource = Join-Path $androidRoot 'app/build/intermediates/merged_native_libs/release/mergeReleaseNativeLibs/out/lib'
foreach ($abi in $abis) {
    $library = Join-Path $symbolsSource "$abi/libphpvpn-android.so"
    if (-not (Test-Path -LiteralPath $library)) { Fail "non-stripped release library not found: $library" }
    $target = Join-Path $outputDirectory "symbols/$abi"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -LiteralPath $library -Destination $target
}

# ----------------------------------------------------------------- checks

$checks = New-Object System.Collections.ArrayList
function Add-Check([string]$name, [string]$target, [bool]$passed, [string]$detail) {
    $status = 'FAIL'
    if ($passed) { $status = 'PASS' }
    [void]$checks.Add([pscustomobject][ordered]@{ check = $name; target = $target; status = $status; detail = $detail })
    $color = 'Green'
    if (-not $passed) { $color = 'Red' }
    Write-Host ("{0,-4} {1} [{2}] {3}" -f $status, $name, $target, $detail) -ForegroundColor $color
}

function Get-Lines($result, [string]$pattern) {
    return @($result.Lines | Where-Object { $_ -match $pattern })
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-NativeEntries([string]$apk) {
    $zip = [IO.Compression.ZipFile]::OpenRead($apk)
    try {
        return @($zip.Entries | Where-Object { $_.FullName -match '^lib/[^/]+/[^/]+\.so$' } |
            ForEach-Object { [pscustomobject]@{ Name = $_.FullName; Size = $_.Length; Compressed = $_.CompressedLength } })
    } finally {
        $zip.Dispose()
    }
}

function Expand-Entry([string]$apk, [string]$entryName, [string]$target) {
    $zip = [IO.Compression.ZipFile]::OpenRead($apk)
    try {
        $entry = $zip.GetEntry($entryName)
        if (-not $entry) { return $false }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        return $true
    } finally {
        $zip.Dispose()
    }
}

# Parses `aapt2 dump xmltree` into elements with their attributes and parent.
function Read-XmlTree($lines) {
    $elements = New-Object System.Collections.ArrayList
    $stack = New-Object System.Collections.ArrayList
    foreach ($line in $lines) {
        if ($line -match '^(\s*)E: (\S+)') {
            $depth = $Matches[1].Length
            while ($stack.Count -gt 0 -and $stack[$stack.Count - 1].Depth -ge $depth) { $stack.RemoveAt($stack.Count - 1) }
            $parent = $null
            if ($stack.Count -gt 0) { $parent = $stack[$stack.Count - 1] }
            $element = [pscustomobject]@{ Name = $Matches[2]; Depth = $depth; Parent = $parent; Attributes = @{}; Children = (New-Object System.Collections.ArrayList) }
            if ($parent) { [void]$parent.Children.Add($element) }
            [void]$elements.Add($element)
            [void]$stack.Add($element)
        } elseif ($line -match '^(\s*)A: (?:http://schemas\.android\.com/apk/res/android:)?([A-Za-z]+)(?:\(0x[0-9a-f]+\))?=(.*)$') {
            if ($stack.Count -eq 0) { continue }
            $attribute = $Matches[2]
            $value = $Matches[3]
            if ($value -match '^"([^"]*)"') { $value = $Matches[1] }
            $stack[$stack.Count - 1].Attributes[$attribute] = $value
        }
    }
    return $elements
}

function Test-True([string]$value) { return $value -match '^(true|\(type 0x12\)0xffffffff)$' }
function Test-False([string]$value) { return $value -match '^(false|\(type 0x12\)0x0)$' }

function Get-IntentFilterValues($element, [string]$kind) {
    $values = @()
    foreach ($filter in $element.Children | Where-Object { $_.Name -eq 'intent-filter' }) {
        foreach ($child in $filter.Children | Where-Object { $_.Name -eq $kind }) { $values += $child.Attributes['name'] }
    }
    return $values
}

$componentKinds = @('activity', 'activity-alias', 'service', 'receiver', 'provider')
$debugCertificateDn = 'C=US, O=Android, CN=Android Debug'
$apkFacts = @{}

foreach ($apk in $apks) {
    $label = $apk.Name
    $isDebug = $apk.BuildType -eq 'debug'
    $expectedAbis = $abis
    if ($apk.Abi -ne 'universal') { $expectedAbis = @($apk.Abi) }

    # --- signature
    $verify = Invoke-Tool $apksigner @('verify', '--print-certs', '-v', $apk.Path)
    $dn = ''
    $dnLine = Get-Lines $verify '^Signer #1 certificate DN: ' | Select-Object -First 1
    if ($dnLine) { $dn = $dnLine -replace '^Signer #1 certificate DN: ', '' }
    $schemes = (Get-Lines $verify '^Verified using v\d+(\.\d+)? scheme .*: true$' | ForEach-Object { ($_ -split ' ')[2] }) -join ','
    if ($isDebug) {
        Add-Check 'apksigner verify --print-certs' $label ($verify.ExitCode -eq 0 -and $dn -eq $debugCertificateDn) "DN '$dn', schemes $schemes"
    } else {
        # R2: no release signing config yet, Gradle leaves the APK unsigned.
        $unsigned = $verify.ExitCode -ne 0 -and $dn -eq ''
        $reason = (Get-Lines $verify 'ERROR|DOES NOT VERIFY' | Select-Object -First 1)
        Add-Check 'apksigner verify (signing state)' $label $unsigned "unsigned as expected under R2 ($reason)"
    }

    # --- 16 KB zip alignment of the uncompressed native libraries
    $align = Invoke-Tool $zipalign @('-c', '-P', '16', '-v', '4', $apk.Path)
    $alignOk = $align.ExitCode -eq 0 -and ($align.Lines -contains 'Verification successful')
    $badEntries = @(Get-Lines $align '\(BAD')
    Add-Check 'zipalign -c -P 16 -v 4' $label $alignOk ("{0} BAD entries" -f $badEntries.Count)

    # --- badging
    $badging = Invoke-Tool $aapt2 @('dump', 'badging', $apk.Path)
    $package = Get-Lines $badging '^package: ' | Select-Object -First 1
    $problems = @()
    if ($badging.ExitCode -ne 0 -or -not $package) { $problems += 'aapt2 dump badging failed' }
    else {
        if ($package -notmatch "name='$([regex]::Escape($applicationId))'") { $problems += "package $package" }
        if ($package -notmatch "versionCode='$versionCode'") { $problems += "versionCode is not $versionCode" }
        if ($package -notmatch "versionName='$([regex]::Escape($version))'") { $problems += "versionName is not $version" }
    }
    if (-not ($badging.Lines -contains "minSdkVersion:'$minSdk'") -or $minSdk -ne '34') { $problems += 'minSdk is not 34' }
    if (-not ($badging.Lines -contains "targetSdkVersion:'$targetSdk'") -or $targetSdk -ne '36') { $problems += 'targetSdk is not 36' }
    $nativeCode = Get-Lines $badging '^native-code: ' | Select-Object -First 1
    $expectedNativeCode = 'native-code: ' + (($expectedAbis | ForEach-Object { "'$_'" }) -join ' ')
    if ($nativeCode -ne $expectedNativeCode) { $problems += "$nativeCode (expected $expectedNativeCode)" }
    $permissions = @(Get-Lines $badging "^uses-permission: name='" | ForEach-Object { ($_ -replace "^uses-permission: name='([^']+)'.*$", '$1') } | Sort-Object -Unique)
    $permissionDiff = @(Compare-Object -ReferenceObject $expectedPermissions -DifferenceObject $permissions | ForEach-Object { "$($_.SideIndicator)$($_.InputObject)" })
    if ($permissionDiff.Count -gt 0) { $problems += 'permissions differ: ' + ($permissionDiff -join ' ') }
    $debuggable = $badging.Lines -contains 'application-debuggable'
    if ($debuggable -ne $isDebug) { $problems += "application-debuggable is $debuggable" }
    $detail = "versionCode $versionCode, minSdk $minSdk, targetSdk $targetSdk, $nativeCode, $($permissions.Count) permissions, debuggable $debuggable"
    if ($problems.Count -gt 0) { $detail = $problems -join '; ' }
    Add-Check 'aapt2 dump badging' $label ($problems.Count -eq 0) $detail

    # --- manifest tree: exported components, backup and extraction rules
    $tree = Invoke-Tool $aapt2 @('dump', 'xmltree', '--file', 'AndroidManifest.xml', $apk.Path)
    $elements = Read-XmlTree $tree.Lines
    $problems = @()
    $exported = @()
    foreach ($component in $elements | Where-Object { $componentKinds -contains $_.Name }) {
        $isExported = $false
        if ($component.Attributes.ContainsKey('exported')) { $isExported = Test-True $component.Attributes['exported'] }
        elseif ($component.Name -ne 'provider') { $isExported = @($component.Children | Where-Object { $_.Name -eq 'intent-filter' }).Count -gt 0 }
        if ($isExported) { $exported += "$($component.Name) $($component.Attributes['name'])" }
    }
    $exported = @($exported | Sort-Object)
    $allowedExported = $releaseExported
    if ($isDebug) { $allowedExported = $releaseExported + $debugOnlyExported }
    $missing = @($releaseExported | Where-Object { $exported -notcontains $_ })
    $unexpected = @($exported | Where-Object { $allowedExported -notcontains $_ })
    if ($missing.Count -gt 0) { $problems += 'not exported: ' + ($missing -join ', ') }
    if ($unexpected.Count -gt 0) { $problems += 'unexpected exported: ' + ($unexpected -join ', ') }

    $byName = @{}
    foreach ($component in $elements | Where-Object { $componentKinds -contains $_.Name }) { $byName[$component.Attributes['name']] = $component }
    $activity = $byName['phpvpn.android.MainActivity']
    if (-not $activity -or (Get-IntentFilterValues $activity 'action') -notcontains 'android.intent.action.MAIN' -or (Get-IntentFilterValues $activity 'category') -notcontains 'android.intent.category.LAUNCHER') {
        $problems += 'MainActivity is not the launcher activity'
    }
    $service = $byName['phpvpn.android.service.PhpvpnVpnService']
    if (-not $service) { $problems += 'no PhpvpnVpnService' }
    else {
        if ($service.Attributes['permission'] -ne 'android.permission.BIND_VPN_SERVICE') { $problems += 'the VPN service is not protected by BIND_VPN_SERVICE' }
        if (@(Get-IntentFilterValues $service 'action') -join ',' -ne 'android.net.VpnService') { $problems += 'the VPN service filter is not exactly android.net.VpnService' }
        # ServiceInfo.FOREGROUND_SERVICE_TYPE_SYSTEM_EXEMPTED = 0x400
        $type = [string]$service.Attributes['foregroundServiceType']
        if ($type -notmatch '0x0*400$') { $problems += "foregroundServiceType $type" }
    }
    $receiver = $byName['phpvpn.android.service.BootReceiver']
    if (-not $receiver -or (@(Get-IntentFilterValues $receiver 'action') -join ',') -ne 'android.intent.action.BOOT_COMPLETED') { $problems += 'the boot receiver filter is not exactly BOOT_COMPLETED' }
    $application = $elements | Where-Object { $_.Name -eq 'application' } | Select-Object -First 1
    if (-not $application) { $problems += 'no application element' }
    else {
        if (-not $application.Attributes.ContainsKey('allowBackup') -or -not (Test-False $application.Attributes['allowBackup'])) { $problems += 'allowBackup is not false' }
        if (-not $application.Attributes.ContainsKey('dataExtractionRules') -or $application.Attributes['dataExtractionRules'] -notmatch '^@0x7f') { $problems += 'no dataExtractionRules' }
    }
    $detail = 'exported: ' + ($exported -join ', ') + '; allowBackup=false, dataExtractionRules set'
    if ($problems.Count -gt 0) { $detail = $problems -join '; ' }
    Add-Check 'aapt2 dump xmltree AndroidManifest.xml' $label ($problems.Count -eq 0) $detail

    # --- native libraries
    $entries = Get-NativeEntries $apk.Path
    $entryAbis = @($entries | ForEach-Object { ($_.Name -split '/')[1] } | Sort-Object -Unique)
    $problems = @()
    if (($entryAbis -join ',') -ne (($expectedAbis | Sort-Object) -join ',')) { $problems += 'ABIs ' + ($entryAbis -join ',') }
    $testLibraries = @($entries | Where-Object { $_.Name -match '/libphpvpn-android-tests\.so$' })
    if ($testLibraries.Count -gt 0) { $problems += 'contains ' + (($testLibraries | ForEach-Object { $_.Name }) -join ', ') }
    $compressed = @($entries | Where-Object { $_.Compressed -ne $_.Size })
    if ($compressed.Count -gt 0) { $problems += 'compressed: ' + (($compressed | ForEach-Object { $_.Name }) -join ', ') }
    $detail = (($entries | ForEach-Object { '{0} {1} B' -f $_.Name, $_.Size }) -join ', ')
    if ($problems.Count -gt 0) { $detail = $problems -join '; ' }
    Add-Check 'native libraries (ABIs, no test library, stored)' $label ($problems.Count -eq 0) $detail

    $libraries = @{}
    foreach ($abi in $expectedAbis) {
        $entryName = "lib/$abi/libphpvpn-android.so"
        $extracted = Join-Path $scratch ("{0}/{1}/libphpvpn-android.so" -f [IO.Path]::GetFileNameWithoutExtension($apk.Name), $abi)
        if (-not (Expand-Entry $apk.Path $entryName $extracted)) {
            Add-Check 'libphpvpn-android.so present' "$label $abi" $false "no $entryName"
            continue
        }
        $target = "$label $abi"

        $nm = Invoke-Tool $llvmNm @('-D', '--defined-only', $extracted)
        $defined = @($nm.Lines | Where-Object { $_ -match '\S' } | ForEach-Object { ($_ -split '\s+')[-1] })
        Add-Check 'llvm-nm -D --defined-only' $target ($nm.ExitCode -eq 0 -and ($defined -join ',') -eq 'JNI_OnLoad') ('defined: ' + ($defined -join ', '))

        $program = Invoke-Tool $llvmReadelf @('-lW', $extracted)
        $loads = @(Get-Lines $program '^\s*LOAD\s')
        $alignments = @($loads | ForEach-Object { ($_.Trim() -split '\s+')[-1] })
        $aligned = $program.ExitCode -eq 0 -and $loads.Count -gt 0 -and @($alignments | Where-Object { $_ -ne '0x4000' }).Count -eq 0
        Add-Check 'llvm-readelf -l LOAD alignment' $target $aligned ("{0} LOAD, align {1}" -f $loads.Count, (($alignments | Sort-Object -Unique) -join ','))

        $sections = Invoke-Tool $llvmReadelf @('-SW', $extracted)
        $hasSymtab = @(Get-Lines $sections '\s\.symtab\s').Count -gt 0
        $hasDebug = @(Get-Lines $sections '\s\.debug_').Count -gt 0
        $notes = Invoke-Tool $llvmReadelf @('-n', $extracted)
        $buildId = ((Get-Lines $notes 'Build ID:' | Select-Object -First 1) -replace '^.*Build ID:\s*', '')
        if (-not $isDebug) {
            Add-Check 'release library stripped' $target (-not $hasSymtab -and -not $hasDebug) ".symtab $hasSymtab, .debug_* $hasDebug, build ID $buildId"
            $symbols = Join-Path $outputDirectory "symbols/$abi/libphpvpn-android.so"
            $symbolSections = Invoke-Tool $llvmReadelf @('-SW', $symbols)
            $symbolNotes = Invoke-Tool $llvmReadelf @('-n', $symbols)
            $symbolBuildId = ((Get-Lines $symbolNotes 'Build ID:' | Select-Object -First 1) -replace '^.*Build ID:\s*', '')
            $symbolDebug = @(Get-Lines $symbolSections '\s\.debug_info\s').Count -gt 0
            Add-Check 'symbols copy (non-stripped, same build ID)' "symbols/$abi" ($symbolDebug -and $buildId -ne '' -and $symbolBuildId -eq $buildId) ".debug_info $symbolDebug, build ID $symbolBuildId"
        }
        $libraries[$abi] = [ordered]@{ size = (Get-Item -LiteralPath $extracted).Length; buildId = $buildId; stripped = (-not $hasSymtab -and -not $hasDebug) }
    }
    $apkFacts[$apk.Name] = [ordered]@{ entries = $entries; libraries = $libraries; signer = $dn; schemes = $schemes }
}

# --- lint
$lintXml = Join-Path $androidRoot 'app/build/reports/lint-results-debug.xml'
$lintCounts = [ordered]@{ fatal = 0; error = 0; warning = 0; information = 0 }
$lintIssues = @{}
if (Test-Path -LiteralPath $lintXml) {
    [xml]$lintReport = [IO.File]::ReadAllText($lintXml)
    foreach ($issue in @($lintReport.issues.issue)) {
        if (-not $issue) { continue }
        $severity = ([string]$issue.severity).ToLowerInvariant()
        if ($lintCounts.Contains($severity)) { $lintCounts[$severity]++ }
        $key = "$($issue.severity) $($issue.id)"
        if ($lintIssues.ContainsKey($key)) { $lintIssues[$key]++ } else { $lintIssues[$key] = 1 }
    }
}
$lintSummary = ($lintIssues.Keys | Sort-Object | ForEach-Object { "$_ x$($lintIssues[$_])" }) -join ', '
if ($lintSummary -eq '') { $lintSummary = 'no issues' }
Add-Check 'lint :app:lintDebug' 'app' ($lintExitCode -eq 0 -and (Test-Path -LiteralPath $lintXml) -and $lintCounts.fatal -eq 0 -and $lintCounts.error -eq 0) ("{0} errors, {1} warnings: {2}" -f ($lintCounts.fatal + $lintCounts.error), $lintCounts.warning, $lintSummary)

# ------------------------------------------------- manifest and checksums

$commit = (Invoke-Tool 'git' @('-C', $repositoryRoot, 'rev-parse', 'HEAD')).Text.Trim()
$dirty = (Invoke-Tool 'git' @('-C', $repositoryRoot, 'status', '--porcelain')).Text.Trim() -ne ''
$javaVersion = ((Invoke-Tool $java @('-version')).Lines | Select-Object -First 1)

$files = @(Get-ChildItem -LiteralPath $outputDirectory -Recurse -File | Sort-Object FullName)
$fileList = @()
foreach ($file in $files) {
    $relative = $file.FullName.Substring($outputDirectory.Length + 1) -replace '\\', '/'
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $fileList += [pscustomobject][ordered]@{ path = $relative; size = $file.Length; sha256 = $hash }
}

$apkEntries = @()
foreach ($apk in $apks) {
    $facts = $apkFacts[$apk.Name]
    $signing = 'debug keystore'
    if ($apk.BuildType -eq 'release') { $signing = 'unsigned' }
    $apkEntries += [ordered]@{
        file = $apk.Name; buildType = $apk.BuildType; abi = $apk.Abi; signing = $signing; signer = $facts.signer
        size = (Get-Item -LiteralPath $apk.Path).Length
        nativeLibraries = @($facts.entries | ForEach-Object { [ordered]@{ path = $_.Name; size = $_.Size } })
        libphpvpnAndroid = $facts.libraries
    }
}

$manifest = [ordered]@{
    schema = 1
    generator = 'client/tools/android/make-android-dev.ps1'
    purpose = 'development build, not a release (plan-android-v001 R2)'
    applicationId = $applicationId
    version = $version
    versionCode = $versionCode
    commit = $commit
    sourceDirty = $dirty
    buildDate = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    abis = $abis
    sdk = [ordered]@{
        compileSdk = "$compileSdk.$compileSdkMinor"; minSdk = [int]$minSdk; targetSdk = [int]$targetSdk
        buildTools = $BuildToolsVersion; ndk = $ndkVersion; cmake = $cmakeVersion
        agp = $agpVersion; kotlin = $kotlinVersion; gradle = $gradleVersion; jdk = $javaVersion
    }
    dependencies = [ordered]@{
        directory = Split-Path -Leaf $androidDeps
        recipeSha256 = $depsManifest.recipe_sha256
        buildDate = $depsManifest.build_date
        ndk = $depsManifest.ndk.revision
        apiLevel = $depsManifest.api_level
        components = @($depsManifest.components | ForEach-Object { [ordered]@{ name = $_.name; version = $_.version; sha256 = $_.sha256 } })
    }
    apks = $apkEntries
    symbols = 'symbols/<abi>/libphpvpn-android.so: app/build/intermediates/merged_native_libs/release/mergeReleaseNativeLibs/out/lib/<abi>, before stripReleaseDebugSymbols'
    lint = [ordered]@{ errors = $lintCounts.fatal + $lintCounts.error; warnings = $lintCounts.warning; information = $lintCounts.information; issues = $lintSummary }
    checks = @($checks)
    files = $fileList
}
$manifestPath = Join-Path $outputDirectory 'manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 10) + "`n", $utf8)

$sums = New-Object Text.StringBuilder
foreach ($file in @(Get-ChildItem -LiteralPath $outputDirectory -Recurse -File | Where-Object { $_.Name -ne 'SHA256SUMS' } | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($outputDirectory.Length + 1) -replace '\\', '/'
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    [void]$sums.Append("$hash  $relative`n")
}
[IO.File]::WriteAllText((Join-Path $outputDirectory 'SHA256SUMS'), $sums.ToString(), $utf8)

$failed = @($checks | Where-Object { $_.status -ne 'PASS' })
Write-Host ''
Write-Host ("Checks        : {0} passed, {1} failed" -f ($checks.Count - $failed.Count), $failed.Count)
Write-Host "Output        : $outputDirectory"
if ($failed.Count -gt 0) { exit 1 }
exit 0
