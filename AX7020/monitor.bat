@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
python uart_gesture_monitor.py %*
pause