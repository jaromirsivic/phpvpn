<# .SYNOPSIS
Raises the last number of the PHP VPN version (X.Y.Z -> X.Y.Z+1) before a new distribution.
Rewrites every location of Get-VersionLocations: the client CMake project, the RPM spec,
the manual pages, the version header and SERVER_VERSION of the PHP, Python and Node.js
servers, the VPN Server WordPress plugin (header, Stable tag, generated server core), and prepends a release entry to the RPM and Debian changelogs. All locations must
hold the same current version, otherwise nothing is written. Run it as the first step of a
distribution, before the Linux package build, and commit the result (or pass -Commit).
#>
[CmdletBinding()]
param(
    # Commit exactly the rewritten files as "Version X.Y.Z".
    [switch]$Commit
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'distribution-common.ps1')
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$current = Get-ClientVersion $repository
$problems = Get-VersionLocationProblems $repository $current
if ($problems.Count) { throw ("Version locations are not all at $current, nothing was changed:`n" + ($problems -join "`n")) }
$parts = $current.Split('.')
$next = "$($parts[0]).$($parts[1]).$([int]$parts[2] + 1)"
$now = [DateTime]::UtcNow
$invariant = [Globalization.CultureInfo]::InvariantCulture

$texts = [ordered]@{}
function Get-Text([string]$Path) {
    if (-not $texts.Contains($Path)) { $texts[$Path] = [IO.File]::ReadAllText((Join-Path $repository $Path)) }
    return $texts[$Path]
}
function Get-NewLine([string]$Text) { if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" } }

# Changelog entries first: their top entry is itself a version location.
$spec = 'client/packaging/linux/rpm/phpvpn.spec'
$text = Get-Text $spec
$nl = Get-NewLine $text
$maintainer = [regex]::Match($text, '(?m)^%changelog\r?\n\* \S+ \S+ \d+ \d+ (?<m>.+) - ').Groups['m'].Value
if (-not $maintainer) { throw "The $spec changelog has no previous entry to take the packager from." }
$entry = "* $($now.ToString('ddd MMM dd yyyy', $invariant)) $maintainer - $next-1$nl- Release $next.$nl$nl"
$texts[$spec] = [regex]::Replace($text, '(?m)^%changelog\r?\n', { param($m) $m.Value + $entry })

$debian = 'client/packaging/linux/debian/changelog'
$text = Get-Text $debian
$nl = Get-NewLine $text
$maintainer = [regex]::Match($text, '(?m)^ -- (?<m>.+?)  ').Groups['m'].Value
if (-not $maintainer) { throw "The $debian has no previous entry to take the maintainer from." }
$texts[$debian] = "phpvpn ($next-1) unstable; urgency=medium$nl$nl  * Release $next.$nl$nl" +
    " -- $maintainer  $($now.ToString('ddd, dd MMM yyyy HH:mm:ss', $invariant)) +0000$nl$nl" + $text

foreach ($location in Get-VersionLocations | Where-Object { $_.Replace }) {
    $text = Get-Text $location.Path
    $match = [regex]::Match($text, $location.Pattern, 'Multiline')
    $groups = @($match.Groups['v'])
    if ($match.Groups['d'].Success) { $groups += $match.Groups['d'] }
    # Replace from the end so the earlier group index stays valid.
    foreach ($group in $groups | Sort-Object Index -Descending) {
        $value = if ($group.Name -eq 'd') { $now.ToString('yyyy-MM-dd', $invariant) } else { $next }
        $text = $text.Substring(0, $group.Index) + $value + $text.Substring($group.Index + $group.Length)
    }
    $texts[$location.Path] = $text
}

$encoding = [Text.UTF8Encoding]::new($false)
foreach ($path in $texts.Keys) { [IO.File]::WriteAllText((Join-Path $repository $path), $texts[$path], $encoding) }
$problems = Get-VersionLocationProblems $repository $next
if ($problems.Count) { throw ("Version $next was written incompletely:`n" + ($problems -join "`n")) }
Write-Output "PHP VPN version $current -> $next ($($texts.Count) files)."
if ($Commit) {
    & git -C $repository commit -m "Version $next" -- @($texts.Keys)
    if ($LASTEXITCODE -ne 0) { throw 'The version commit failed.' }
} else {
    Write-Output 'Commit the change before the Linux package build (or rerun with -Commit next time).'
}
