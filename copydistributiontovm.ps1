$ErrorActionPreference = "Stop"

# The Hyper-V virtual machine and the target folder in its guest come from the
# hyperV section of the global configuration phpvpn.config.json.
. (Join-Path $PSScriptRoot "tools\phpvpn-config.ps1")
$vmName = [string](Get-PhpVpnConfigValue "hyperV.vmName")
$destinationDirectory = [string](Get-PhpVpnConfigValue "hyperV.destinationDirectory")
if ([string]::IsNullOrWhiteSpace($vmName) -or [string]::IsNullOrWhiteSpace($destinationDirectory)) {
  throw "Set hyperV.vmName and hyperV.destinationDirectory in $(Get-PhpVpnConfigFile)."
}
$sourceArchive = Join-Path $PSScriptRoot "dist.zip"
$sourceDirectory = Join-Path $PSScriptRoot "dist"
$destinationDistDirectory = Join-Path $destinationDirectory "dist"

if (-not (Test-Path -LiteralPath $sourceArchive -PathType Leaf)) {
  throw "The distribution archive does not exist: $sourceArchive"
}

if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
  throw "The distribution directory does not exist: $sourceDirectory"
}

Copy-VMFile `
  -VMName $vmName `
  -SourcePath $sourceArchive `
  -DestinationPath (Join-Path $destinationDirectory "dist.zip") `
  -FileSource Host `
  -CreateFullPath `
  -Force

$sourceDirectory = (Resolve-Path -LiteralPath $sourceDirectory).Path.TrimEnd("\")
Get-ChildItem -LiteralPath $sourceDirectory -File -Recurse | ForEach-Object {
  $relativePath = $_.FullName.Substring($sourceDirectory.Length).TrimStart("\")
  Copy-VMFile `
    -VMName $vmName `
    -SourcePath $_.FullName `
    -DestinationPath (Join-Path $destinationDistDirectory $relativePath) `
    -FileSource Host `
    -CreateFullPath `
    -Force
}
