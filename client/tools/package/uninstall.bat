@echo off
rem Supported entry point for removal, plan_client.md chapter 20.5.

setlocal

set "PACKAGE=%~dp0"
if not exist "%PACKAGE%uninstall.ps1" (
    echo uninstall.ps1 is missing from the package.
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PACKAGE%uninstall.ps1" %*
exit /b %ERRORLEVEL%
