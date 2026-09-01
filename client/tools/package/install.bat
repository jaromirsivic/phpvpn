@echo off
rem Supported entry point of the phpvpn package, plan_client.md chapter 20.2.
rem It only forwards to the PowerShell helper next to it and asks for elevation,
rem so the verification order stays in one place.

setlocal

set "PACKAGE=%~dp0"
if not exist "%PACKAGE%install.ps1" (
    echo install.ps1 is missing from the package.
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PACKAGE%install.ps1" %*
exit /b %ERRORLEVEL%
