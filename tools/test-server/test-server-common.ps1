# Shared state of the private nginx/PHP test server (start-test-server.ps1 and
# stop-test-server.ps1). A process counts as owned only when its PID file names
# a live process with the expected executable and a command line containing the
# expected marker, so a reused PID or an unrelated nginx/PHP is never touched.

function Get-TestServerState([string]$Directory) {
    $prefix = [IO.Path]::GetFullPath($Directory)
    $run = Join-Path $prefix 'run'
    $logs = Join-Path $prefix 'logs'
    $conf = Join-Path $prefix 'conf'
    return [pscustomobject]@{
        Prefix         = $prefix
        PrefixArgument = $prefix.Replace('\', '/').TrimEnd('/') + '/'
        Conf           = $conf
        Logs           = $logs
        Run            = $run
        NginxConf      = (Join-Path $conf 'nginx.conf').Replace('\', '/')
        NginxPid       = Join-Path $logs 'nginx.pid'
        PhpCgiPid      = Join-Path $run 'php-cgi.pid'
        BuiltinPid     = Join-Path $run 'php-builtin.pid'
    }
}

function Get-OwnedProcess([string]$PidFile, [string]$Executable, [string]$Marker) {
    if (-not (Test-Path -LiteralPath $PidFile -PathType Leaf)) { return $null }
    $text = ([IO.File]::ReadAllText($PidFile)).Trim()
    if ($text -notmatch '^\d+$') { return $null }
    $process = Get-CimInstance Win32_Process -Filter "ProcessId = $text" -ErrorAction SilentlyContinue
    if (-not $process -or -not $process.ExecutablePath -or -not $process.CommandLine) { return $null }
    if (-not $process.ExecutablePath.Equals([IO.Path]::GetFullPath($Executable), [StringComparison]::OrdinalIgnoreCase)) { return $null }
    if ($process.CommandLine.IndexOf($Marker, [StringComparison]::OrdinalIgnoreCase) -lt 0) { return $null }
    return $process
}

function Assert-ListenerFree([string]$Listen) {
    $port = [int]$Listen.Substring($Listen.LastIndexOf(':') + 1)
    $owner = Get-NetTCPConnection -State Listen -LocalPort $port -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($owner) { throw "Port $port of $Listen is already used by process $($owner.OwningProcess). Choose another port in phpvpn.config.json." }
}
