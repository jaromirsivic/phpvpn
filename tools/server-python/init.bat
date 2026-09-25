@echo off
setlocal DisableDelayedExpansion
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0bootstrap.ps1"
exit /b %errorlevel%
