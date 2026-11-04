@echo off
setlocal
cd /d "%~dp0"
set "XSCT=D:\Xilinx\SDK\2019.1\bin\xsct.bat"
if not exist "%XSCT%" (
  echo ERROR: Xilinx SDK 2019.1 not found.
  pause
  exit /b 1
)
echo Downloading AX7020 firmware. Keep the camera area empty and still.
call "%XSCT%" "%~dp0run_uart_test.tcl"
if errorlevel 1 (
  echo ERROR: Download failed. See messages above.
  pause
  exit /b 1
)
echo Download complete. UART baud rate: 115200.
pause
