# Isolated installer lifecycle regression. No real service/process commands run.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.ServiceProcess
$installer = Join-Path $PSScriptRoot '../tools/package/install.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($installer, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Installer parse failed' }
$requestFunction = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Request-TrayExit' }, $true)
Invoke-Expression $requestFunction.Extent.Text
# Compile and load the real native bridge without addressing any live process.
if (Request-TrayExit -ProcessId -1) { throw 'Impossible PID matched a tray window' }
$stopFunction = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Stop-InstalledClients' }, $true)
Invoke-Expression $stopFunction.Extent.Text
$script:events = @()
$script:trays = @()
$script:service = $null
$script:exitAllowed = $true
$script:stopFails = $false
function Get-Process { param($Name, $ErrorAction) return $script:trays }
function Get-Service { param($Name, $ErrorAction) return $script:service }
function Stop-Service {
    param($Name, $ErrorAction)
    $script:events += 'stop-service'
    if ($script:stopFails) { throw 'Synthetic stop failure' }
}
function Request-TrayExit { param($ProcessId) $script:events += "exit-$ProcessId"; return $script:exitAllowed }
function New-Tray {
    param([string]$Path, [bool]$Exits = $true)
    $tray = [pscustomobject]@{ Path = $Path; Id = 123; HasExited = $false; Exits = $Exits }
    $tray | Add-Member ScriptMethod WaitForExit { param($Timeout) $script:events += 'wait-tray'; return $this.Exits }
    return $tray
}
function Assert-Events { param([string]$Expected) if (($script:events -join ',') -ne $Expected) { throw "Wrong lifecycle: $($script:events -join ',')" } }
function Assert-Rejected { $rejected = $false; try { Stop-InstalledClients 'C:\test\phpvpn' } catch { $rejected = $true }; if (-not $rejected) { throw 'Expected rejection' } }

Stop-InstalledClients 'C:\test\phpvpn'
Assert-Events ''
$script:service = [pscustomobject]@{}
$script:service | Add-Member ScriptMethod WaitForStatus { param($Status, $Timeout) $script:events += 'wait-service' }
$script:trays = @(New-Tray 'C:\test\phpvpn\1.0.0\phpvpn-tray.exe')
Stop-InstalledClients 'C:\test\phpvpn'
Assert-Events 'exit-123,wait-tray,stop-service,wait-service'
$script:events = @()
$script:trays = @(New-Tray 'C:\test\phpvpn-other\phpvpn-tray.exe')
Stop-InstalledClients 'C:\test\phpvpn'
Assert-Events 'stop-service,wait-service'
$script:events = @()
$script:trays = @(New-Tray 'C:\test\phpvpn\1.0.0\phpvpn-tray.exe' $false)
Assert-Rejected
Assert-Events 'exit-123,wait-tray'
$script:events = @()
$script:exitAllowed = $false
Assert-Rejected
Assert-Events 'exit-123'
$script:events = @()
$script:trays = @(New-Tray '')
Assert-Rejected
Assert-Events ''
$script:events = @()
$script:trays = @()
$script:stopFails = $true
Assert-Rejected
Assert-Events 'stop-service'

# Ordering assertion covers the actual script, not a simulated copy routine.
$commands = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] }, $true))
$stop = @($commands | Where-Object { $_.GetCommandName() -eq 'Stop-InstalledClients' })
$copies = @($commands | Where-Object { $_.GetCommandName() -in @('Copy-Item', 'Move-Item') })
if ($stop.Count -ne 1 -or @($copies | Where-Object { $_.Extent.StartOffset -lt $stop[0].Extent.StartOffset }).Count) { throw 'Files/pointer can be overwritten before stop' }
$verifyOnly = $ast.Find({ param($node) $node -is [Management.Automation.Language.IfStatementAst] -and $node.Clauses[0].Item1.Extent.Text -eq '$VerifyOnly' }, $true)
if (-not $verifyOnly -or $verifyOnly.Extent.EndOffset -gt $stop[0].Extent.StartOffset -or $verifyOnly.Extent.Text -notmatch 'exit 0') { throw 'VerifyOnly must exit before lifecycle mutations' }
if (@($commands | Where-Object { $_.GetCommandName() -eq 'Stop-Process' }).Count) { throw 'Installer must not force-discard tray drafts' }
Write-Output 'PASS: fresh install, ordered upgrade, unrelated tray, cancelled/timeout/inaccessible tray, stop failure, VerifyOnly and copy/pointer ordering.'
