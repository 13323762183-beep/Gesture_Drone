@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tune_throttle.ps1" %*
echo.
pause

