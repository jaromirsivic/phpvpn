# Verify documentation policy against a clean, unpacked complete distribution
# (not a live dist tree containing preserved .venv or .runtime state).
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$DistributionDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'distribution-common.ps1')
$repository = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$work = Join-Path $repository 'work'
$fixture = Assert-DistributionPath (Join-Path $work ('documentation-test-' + [guid]::NewGuid().ToString('N'))) $work
$verify = Join-Path $PSScriptRoot 'verify-distribution.ps1'
function Expect-Rejected([string]$Message) {
    try { & $verify -DistributionDirectory $fixture | Out-Null }
    catch {
        if ($_.Exception.Message -notlike "*$Message*") { throw }
        Write-Output "PASS: rejected $Message"
        return
    }
    throw "Unexpected acceptance: $Message"
}
try {
    Copy-Item -LiteralPath (Resolve-Path $DistributionDirectory).Path -Destination $fixture -Recurse
    $readme = Join-Path $fixture 'documentation/readme.txt'
    $protocol = Join-Path $fixture 'documentation/protocol.md'
    & $verify -DistributionDirectory $fixture | Out-Null
    Write-Output 'PASS: documentation with retained protocol'
    if (Test-Path -LiteralPath $protocol) { Remove-Item -LiteralPath $protocol }
    & $verify -DistributionDirectory $fixture | Out-Null
    Write-Output 'PASS: protocol omitted while retained source exists'
    [IO.File]::WriteAllText($protocol, 'not the retained translation')
    Expect-Rejected 'protocol differs'
    Remove-Item -LiteralPath $protocol
    Remove-Item -LiteralPath $readme
    Expect-Rejected 'Missing distribution file: documentation/readme.txt'
    [IO.File]::WriteAllText($readme, 'outdated README')
    Expect-Rejected 'Stale distribution source: documentation/readme.txt'
    Copy-Item -LiteralPath (Join-Path $repository 'readme.txt') -Destination $readme -Force
    [IO.File]::WriteAllText((Join-Path $fixture 'documentation/extra.txt'), 'unlisted')
    Expect-Rejected 'missing or unexpected files'
    if ($null -ne (Get-DistributionProtocolSource $fixture)) { throw 'Missing retained source was not optional.' }
    Write-Output 'PASS: missing retained source is optional'
} finally {
    $safeFixture = Assert-DistributionPath $fixture $work -Tree
    if (Test-Path -LiteralPath $safeFixture) { Remove-Item -LiteralPath $safeFixture -Recurse -Force }
}
