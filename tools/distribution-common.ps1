# Shared, explicit distribution contract. Source paths are checkout-relative.
function Get-DistributionSigningCertificate {
    param([string]$Thumbprint, [switch]$RequirePrivateKey)
    if ([string]::IsNullOrWhiteSpace($Thumbprint)) {
        . (Join-Path $PSScriptRoot 'phpvpn-config.ps1')
        $Thumbprint = [string](Get-PhpVpnConfigValue 'signing.certificateThumbprint')
    }
    if ($Thumbprint -notmatch '^[0-9a-fA-F]{40}$') {
        throw 'Set signing.certificateThumbprint in phpvpn.config.json or supply -CertificateThumbprint (40 hexadecimal characters).'
    }
    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$Thumbprint" -ErrorAction Stop
    $rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
    if ($null -eq $rsa) { throw 'Only an RSA signing certificate is supported.' }
    $rsa.Dispose()
    if ($RequirePrivateKey -and -not $certificate.HasPrivateKey) { throw 'The selected signing certificate has no private key.' }
    return $certificate
}

function Get-PinnedInstallerText {
    param([string]$Source, [ValidateSet('Windows','Linux','macOS')][string]$Platform,
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)
    $text = [IO.File]::ReadAllText($Source)
    if ($Platform -eq 'Windows') {
        $marker = '@PHPVPN_SIGNING_THUMBPRINT@'
        $pin = $Certificate.Thumbprint.ToUpperInvariant()
    } else {
        $marker = '@PHPVPN_SIGNING_SPKI_SHA256@'
        $pin = Get-SubjectPublicKeyInfoSha256 $Certificate.RawData
    }
    if ([regex]::Matches($text, [regex]::Escape($marker)).Count -ne 1) {
        throw "Installer template must contain exactly one $marker marker: $Source"
    }
    return $text.Replace($marker, $pin)
}

function Write-PinnedInstaller {
    param([string]$Source, [string]$Destination, [ValidateSet('Windows','Linux','macOS')][string]$Platform,
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)
    $text = Get-PinnedInstallerText -Source $Source -Platform $Platform -Certificate $Certificate
    [IO.File]::WriteAllText($Destination, $text, [Text.UTF8Encoding]::new($false))
}

function Assert-PinnedInstaller {
    param([string]$Source, [string]$Copy, [ValidateSet('Windows','Linux','macOS')][string]$Platform,
        [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)
    $expected = [Text.Encoding]::UTF8.GetBytes((Get-PinnedInstallerText -Source $Source -Platform $Platform -Certificate $Certificate))
    if (-not (Test-ByteArrayEqual $expected ([IO.File]::ReadAllBytes($Copy)))) {
        throw "Installer differs from the source template and configured signing identity: $Copy"
    }
}

# Server folders of the distribution, lower case, each with its English README.md. The source
# folders of the checkout have the same names, so a distributed file keeps its relative path.
function Get-DistributionServerFolders {
    return @('server/php', 'server/python', 'server/node.js', 'server/wordpress')
}

function Get-DistributionSources {
    $files = [ordered]@{ 'documentation/readme.txt' = 'readme.txt' }
    $names = [ordered]@{
        'server/php' = @('phpvpn.php')
        'server/python' = @('phpvpn.py','requirements-server.txt','init.bat','start.bat','stop.bat','init.sh','start.sh','stop.sh','bootstrap.ps1','server-control.py')
        'server/node.js' = @('phpvpn.js')
        'server/wordpress' = @()
    }
    foreach ($folder in Get-DistributionServerFolders) {
        foreach ($name in @($names[$folder]) + 'README.md') { $files["$folder/$name"] = "$folder/$name" }
    }
    return $files
}

# The VPN Server WordPress plugin is distributed as the installable ZIP of its plugin folder,
# server/wordpress/vpn-server-X.Y.Z.zip with every entry below vpn-server/.
function Get-WordPressPluginDirectory([string]$Repository) {
    return Join-Path $Repository 'server/wordpress/version7/vpn-server'
}

function Get-WordPressPluginZipName([string]$Repository) {
    $header = [IO.File]::ReadAllText((Join-Path (Get-WordPressPluginDirectory $Repository) 'vpn-server.php'))
    $match = [regex]::Match($header, '(?m)^ \* Version:\s+(\d+\.\d+\.\d+)\s*$')
    if (-not $match.Success) { throw 'The VPN Server plugin version was not found in vpn-server.php.' }
    return "server/wordpress/vpn-server-$($match.Groups[1].Value).zip"
}

function Get-WordPressPluginFiles([string]$Repository) {
    $plugin = (Resolve-Path -LiteralPath (Get-WordPressPluginDirectory $Repository)).Path
    $files = [ordered]@{}
    foreach ($file in Get-ChildItem -LiteralPath $plugin -File -Recurse -Force | Sort-Object FullName) {
        $files['vpn-server/' + $file.FullName.Substring($plugin.Length + 1).Replace('\','/')] = $file.FullName
    }
    return $files
}

function Write-WordPressPluginZip([string]$Repository, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::Open($Destination, 'Create')
    try {
        foreach ($entry in (Get-WordPressPluginFiles $Repository).GetEnumerator()) {
            $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $entry.Value, $entry.Key)
        }
    } finally { $zip.Dispose() }
}

# The plugin ZIP holds exactly the files of the plugin folder, byte for byte.
function Assert-WordPressPluginZip([string]$Repository, [string]$Zip) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $expected = Get-WordPressPluginFiles $Repository
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $names = @($archive.Entries | ForEach-Object { $_.FullName })
        if (Compare-Object @($expected.Keys) $names -CaseSensitive) { throw "WordPress plugin ZIP has missing or unexpected entries: $Zip" }
        foreach ($entry in $archive.Entries) {
            $stream = $entry.Open()
            $buffer = New-Object IO.MemoryStream
            try { $stream.CopyTo($buffer) } finally { $stream.Dispose() }
            if (-not (Test-ByteArrayEqual $buffer.ToArray() ([IO.File]::ReadAllBytes($expected[$entry.FullName])))) {
                throw "Stale WordPress plugin ZIP entry: $($entry.FullName)"
            }
        }
    } finally { $archive.Dispose() }
}

