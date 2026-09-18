@echo off
setlocal

pushd "%~dp0" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Cannot access the project directory.
    exit /b 1
)

if not exist "dist\" (
    echo ERROR: The distribution directory "%~dp0dist" does not exist.
    popd
    exit /b 1
)

if exist "dist.zip" (
    del /f /q "dist.zip" >nul 2>&1
    if exist "dist.zip" (
        echo ERROR: Cannot remove the existing archive "%~dp0dist.zip".
        popd
        exit /b 1
    )
)

set "PHPVPN_PROJECT_ROOT=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "$source = Join-Path $env:PHPVPN_PROJECT_ROOT 'dist'; $archive = Join-Path $env:PHPVPN_PROJECT_ROOT 'dist.zip'; try { Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop; [System.IO.Compression.ZipFile]::CreateFromDirectory($source, $archive, [System.IO.Compression.CompressionLevel]::Optimal, $false); if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw 'The archive was not created.' } } catch { Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue; Write-Error ('Failed to create dist.zip: ' + $_.Exception.Message); exit 1 }"
if errorlevel 1 (
    echo ERROR: Distribution compression failed. The VM transfer was not started.
    popd
    exit /b 1
)

if not exist "copydistributiontovm.ps1" (
    echo ERROR: The transfer script "%~dp0copydistributiontovm.ps1" does not exist.
    popd
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -Command ^
    "$script = Join-Path $env:PHPVPN_PROJECT_ROOT 'copydistributiontovm.ps1'; try { $arguments = '-NoProfile -ExecutionPolicy Bypass -File ' + [char]34 + $script + [char]34; $process = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -Wait -PassThru -ErrorAction Stop; exit $process.ExitCode } catch { Write-Error ('Failed to start the elevated VM transfer: ' + $_.Exception.Message); exit 1 }"
set "TRANSFER_EXIT_CODE=%ERRORLEVEL%"

popd
if not "%TRANSFER_EXIT_CODE%"=="0" (
    echo ERROR: The VM transfer failed or was cancelled.
    exit /b %TRANSFER_EXIT_CODE%
)

echo Distribution archive created and transferred successfully.
exit /b 0
