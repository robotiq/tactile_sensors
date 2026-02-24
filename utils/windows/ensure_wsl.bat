@echo off

where wsl >nul 2>&1
if errorlevel 1 goto :not_installed
goto :check_running

:not_installed
echo [INFO] Windows Subsystem for Linux (WSL) is required.
set /p INSTALL_CHOICE=Install WSL now? This will open an elevated prompt and may require a reboot. [Y/N]: 

if /I not "%INSTALL_CHOICE%"=="Y" (
  echo [ERROR] WSL is not installed. Install it (https://aka.ms/wslinstall) and re-run this script.
  exit /b 1
)

powershell -NoProfile -Command ^
 "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -Command wsl --install'"

if errorlevel 1 (
  echo [ERROR] Failed to start WSL installation. Please run 'wsl --install' manually from an elevated PowerShell.
  exit /b 1
)

echo [INFO] WSL installation initiated.
echo Follow the prompts, reboot if needed, then rerun this script.
exit /b 1

:check_running
wsl -l -q >nul 2>&1
if errorlevel 1 (
  echo [INFO] WSL appears installed but not ready yet.
  echo [INFO] Please reboot Windows or launch "wsl" once manually, then rerun this script.
  exit /b 1
)

goto :ensure_ubuntu

:ensure_ubuntu
@REM Robust Check: Attempt to run a harmless command ('true') inside Ubuntu-20.04.
@REM If the distro exists, this returns 0. If missing, it returns an error.
echo [INFO] Verifying Ubuntu-20.04...
set "DISTRO_NAME=Ubuntu-20.04"

:: Check the Windows Registry for the distribution name
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss" /s /f "%DISTRO_NAME%" >nul 2>&1

if %errorlevel% equ 0 (
    echo [INFO] %DISTRO_NAME% is installed and ready.
    goto :set_default_ubuntu
) else (
    echo [ERROR] %DISTRO_NAME% was not found.
    echo Please verify the name or check if you are running as the correct user.
)

:install_ubuntu
echo [INFO] Ubuntu 20.04 check failed (it might be missing or stopped).
set /p INSTALL_UBUNTU=Install Ubuntu 20.04 now? This will open an elevated prompt. [Y/N]:

if /I not "%INSTALL_UBUNTU%"=="Y" (
  echo [ERROR] Ubuntu 20.04 is required. Install it and re-run this script.
  exit /b 1
)

powershell -NoProfile -Command ^
 "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -Command wsl --install -d Ubuntu-20.04'"

if errorlevel 1 (
  echo [ERROR] Failed to start Ubuntu 20.04 installation. Please run 'wsl --install -d Ubuntu-20.04' manually from an elevated PowerShell.
  exit /b 1
)

echo [INFO] Ubuntu 20.04 installation initiated.
echo Follow the prompts, reboot if needed, then rerun this script.
exit /b 1

:set_default_ubuntu
@REM Set default to Ubuntu-20.04.
wsl --set-default Ubuntu-20.04 >nul 2>&1

if errorlevel 1 (
  echo [WARN] Could not automatically set Ubuntu-20.04 as default.
  echo [WARN] Ensure it is installed correctly or run 'wsl --set-default Ubuntu-20.04' manually.
) else (
  echo [INFO] Ubuntu 20.04 is set as the default WSL distro.
)

:ubuntu_ready
exit /b 0