# Translation is maintained explicitly outside dist, never generated by a build.
# A build may omit it, even when an older retained translation exists.
function Get-DistributionProtocolSource {
    param([string]$Repository)
    $path = Join-Path $Repository 'documentation/protocol.en.md'
    if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
}

function Assert-DistributionPath {
    param([string]$Path, [string]$Within, [switch]$Tree)
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    $parent = [IO.Path]::GetFullPath($Within).TrimEnd('\','/')
    if (-not $full.StartsWith($parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe target outside the permitted directory: $full"
    }
    $current = $full
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse path is not allowed: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
    if ($Tree -and (Test-Path -LiteralPath $full)) {
        # Enumerate level by level so traversal never enters a reparse directory.
        $pending = New-Object 'Collections.Generic.Queue[string]'
        $pending.Enqueue($full)
        while ($pending.Count) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
                if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse entry: $($item.FullName)" }
                if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
            }
        }
    }
    return $full
}

# ZIP of the whole Source tree with POSIX modes: directories 0755, *.sh 0755, other files 0644.
function Write-DistributionZip([string]$Source, [string]$Archive) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $root = [IO.Path]::GetFullPath($Source).TrimEnd('\','/')
    $zip = [IO.Compression.ZipFile]::Open($Archive, 'Create')
    try {
        foreach ($directory in Get-ChildItem -LiteralPath $root -Directory -Recurse) {
            $name = $directory.FullName.Substring($root.Length + 1).Replace('\','/') + '/'
            $entry = $zip.CreateEntry($name)
            $entry.ExternalAttributes = 1106051088 # POSIX directory 0755 plus DOS directory flag
        }
        foreach ($file in Get-ChildItem -LiteralPath $root -File -Recurse) {
            $name = $file.FullName.Substring($root.Length + 1).Replace('\','/')
            $entry = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $name)
            # Signed Int32 representation of regular file 0755 / 0644.
            $entry.ExternalAttributes = if ($name.EndsWith('.sh')) { -2115174400 } else { -2119958528 }
        }
    } finally { $zip.Dispose() }
}

# Extraction on Windows drops POSIX modes, so check them in the archive itself.
function Assert-DistributionZipModes([string]$Archive, [string[]]$ExecutableEntries = @()) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        foreach ($entry in $zip.Entries) {
            $mode = ($entry.ExternalAttributes -shr 16) -band 0xFFFF
            $expectedMode = if ($entry.FullName.EndsWith('/')) { 0x41ED } elseif ($entry.FullName.EndsWith('.sh')) { 0x81ED } else { 0x81A4 }
            if ($mode -ne $expectedMode) { throw "ZIP entry has an incorrect POSIX mode: $($entry.FullName)" }
        }
        foreach ($name in $ExecutableEntries) {
            if ($null -eq $zip.GetEntry($name)) { throw "ZIP is missing the executable entry point $name" }
        }
    } finally { $zip.Dispose() }
}

function Get-UnsupportedClientReadme([string]$Platform) {
    return "# PHP VPN client for $Platform`n`nNo distributable client implementation is available for this platform yet.`nThis directory does not claim platform support or completed validation.`n"
}

# Linux client folder client/Linux/x64, staged by client/tools/linux/make-linux-dist.ps1.
# The complete distribution requires phpvpn and phpvpn-tray for both Fedora and Ubuntu.
function Get-LinuxPackagePatterns {
    return [ordered]@{
        rpm     = '^phpvpn-(\d+\.\d+\.\d+)-(\d+)\.x86_64\.rpm$'
        deb     = '^phpvpn_(\d+\.\d+\.\d+)-(\d+)_amd64\.deb$'
        trayRpm = '^phpvpn-tray-(\d+\.\d+\.\d+)-(\d+)\.x86_64\.rpm$'
        trayDeb = '^phpvpn-tray_(\d+\.\d+\.\d+)-(\d+)_amd64\.deb$'
    }
}

# Unchanged copies in client/Linux/x64 besides the packages and the signed manifest.
function Get-LinuxClientSources {
    return [ordered]@{
        'install.sh' = 'client/packaging/linux/dist/install.sh'
        'uninstall.sh' = 'client/packaging/linux/dist/uninstall.sh'
        'README.md' = 'client/packaging/linux/dist/README.md'
    }
}

# macOS client folder client/macOS/arm64, staged by client/tools/macos/make-macos-dist.ps1 from a
# package build of client/tools/macos/package-pkg.sh (the GitHub Actions artifact phpvpn-macos-arm64,
# by convention copied to work/macos-packages/latest). Without a package build client/macOS holds
# only the placeholder README of Get-UnsupportedClientReadme.
function Get-MacosClientFolder { return 'client/macOS/arm64' }

