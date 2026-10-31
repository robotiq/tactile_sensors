@echo off
setlocal

rem === Target Robotiq TSF VID:PID pairs ===
rem Newer production units use 16D0:14CC, older units use Cypress default 04B4:F232
set "TARGET_VIDPID1=16D0:14CC"
set "TARGET_VIDPID2=04B4:F232"

set "USB_BUSID="
set "USB_DESC="

rem === Enumerate USB devices (robust against warnings) ===
for /f "tokens=1,2,*" %%A in ('usbipd list') do (
  if /I "%%B"=="%TARGET_VIDPID1%" (
    set "USB_BUSID=%%A"
    set "USB_DESC=%%C"
    goto :usb_found
  )
  if /I "%%B"=="%TARGET_VIDPID2%" (
    set "USB_BUSID=%%A"
    set "USB_DESC=%%C"
    goto :usb_found
  )
)

echo [ERROR] Could not find a connected Robotiq TSF device (VID:PID %TARGET_VIDPID1% or %TARGET_VIDPID2%).
echo Connect the sensor, ensure Windows detects it, then rerun this script.
echo.
usbipd list
exit /b 1

:usb_found
echo [INFO] Found USB device %USB_BUSID% (%USB_DESC%)
echo [INFO] Attaching USB device to WSL...

rem --- Bind the device first with --force ---
powershell -NoProfile -Command ^
    "Start-Process usbipd -Verb RunAs -ArgumentList 'bind','--busid','%USB_BUSID%','--force' -Wait" >nul 2>&1

if errorlevel 1 (
    echo [ERROR] Failed to bind USB device %USB_BUSID%.
    echo Run this script as Administrator or check USBIPD installation.
    exit /b 1
)

rem --- Attach the device to WSL ---
powershell -NoProfile -Command ^
    "Start-Process usbipd -Verb RunAs -ArgumentList 'attach','--wsl','--busid','%USB_BUSID%' -Wait" >nul 2>&1

if errorlevel 1 (
    echo [ERROR] Failed to attach USB device %USB_BUSID% to WSL.
    echo Run this script as Administrator. Check your organization's USB policies if USB redirection is blocked.
    exit /b 1
)

echo [INFO] USB device successfully attached to WSL.

endlocal
exit /b 0
