# Reader of the global development configuration phpvpn.config.json, dot-sourced
# by the build, distribution and test scripts. PHPVPN_CONFIG selects another file.
# A relative path resolves against the repository root. A value may be an object
# with "windows" and "linux" members. An empty tool path means a PATH lookup.
# Distributed artefacts (installers, server scripts) never read this file.
$PhpVpnRepositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Get-PhpVpnConfigFile {
    if ($env:PHPVPN_CONFIG) { return [IO.Path]::GetFullPath($env:PHPVPN_CONFIG) }
    return Join-Path $PhpVpnRepositoryRoot 'phpvpn.config.json'
}

function Get-PhpVpnConfig {
    $file = Get-PhpVpnConfigFile
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Global configuration not found: $file (set PHPVPN_CONFIG or create it)."
    }
    return [IO.File]::ReadAllText($file) | ConvertFrom-Json
}

function Get-PhpVpnConfigValue {
    param([Parameter(Mandatory=$true)][string]$Key, $Default = $null)
    $value = Get-PhpVpnConfig
    foreach ($part in $Key.Split('.')) {
        if ($null -eq $value -or -not ($value.PSObject.Properties.Name -contains $part)) { return $Default }
        $value = $value.$part
    }
    if ($value -is [Management.Automation.PSCustomObject]) {
        $names = @($value.PSObject.Properties.Name)
        if (@($names | Where-Object { $_ -notin @('windows', 'linux', 'macos') }).Count -eq 0) { $value = $value.windows }
    }
    return $value
}

function Resolve-PhpVpnConfigPath {
    param([Parameter(Mandatory=$true)][string]$Key)
    $value = [string](Get-PhpVpnConfigValue $Key)
    if ([string]::IsNullOrWhiteSpace($value)) { return $null }
    if (-not [IO.Path]::IsPathRooted($value)) { $value = Join-Path $PhpVpnRepositoryRoot $value }
    return [IO.Path]::GetFullPath($value)
}

function Resolve-PhpVpnTool {
    # The configured tools.<Name> executable, otherwise <Command> from PATH.
    param([Parameter(Mandatory=$true)][string]$Name, [string]$Command = $Name)
    $configured = Resolve-PhpVpnConfigPath "tools.$Name"
    if ($configured) {
        if (-not (Test-Path -LiteralPath $configured -PathType Leaf)) {
            throw "tools.$Name in $(Get-PhpVpnConfigFile) does not exist: $configured"
        }
        return $configured
    }
    $found = Get-Command $Command -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $found) { throw "$Command was not found. Set tools.$Name in $(Get-PhpVpnConfigFile) or add it to PATH." }
    return $found.Source
}