function Get-MacosPackagePattern { return '^phpvpn-(\d+\.\d+\.\d+)-arm64\.pkg$' }

function Get-MacosMinimumOsVersion { return '14.0' }

# Unchanged copies in client/macOS/arm64 besides the package and the signed manifest
# (install.sh carries the pinned signing key).
function Get-MacosClientSources {
    return [ordered]@{
        'install.sh' = 'client/packaging/macos/dist/install.sh'
        'uninstall.sh' = 'client/packaging/macos/dist/uninstall.sh'
        'README.md' = 'client/packaging/macos/dist/README.md'
    }
}

# 'placeholder' when client/macOS holds only the placeholder README.md, 'staged' when it holds only
# the arm64 client folder. Anything else is a half-staged folder and is rejected.
function Get-MacosDistributionState([string]$Tree) {
    $root = Join-Path $Tree 'client/macOS'
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Distribution folder client/macOS is missing.' }
    $entries = @(Get-ChildItem -LiteralPath $root -Force | ForEach-Object {
        if ($_.PSIsContainer) { $_.Name + '/' } else { $_.Name }
    })
    if ($entries.Count -eq 1 -and $entries[0] -ceq 'README.md') { return 'placeholder' }
    if ($entries.Count -eq 1 -and $entries[0] -ceq 'arm64/') { return 'staged' }
    throw ("client/macOS is half-staged: it must hold either only the placeholder README.md or only the arm64 " +
        "client folder (found: $($entries -join ', ')).")
}

# Checks a package build directory of client/tools/macos/package-pkg.sh: SHA256SUMS, exactly one
# product archive of the client version and build-provenance.json. The unstaged README.md,
# install.sh and uninstall.sh and the logs folder of the CI artifact may be present; staging takes
# those files from the checkout instead.
function Read-MacosPackageBuild([string]$Directory, [string]$ClientVersion) {
    $packages = (Resolve-Path -LiteralPath $Directory).Path
    $sumsPath = Join-Path $packages 'SHA256SUMS'
    $provenancePath = Join-Path $packages 'build-provenance.json'
    foreach ($required in @($sumsPath, $provenancePath)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "The macOS package build is incomplete, $(Split-Path $required -Leaf) is missing in $packages."
        }
    }
    $sums = @{}
    foreach ($line in [IO.File]::ReadAllLines($sumsPath)) {
        if ($line -eq '') { continue }
        if ($line -notmatch '^([0-9a-f]{64}) [ *]([A-Za-z0-9._+-]+)$') { throw "macOS SHA256SUMS holds a malformed line: $line" }
        $sums[$Matches[2]] = $Matches[1]
    }
    foreach ($name in $sums.Keys) {
        $path = Join-Path $packages $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Sha256 $path) -ne $sums[$name]) {
            throw "macOS package file $name is missing or does not match SHA256SUMS."
        }
    }
    $found = @(Get-ChildItem -LiteralPath $packages -File -Force | Where-Object { $_.Extension -eq '.pkg' })
    if ($found.Count -ne 1) { throw "The macOS package build must hold exactly one .pkg, found $($found.Count) in $packages." }
    $package = $found[0].Name
    if (-not ($package -cmatch (Get-MacosPackagePattern))) { throw "$package is not a phpvpn macOS arm64 package." }
    if ($Matches[1] -ne $ClientVersion) { throw "$package is not client version $ClientVersion." }
    if (-not $sums.ContainsKey($package)) { throw "$package is not listed in SHA256SUMS." }
    $magic = New-Object byte[] 4
    $stream = [IO.File]::OpenRead($found[0].FullName)
    try { $read = $stream.Read($magic, 0, 4) } finally { $stream.Dispose() }
    if ($read -ne 4 -or [Text.Encoding]::ASCII.GetString($magic) -cne 'xar!') { throw "$package is not a xar installer package." }

    $provenance = [IO.File]::ReadAllText($provenancePath) | ConvertFrom-Json
    function Get-Field([string]$Name) {
        $property = $provenance.PSObject.Properties[$Name]
        if ($null -eq $property) { return $null }
        return $property.Value
    }
    if ((Get-Field 'formatVersion') -ne 1 -or (Get-Field 'product') -cne 'phpvpn' -or (Get-Field 'platform') -cne 'macos' -or
        (Get-Field 'architecture') -cne 'arm64') { throw 'build-provenance.json is not a phpvpn macOS arm64 provenance.' }
    if ((Get-Field 'version') -cne $ClientVersion) { throw "build-provenance.json names version $(Get-Field 'version'), the client is $ClientVersion." }
    if ((Get-Field 'minimumOsVersion') -cne (Get-MacosMinimumOsVersion)) {
        throw "build-provenance.json names minimum macOS $(Get-Field 'minimumOsVersion'), the distribution requires $(Get-MacosMinimumOsVersion)."
    }
    if ([string](Get-Field 'sourceCommit') -notmatch '^[0-9a-f]{40}$' -or (Get-Field 'sourceDirty') -isnot [bool]) {
        throw 'build-provenance.json has no valid source commit and dirty flag.'
    }
    foreach ($name in @('builtOn', 'xcode')) {
        $value = Get-Field $name
        if ($value -isnot [string] -or $value.Trim() -eq '') { throw "build-provenance.json has no $name." }
    }
    if ((Get-Field 'trayIncluded') -isnot [bool]) { throw 'build-provenance.json has no trayIncluded flag.' }
    $entries = @(Get-Field 'packages')
    if ($entries.Count -ne 1 -or $entries[0].file -cne $package -or $entries[0].sha256 -cne $sums[$package] -or
        [long]$entries[0].bytes -ne $found[0].Length) { throw "The provenance of $package does not match the package and SHA256SUMS." }
    return [pscustomobject]@{
        Directory  = $packages
        Package    = $package
        Sha256     = $sums[$package]
        Provenance = $provenance
    }
}

