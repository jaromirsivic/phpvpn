@echo off
setlocal DisableDelayedExpansion
if not exist "%~dp0.venv\Scripts\python.exe" (
  echo ERROR: Environment is missing. Run init.bat first. 1>&2
  exit /b 1
)
"%~dp0.venv\Scripts\python.exe" -I -B "%~dp0server-control.py" stop
exit /b %errorlevel%
