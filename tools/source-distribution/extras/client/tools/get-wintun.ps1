# Download the exact official Wintun release. No binary is stored in the source distribution.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$archive = Join-Path ([IO.Path]::GetTempPath()) ('phpvpn-wintun-' + [guid]::NewGuid().ToString('N') + '.zip')
try {
    Invoke-WebRequest -UseBasicParsing -Uri 'https://www.wintun.net/builds/wintun-0.14.1.zip' -OutFile $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '07C256185D6EE3652E09FA55C0B673E2624B565E02C4B9091C79CA7D2F24EF51') {
        throw 'The Wintun archive does not match the pinned official release.'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        foreach ($arch in @('x64','arm64','x86')) {
            $archiveArch = if ($arch -eq 'x64') { 'amd64' } else { $arch }
            $entry = $zip.GetEntry("wintun/bin/$archiveArch/wintun.dll")
            if ($null -eq $entry) { throw "Missing official $arch library" }
            $directory = Join-Path $root "client/third_party/wintun/$arch"
            [IO.Directory]::CreateDirectory($directory) | Out-Null
            $target = Join-Path $directory 'wintun.dll'
            $inputStream = $entry.Open()
            try {
                $outputStream = [IO.File]::Create($target)
                try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
            } finally { $inputStream.Dispose() }
            $signature = Get-AuthenticodeSignature -LiteralPath $target
            if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=WireGuard LLC') {
                throw "The $arch library does not have a valid official WireGuard signature."
            }
            Write-Output "Verified official Wintun library: $arch"
        }
    } finally { $zip.Dispose() }
} finally {
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
}