function Get-ClientVersion([string]$Repository) {
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $Repository 'client/CMakeLists.txt'))) {
        if ($line -match '^\s*VERSION\s+(\d+\.\d+\.\d+)\s*$') { return $Matches[1] }
    }
    throw 'The client version was not found in client/CMakeLists.txt.'
}

# Every place that carries the product version, matched by exactly one occurrence of
# Pattern (multiline). Group v is the version and the optional group d the manual date.
# bump-version.ps1 rewrites these groups in the Replace entries and prepends a new entry
# to both package changelogs, whose top entry is the remaining location.
function Get-VersionLocations {
    $version = '(?<v>\d+\.\d+\.\d+)'
    $man = "^\.TH \S+ \d `"(?<d>\d{4}-\d{2}-\d{2})`" `"phpvpn $version`""
    $header = "PHP VPN $version \(MIT License, Copyright \(c\) www\.pilandrome\.com\)\r?$"
    return @(
        @{ Path = 'client/CMakeLists.txt'; Pattern = "^\s*VERSION\s+$version\s*$"; Replace = $true }
        @{ Path = 'client/packaging/linux/rpm/phpvpn.spec'; Pattern = "^Version:\s+$version\s*$"; Replace = $true }
        @{ Path = 'client/packaging/linux/rpm/phpvpn.spec'; Pattern = "^%changelog\r?\n\* .+ - $version-1\r?$"; Replace = $false }
        @{ Path = 'client/packaging/linux/debian/changelog'; Pattern = "\Aphpvpn \($version-1\)"; Replace = $false }
        @{ Path = 'client/packaging/linux/man/phpvpn.1'; Pattern = $man; Replace = $true }
        @{ Path = 'client/packaging/linux/man/phpvpn-tray.1'; Pattern = $man; Replace = $true }
        @{ Path = 'client/packaging/linux/man/phpvpn-service.8'; Pattern = $man; Replace = $true }
        @{ Path = 'server/php/phpvpn.php'; Pattern = "\A<\?php\r?\n// $header"; Replace = $true }
        @{ Path = 'server/php/phpvpn.php'; Pattern = "^const SERVER_VERSION = '$version';"; Replace = $true }
        @{ Path = 'server/python/phpvpn.py'; Pattern = "\A#!.*\r?\n# $header"; Replace = $true }
        @{ Path = 'server/python/phpvpn.py'; Pattern = "^SERVER_VERSION = '$version'"; Replace = $true }
        @{ Path = 'server/node.js/phpvpn.js'; Pattern = "\A#!.*\r?\n// $header"; Replace = $true }
        @{ Path = 'server/node.js/phpvpn.js'; Pattern = "^    SERVER_VERSION: '$version',"; Replace = $true }
        @{ Path = 'server/wordpress/version7/vpn-server/vpn-server.php'; Pattern = "^ \* Version:\s+$version\s*$"; Replace = $true }
        @{ Path = 'server/wordpress/version7/vpn-server/vpn-server.php'; Pattern = "^const VERSION = '$version';"; Replace = $true }
        @{ Path = 'server/wordpress/version7/vpn-server/readme.txt'; Pattern = "^Stable tag: $version\s*$"; Replace = $true }
        @{ Path = 'server/wordpress/version7/vpn-server/includes/server-core.php'; Pattern = "^ \* PHPVPN server core of the VPN Server plugin, version $version\.\s*$"; Replace = $true }
        @{ Path = 'server/wordpress/version7/vpn-server/includes/server-core.php'; Pattern = "^const SERVER_VERSION = '$version';"; Replace = $true }
    )
}

# Returns one problem per location that is missing, ambiguous or not at $Version.
function Get-VersionLocationProblems([string]$Repository, [string]$Version) {
    $problems = New-Object 'Collections.Generic.List[string]'
    foreach ($location in Get-VersionLocations) {
        $text = [IO.File]::ReadAllText((Join-Path $Repository $location.Path))
        $found = [regex]::Matches($text, $location.Pattern, 'Multiline')
        if ($found.Count -ne 1) { $problems.Add("$($location.Path): $($found.Count) version entries match $($location.Pattern)"); continue }
        if ($found[0].Groups['v'].Value -cne $Version) {
            $problems.Add("$($location.Path): version $($found[0].Groups['v'].Value) instead of $Version")
        }
    }
    return ,$problems
}

