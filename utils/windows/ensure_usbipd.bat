@echo off
setlocal

where usbipd >nul 2>&1
if not errorlevel 1 goto :ok

echo [INFO] usbipd.exe not found.

set /p INSTALL_CHOICE=Install USBIPD for Windows now? This requires administrator privileges. [Y/N]: 
if /I not "%INSTALL_CHOICE%"=="Y" (
  echo [ERROR] USBIPD is required. Install it from https://aka.ms/usbipd and rerun this script.
  exit /b 1
)

echo [INFO] Installing USBIPD for Windows...

powershell -NoProfile -Command ^
 "Start-Process winget -Verb RunAs -ArgumentList 'install','--id','dorssel.usbipd-win','-e','--accept-source-agreements','--accept-package-agreements' -Wait"

if errorlevel 1 (
  echo [ERROR] Failed to start USBIPD installation.
  exit /b 1
)

echo [INFO] Waiting for installation to complete...
timeout /t 5 >nul

where usbipd >nul 2>&1
if errorlevel 1 (
  echo [ERROR] usbipd installation did not complete successfully.
  echo You may need to reboot, then rerun this script.
  exit /b 1
)

:ok
echo [INFO] usbipd is installed.
endlocal
exit /b 0
