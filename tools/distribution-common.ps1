# Shared, explicit distribution contract. Source paths are checkout-relative.
function Get-DistributionSources {
    $files = [ordered]@{
        'server/PHP/phpvpn.php' = 'phpvpn.php'
        'server/Python/phpvpn.py' = 'phpvpn.py'
        'server/Python/requirements-server.txt' = 'requirements-server.txt'
        'server/Python/server-python.md' = 'server-python.md'
    }
    foreach ($name in @('init.bat','start.bat','stop.bat','init.sh','start.sh','stop.sh','bootstrap.ps1','server-control.py')) {
        $files["server/Python/$name"] = "tools/server-python/$name"
    }
    return $files
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

function Get-UnsupportedClientReadme([string]$Platform) {
    return "# PHP VPN client for $Platform`n`nNo distributable client implementation is available for this platform yet.`nThis directory does not claim platform support or completed validation.`n"
}

function Test-DistributionRuntimePath([string]$RelativePath) {
    return $RelativePath.Replace('\', '/') -match '^server/Python/\.(runtime|venv)(/|$)'
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
            throw 'Distribution staging must not contain local Python runtime state.'
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
}
