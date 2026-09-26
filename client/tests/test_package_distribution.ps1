# Real filesystem publication tests, including Windows directory/file locks.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../tools/distribution-common.ps1')
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class DistributionDirectoryLock {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    public static SafeFileHandle Open(string path) {
        var handle = CreateFile(path, 0x80000000, 3, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero);
        if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        return handle;
    }
}
'@
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('phpvpn-distribution-' + [guid]::NewGuid().ToString('N'))
function Check([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function TextAt([string]$Path) { return [IO.File]::ReadAllText($Path) }
try {
    foreach ($scenario in @('fresh', 'replace', 'root-locked', 'child-locked', 'file-locked', 'archive-locked')) {
        $repository = Join-Path $testRoot $scenario
        $session = Join-Path $repository 'work/session'
        $staging = Join-Path $session 'staging'
        $archive = Join-Path $session 'new.zip'
        $dist = Join-Path $repository 'dist'
        [IO.Directory]::CreateDirectory((Join-Path $staging 'client')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $staging 'server')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $staging 'server/Python')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $staging 'client/new.txt'), 'new client')
        [IO.File]::WriteAllText((Join-Path $staging 'server/new.txt'), 'new server')
        [IO.File]::WriteAllText($archive, 'new zip')
        if ($scenario -ne 'fresh') {
            [IO.Directory]::CreateDirectory((Join-Path $dist 'client')) | Out-Null
            [IO.Directory]::CreateDirectory((Join-Path $dist 'server/Python/.runtime')) | Out-Null
            [IO.File]::WriteAllText((Join-Path $dist 'client/old.txt'), 'old client')
            [IO.File]::WriteAllText((Join-Path $dist 'server/Python/.runtime/old.txt'), 'old runtime')
            [IO.File]::WriteAllText((Join-Path $repository 'dist.zip'), 'old zip')
        }
        $lock = $null
        if ($scenario -eq 'root-locked') { $lock = [DistributionDirectoryLock]::Open($dist) }
        if ($scenario -eq 'child-locked') { $lock = [DistributionDirectoryLock]::Open((Join-Path $dist 'server')) }
        if ($scenario -eq 'file-locked') { $lock = [IO.File]::Open((Join-Path $dist 'client/old.txt'), 'Open', 'Read', 'Read') }
        if ($scenario -eq 'archive-locked') { $lock = [IO.File]::Open($archive, 'Open', 'Read', 'Read') }
        $failure = $null
        try { Publish-Distribution -Repository $repository -Session $session -Staging $staging -Archive $archive }
        catch { $failure = $_ }
        finally { if ($lock) { $lock.Dispose() } }
        if ($scenario -in @('file-locked', 'archive-locked')) {
            Check ($null -ne $failure) "$scenario unexpectedly succeeded"
            Check ((TextAt (Join-Path $dist 'client/old.txt')) -ceq 'old client') 'Rollback lost old client'
            Check ((TextAt (Join-Path $dist 'server/Python/.runtime/old.txt')) -ceq 'old runtime') 'Rollback lost runtime'
            Check ((TextAt (Join-Path $repository 'dist.zip')) -ceq 'old zip') 'Rollback lost ZIP'
            Check (-not (Test-Path -LiteralPath (Join-Path $dist 'server/new.txt'))) 'Rollback mixed releases'
        } else {
            if ($failure) { throw $failure }
            Check ((TextAt (Join-Path $dist 'client/new.txt')) -ceq 'new client') 'New client missing'
            Check ((TextAt (Join-Path $dist 'server/new.txt')) -ceq 'new server') 'New server missing'
            Check ((TextAt (Join-Path $repository 'dist.zip')) -ceq 'new zip') 'New ZIP missing'
            if ($scenario -ne 'fresh') {
                Check ((TextAt (Join-Path $session 'previous-dist/client/old.txt')) -ceq 'old client') 'Backup lost client'
                Check ((TextAt (Join-Path $dist 'server/Python/.runtime/old.txt')) -ceq 'old runtime') 'Publication modified runtime'
                Check (-not (Test-Path -LiteralPath (Join-Path $session 'previous-dist/server/Python/.runtime'))) 'Runtime must not be moved to backup'
                Check ((TextAt (Join-Path $session 'previous-dist.zip')) -ceq 'old zip') 'Backup lost ZIP'
            }
        }
        Write-Output "PASS: distribution publication $scenario"
    }
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if ($resolved.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path $resolved -Leaf) -like 'phpvpn-distribution-*') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
