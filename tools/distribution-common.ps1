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