function Test-DistributionRuntimePath([string]$RelativePath) {
    # Local operational state of a server started directly from dist: the Python venv and
    # control state, and the default Node.js disk queue next to phpvpn.js. The match ignores
    # case, so state kept in a folder of an older distribution (server/Python) is preserved too.
    return $RelativePath.Replace('\', '/') -match '^server/(python/\.(runtime|venv)|node\.js/phpvpn_data)(/|$)'
}

function Publish-Distribution {
    param([string]$Repository, [string]$Session, [string]$Staging, [string]$Archive)
    $ErrorActionPreference = 'Stop'
    $dist = Assert-DistributionPath (Join-Path $Repository 'dist') $Repository -Tree
    $zip = Assert-DistributionPath (Join-Path $Repository 'dist.zip') $Repository
    $staging = Assert-DistributionPath $Staging $Session -Tree
    $archive = Assert-DistributionPath $Archive $Session
    $previous = Assert-DistributionPath (Join-Path $Session 'previous-dist') $Session
    $previousZip = Assert-DistributionPath (Join-Path $Session 'previous-dist.zip') $Session
    $failed = Assert-DistributionPath (Join-Path $Session 'failed-publication') $Session
    foreach ($path in @($previous, $previousZip, $failed)) {
        if (Test-Path -LiteralPath $path) { throw "Publication backup already exists: $path" }
    }
    if (-not (Test-Path -LiteralPath $staging -PathType Container) -or
        -not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw 'Verified distribution staging or ZIP is missing.' }
    [IO.Directory]::CreateDirectory($dist) | Out-Null
    [IO.Directory]::CreateDirectory($previous) | Out-Null
    $oldEntries = @(Get-ChildItem -LiteralPath $dist -Recurse -Force | Where-Object {
        -not (Test-DistributionRuntimePath $_.FullName.Substring($dist.Length + 1))
    })
    $newEntries = @(Get-ChildItem -LiteralPath $staging -Recurse -Force)
    foreach ($entry in $newEntries) {
        if (Test-DistributionRuntimePath $entry.FullName.Substring($staging.Length + 1)) {
            throw 'Distribution staging must not contain local server runtime state.'
        }
    }
    $oldDirectories = @($oldEntries | Where-Object PSIsContainer | ForEach-Object { $_.FullName.Substring($dist.Length + 1) })
    $newDirectories = @($newEntries | Where-Object PSIsContainer | ForEach-Object { $_.FullName.Substring($staging.Length + 1) })
    $savedNames = [Collections.Generic.List[string]]::new()
    $publishedNames = [Collections.Generic.List[string]]::new()
    $savedZip = $false
    try {
        # Preserve directory handles/current directories held by open shells.
        # Only replace distribution files. Operational Python state is excluded
        # from both publication and rollback and remains at its original path.
        foreach ($name in $oldDirectories) {
            [IO.Directory]::CreateDirectory((Assert-DistributionPath (Join-Path $previous $name) $previous)) | Out-Null
        }
        foreach ($entry in $oldEntries | Where-Object { -not $_.PSIsContainer }) {
            $name = $entry.FullName.Substring($dist.Length + 1)
            $source = Assert-DistributionPath $entry.FullName $dist
            $destination = Assert-DistributionPath (Join-Path $previous $name) $previous
            Move-Item -LiteralPath $source -Destination $destination
            $savedNames.Add($name)
        }
        if (Test-Path -LiteralPath $zip) {
            Move-Item -LiteralPath $zip -Destination $previousZip
            $savedZip = $true
        }
        foreach ($name in $oldDirectories | Sort-Object Length -Descending) {
            if ($name -notin $newDirectories) {
                $path = Assert-DistributionPath (Join-Path $dist $name) $dist
                [IO.Directory]::Delete($path) # Empty only, never recursive.
            }
        }
        foreach ($name in $newDirectories) {
            [IO.Directory]::CreateDirectory((Assert-DistributionPath (Join-Path $dist $name) $dist)) | Out-Null
        }
        foreach ($entry in $newEntries | Where-Object { -not $_.PSIsContainer }) {
            $name = $entry.FullName.Substring($staging.Length + 1)
            $source = Assert-DistributionPath $entry.FullName $staging
            $destination = Assert-DistributionPath (Join-Path $dist $name) $dist
            Move-Item -LiteralPath $source -Destination $destination
            $publishedNames.Add($name)
        }
        Move-Item -LiteralPath $archive -Destination $zip
    } catch {
        [IO.Directory]::CreateDirectory($failed) | Out-Null
        foreach ($name in $publishedNames) {
            $source = Assert-DistributionPath (Join-Path $dist $name) $dist
            $destination = Assert-DistributionPath (Join-Path $failed $name) $failed
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
            Move-Item -LiteralPath $source -Destination $destination
        }
        foreach ($name in $oldDirectories) {
            [IO.Directory]::CreateDirectory((Assert-DistributionPath (Join-Path $dist $name) $dist)) | Out-Null
        }
        foreach ($name in $savedNames) {
            $source = Assert-DistributionPath (Join-Path $previous $name) $previous
            $destination = Assert-DistributionPath (Join-Path $dist $name) $dist
            Move-Item -LiteralPath $source -Destination $destination
        }
        foreach ($name in $newDirectories | Sort-Object Length -Descending) {
            $path = Assert-DistributionPath (Join-Path $dist $name) $dist
            if ($name -notin $oldDirectories -and (Test-Path -LiteralPath $path)) { [IO.Directory]::Delete($path) }
        }
        if ($savedZip) { Move-Item -LiteralPath $previousZip -Destination $zip }
        throw
    }
    # Windows keeps the case of an existing directory, so a folder whose name changed only in
    # case (server/Python -> server/python) is renamed explicitly, parents first.
    foreach ($name in $newDirectories | Sort-Object Length) {
        $wanted = [IO.Path]::GetFileName($name)
        $parent = [IO.Path]::GetDirectoryName((Assert-DistributionPath (Join-Path $dist $name) $dist))
        # Only an enumeration reports the name as stored on disk.
        $actual = Get-ChildItem -LiteralPath $parent -Directory -Force | Where-Object { $_.Name -eq $wanted }
        if ($null -eq $actual -or $actual.Name -ceq $wanted) { continue }
        $temporary = Assert-DistributionPath (Join-Path $actual.Parent.FullName ('.rename-' + [Guid]::NewGuid().ToString('N'))) $dist
        try {
            [IO.Directory]::Move($actual.FullName, $temporary)
            [IO.Directory]::Move($temporary, (Join-Path $actual.Parent.FullName $wanted))
        } catch {
            if ((Test-Path -LiteralPath $temporary) -and -not (Test-Path -LiteralPath $actual.FullName)) {
                [IO.Directory]::Move($temporary, $actual.FullName)
            }
            Write-Warning "The folder $($actual.FullName) keeps its old name, rename it to $wanted (a running server may hold it): $($_.Exception.Message)"
        }
    }
}

# ------------------------------------------------ release manifest and signature
# Shared by client/tools/make-dist.ps1 (Windows client package) and
# client/tools/linux/make-linux-dist.ps1 (Linux client folder). The manifest is
# compact UTF-8 JSON without a byte order mark or line break, and the signature
# is stored separately, so the exact bytes that were signed are the exact bytes
# the installers verify.

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-CanonicalJson {
    # A tiny deterministic writer. The built in ConvertTo-Json reorders nothing
    # but it does insert whitespace and escapes that would change the signed
    # bytes between PowerShell versions.
    param($Value)

    if ($null -eq $Value) { return "null" }
    if ($Value -is [bool]) { return $(if ($Value) { "true" } else { "false" }) }
    if ($Value -is [int] -or $Value -is [long]) { return $Value.ToString([cultureinfo]::InvariantCulture) }
    if ($Value -is [string]) {
        $builder = New-Object System.Text.StringBuilder
        [void]$builder.Append('"')
        foreach ($character in $Value.ToCharArray()) {
            switch ($character) {
                '"'  { [void]$builder.Append('\"'); continue }
                '\'  { [void]$builder.Append('\\'); continue }
                "`n" { [void]$builder.Append('\n'); continue }
                "`r" { [void]$builder.Append('\r'); continue }
                "`t" { [void]$builder.Append('\t'); continue }
                default {
                    if ([int]$character -lt 0x20) {
                        [void]$builder.AppendFormat('\u{0:x4}', [int]$character)
                    } else {
                        [void]$builder.Append($character)
                    }
                }
            }
        }
        [void]$builder.Append('"')
        return $builder.ToString()
    }
    if ($Value -is [System.Collections.IList]) {
        $items = foreach ($item in $Value) { ConvertTo-CanonicalJson $item }
        return "[" + ($items -join ",") + "]"
    }
    if ($Value -is [System.Collections.Specialized.OrderedDictionary]) {
        $items = foreach ($key in $Value.Keys) {
            (ConvertTo-CanonicalJson ([string]$key)) + ":" + (ConvertTo-CanonicalJson $Value[$key])
        }
        return "{" + ($items -join ",") + "}"
    }
    throw "The canonical writer does not support $($Value.GetType().FullName)."
}

function Test-IsEphemeralCngHandleException {
    param([System.Exception]$Exception)

    # .NET Framework wraps the RSACng constructor's exact ArgumentException in
    # MethodInvocationException. Match the stable exception contract rather
    # than treating an arbitrary private-key failure as permission to fall back.
    $candidate = $Exception
    while ($null -ne $candidate) {
        if ($candidate -is [System.ArgumentException] -and
            $candidate.HResult -eq -2147024809 -and
            $candidate.ParamName -eq "keyHandleOpenOptions") {
            return $true
        }
        $candidate = $candidate.InnerException
    }
    return $false
}

function Get-CertificateCngBinding {
    param([System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate)

    if (-not ("PhpVpn.CngCertificateBindingReader" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;

namespace PhpVpn
{
    public sealed class CngCertificateBinding
    {
        public string ContainerName { get; private set; }
        public string ProviderName { get; private set; }
        public bool MachineKey { get; private set; }
        public bool Silent { get; private set; }

        internal CngCertificateBinding(string containerName, string providerName,
                                       bool machineKey, bool silent)
        {
            ContainerName = containerName;
            ProviderName = providerName;
            MachineKey = machineKey;
            Silent = silent;
        }
    }

    public static class CngCertificateBindingReader
    {
        private const uint CertKeyProvInfoProperty = 2;
        private const uint CryptMachineKeyset = 0x20;
        private const uint CryptSilent = 0x40;
        private const uint CertNCryptKeySpec = 0xffffffff;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct CryptKeyProvInfo
        {
            public IntPtr ContainerName;
            public IntPtr ProviderName;
            public uint ProviderType;
            public uint Flags;
            public uint ProviderParameterCount;
            public IntPtr ProviderParameters;
            public uint KeySpec;
        }

        [DllImport("crypt32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CertGetCertificateContextProperty(
            IntPtr certificateContext, uint propertyId, IntPtr data, ref uint dataSize);

        public static CngCertificateBinding Read(X509Certificate2 certificate)
        {
            if (certificate == null)
                throw new ArgumentNullException("certificate");

            uint dataSize = 0;
            if (!CertGetCertificateContextProperty(certificate.Handle,
                                                    CertKeyProvInfoProperty,
                                                    IntPtr.Zero,
                                                    ref dataSize))
                throw new Win32Exception(Marshal.GetLastWin32Error());

            if (dataSize < Marshal.SizeOf(typeof(CryptKeyProvInfo)))
                throw new InvalidOperationException("The certificate private-key binding is incomplete.");

            IntPtr data = Marshal.AllocHGlobal(checked((int)dataSize));
            try
            {
                if (!CertGetCertificateContextProperty(certificate.Handle,
                                                        CertKeyProvInfoProperty,
                                                        data,
                                                        ref dataSize))
                    throw new Win32Exception(Marshal.GetLastWin32Error());

                CryptKeyProvInfo info = (CryptKeyProvInfo)Marshal.PtrToStructure(
                    data, typeof(CryptKeyProvInfo));
                // ProviderType zero identifies a CNG KSP. Windows may persist
                // either the documented NCrypt marker or zero in KeySpec for a
                // certificate whose key is already identified by that KSP.
                if (info.ProviderType != 0 ||
                    (info.KeySpec != 0 && info.KeySpec != CertNCryptKeySpec))
                    throw new NotSupportedException("The certificate private key is not bound to a CNG key storage provider.");
                if (info.ContainerName == IntPtr.Zero || info.ProviderName == IntPtr.Zero)
                    throw new InvalidOperationException("The certificate CNG private-key binding is incomplete.");

                string containerName = Marshal.PtrToStringUni(info.ContainerName);
                string providerName = Marshal.PtrToStringUni(info.ProviderName);
                if (String.IsNullOrWhiteSpace(containerName) || String.IsNullOrWhiteSpace(providerName))
                    throw new InvalidOperationException("The certificate CNG private-key binding is incomplete.");

                return new CngCertificateBinding(
                    containerName,
                    providerName,
                    (info.Flags & CryptMachineKeyset) != 0,
                    (info.Flags & CryptSilent) != 0);
            }
            finally
            {
                Marshal.FreeHGlobal(data);
            }
        }
    }
}
"@
    }

    return [PhpVpn.CngCertificateBindingReader]::Read($Certificate)
}

function Test-ByteArrayEqual {
    param([byte[]]$Left, [byte[]]$Right)

    if ($null -eq $Left -or $null -eq $Right -or $Left.Length -ne $Right.Length) {
        return $false
    }
    $difference = 0
    for ($index = 0; $index -lt $Left.Length; ++$index) {
        $difference = $difference -bor ($Left[$index] -bxor $Right[$index])
    }
    return $difference -eq 0
}

function Write-SignedReleaseManifest {
    # Writes release-manifest.json in its canonical form, then the detached
    # RSA-PKCS1-SHA256 signature release-manifest.sig and the public
    # certificate release-signing.cer, signed with the pinned certificate from
    # Cert:\CurrentUser\My. Both public artifacts are published together or
    # not at all. Returns the signing subject and whether the Windows
    # PowerShell CNG compatibility path opened the private key.
    param(
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [Parameter(Mandatory=$true)][System.Collections.Specialized.OrderedDictionary]$Manifest,
        [Parameter(Mandatory=$true)][string]$CertificateThumbprint
    )

    $canonical = ConvertTo-CanonicalJson $Manifest
    $manifestBytes = [System.Text.Encoding]::UTF8.GetBytes($canonical)
    [System.IO.File]::WriteAllBytes((Join-Path $OutputDirectory "release-manifest.json"), $manifestBytes)

    $signaturePath = Join-Path $OutputDirectory "release-manifest.sig"
    $certificatePath = Join-Path $OutputDirectory "release-signing.cer"
    Remove-Item -LiteralPath $signaturePath, $certificatePath -Force -ErrorAction SilentlyContinue

    $certificate = Get-Item "Cert:\CurrentUser\My\$CertificateThumbprint" -ErrorAction SilentlyContinue
    if ($null -eq $certificate) {
        throw "The signing certificate $CertificateThumbprint is not present in Cert:\CurrentUser\My."
    }
    try {
        if (-not $certificate.HasPrivateKey) {
            throw "The signing certificate $CertificateThumbprint has no usable private key."
        }

        $publicKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
        if ($null -eq $publicKey) {
            throw "Only an RSA signing certificate is supported."
        }
        $privateKey = $null
        $cngKey = $null
        $usedCngCompatibilityPath = $false
        try {
            try {
                $privateKey = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
            } catch {
                if (-not (Test-IsEphemeralCngHandleException $_.Exception)) {
                    throw "The signing certificate private key could not be opened through the standard RSA provider path."
                }

                try {
                    $binding = Get-CertificateCngBinding $certificate
                } catch {
                    throw "The signing certificate's CNG provider binding could not be obtained safely."
                }

                $openOptions = [System.Security.Cryptography.CngKeyOpenOptions]::None
                if ($binding.MachineKey) {
                    $openOptions = $openOptions -bor [System.Security.Cryptography.CngKeyOpenOptions]::MachineKey
                }
                if ($binding.Silent) {
                    $openOptions = $openOptions -bor [System.Security.Cryptography.CngKeyOpenOptions]::Silent
                }

                try {
                    $provider = New-Object System.Security.Cryptography.CngProvider($binding.ProviderName)
                    $cngKey = [System.Security.Cryptography.CngKey]::Open($binding.ContainerName,
                                                                         $provider,
                                                                         $openOptions)
                    if ($cngKey.AlgorithmGroup -ne [System.Security.Cryptography.CngAlgorithmGroup]::Rsa) {
                        throw "not-rsa"
                    }
                    $privateKey = New-Object System.Security.Cryptography.RSACng($cngKey)
                } catch {
                    throw "The certificate-bound CNG private key could not be opened as RSA."
                }
                $usedCngCompatibilityPath = $true
            }

            if ($null -eq $privateKey) {
                throw "The signing certificate has no usable RSA private key."
            }

            try {
                $certificateParameters = $publicKey.ExportParameters($false)
                $privateKeyParameters = $privateKey.ExportParameters($false)
            } catch {
                throw "The opened private key could not be verified against the signing certificate's public key."
            }
            if (-not (Test-ByteArrayEqual $certificateParameters.Modulus $privateKeyParameters.Modulus) -or
                -not (Test-ByteArrayEqual $certificateParameters.Exponent $privateKeyParameters.Exponent)) {
                throw "The opened private key does not match the signing certificate's public key."
            }

            try {
                $signature = $privateKey.SignData($manifestBytes,
                                                  [System.Security.Cryptography.HashAlgorithmName]::SHA256,
                                                  [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
            } catch {
                throw "Signing the release manifest failed."
            }

            $detached = [ordered]@{
                algorithm   = "RSA-PKCS1-SHA256"
                thumbprint  = $certificate.Thumbprint.ToUpperInvariant()
                subject     = $certificate.Subject
                signature   = [Convert]::ToBase64String($signature)
            }
            $detachedBytes = [System.Text.Encoding]::UTF8.GetBytes((ConvertTo-CanonicalJson $detached))

            # Prepare both public artifacts completely before publishing either one.
            try {
                $certificateBytes = $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert)
            } catch {
                throw "The public signing certificate could not be exported."
            }
            $signatureTemporaryPath = Join-Path $OutputDirectory ([System.IO.Path]::GetRandomFileName())
            $certificateTemporaryPath = Join-Path $OutputDirectory ([System.IO.Path]::GetRandomFileName())
            try {
                [System.IO.File]::WriteAllBytes($signatureTemporaryPath, $detachedBytes)
                [System.IO.File]::WriteAllBytes($certificateTemporaryPath, $certificateBytes)
                Move-Item -LiteralPath $signatureTemporaryPath -Destination $signaturePath
                Move-Item -LiteralPath $certificateTemporaryPath -Destination $certificatePath
            } catch {
                Remove-Item -LiteralPath $signaturePath, $certificatePath -Force -ErrorAction SilentlyContinue
                throw "The release signature artifacts could not be published safely."
            } finally {
                Remove-Item -LiteralPath $signatureTemporaryPath, $certificateTemporaryPath -Force -ErrorAction SilentlyContinue
            }
        } catch {
            Remove-Item -LiteralPath $signaturePath, $certificatePath -Force -ErrorAction SilentlyContinue
            throw
        } finally {
            if ($null -ne $privateKey) {
                $privateKey.Dispose()
            }
            if ($null -ne $cngKey) {
                $cngKey.Dispose()
            }
            $publicKey.Dispose()
        }

        $signingSubject = $certificate.Subject
    } finally {
        $certificate.Dispose()
    }

    return [pscustomobject]@{
        Subject                  = $signingSubject
        UsedCngCompatibilityPath = $usedCngCompatibilityPath
        ManifestBytes            = $manifestBytes
    }
}

function Get-DerElement([byte[]]$Data, [int]$Offset) {
    if ($Offset + 2 -gt $Data.Length) { throw 'Truncated DER element.' }
    $length = [int]$Data[$Offset + 1]
    $content = $Offset + 2
    if ($length -ge 0x80) {
        $count = $length -band 0x7F
        if ($count -lt 1 -or $count -gt 3) { throw 'Unsupported DER length.' }
        $length = 0
        for ($index = 0; $index -lt $count; ++$index) { $length = ($length -shl 8) -bor $Data[$content]; ++$content }
    }
    if ($content + $length -gt $Data.Length) { throw 'Truncated DER element.' }
    return [pscustomobject]@{ Tag = $Data[$Offset]; Offset = $Offset; Content = $content; End = $content + $length }
}
function Get-SubjectPublicKeyInfoSha256([byte[]]$Certificate) {
    $tbs = Get-DerElement $Certificate (Get-DerElement $Certificate 0).Content
    $field = Get-DerElement $Certificate $tbs.Content
    if ($field.Tag -eq 0xA0) { $field = Get-DerElement $Certificate $field.End } # explicit version
    # serialNumber, signature, issuer, validity and subject precede subjectPublicKeyInfo.
    foreach ($tag in @(0x02, 0x30, 0x30, 0x30, 0x30)) {
        if ($field.Tag -ne $tag) { throw 'Unexpected certificate structure.' }
        $field = Get-DerElement $Certificate $field.End
    }
    if ($field.Tag -ne 0x30) { throw 'Unexpected certificate structure.' }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash($Certificate, $field.Offset, $field.End - $field.Offset) } finally { $sha.Dispose() }
    return (-join ($digest | ForEach-Object { $_.ToString('x2') }))
}
