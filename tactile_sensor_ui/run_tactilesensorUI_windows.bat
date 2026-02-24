@echo off
setlocal
if "%~1"=="STAY_OPEN" goto :RunScript
cmd /k "%~f0" STAY_OPEN
exit

:RunScript
rem --- Script directory ---
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

rem --- Verify run.sh exists ---
set "RUN_SH=%SCRIPT_DIR%\run_tactilesensorUI.sh"
if not exist "%RUN_SH%" (
    echo [ERROR] Unable to find run_tactilesensorUI.sh next to this script.
    exit /b 1
)

rem --- Windows helper scripts ---
set "WINDOWS_DIR=%SCRIPT_DIR%\..\utils\windows"
if not exist "%WINDOWS_DIR%\ensure_wsl.bat" (
    echo [ERROR] Missing helper scripts under %WINDOWS_DIR%.
    exit /b 1
)
if not exist "%WINDOWS_DIR%\ensure_hypervisor.bat" (
    echo [ERROR] Missing helper scripts under %WINDOWS_DIR%.
    exit /b 1
)
if not exist "%WINDOWS_DIR%\ensure_docker_desktop.bat" (
    echo [ERROR] Missing helper scripts under %WINDOWS_DIR%.
    exit /b 1
)
if not exist "%WINDOWS_DIR%\ensure_usbipd.bat" (
    echo [ERROR] Missing helper scripts under %WINDOWS_DIR%.
    exit /b 1
)

rem --- Ensure WSL is installed ---
rem Note: the default wsl version installed with Docker Desktop is not sufficient. 
call "%WINDOWS_DIR%\ensure_wsl.bat"
if errorlevel 1 exit /b 1

rem --- Ensure Docker Desktop is installed and running ---
call "%WINDOWS_DIR%\ensure_docker_desktop.bat"
if errorlevel 1 exit /b 1

rem --- Ensure USBIPD is installed ---
call "%WINDOWS_DIR%\ensure_usbipd.bat"
if errorlevel 1 exit /b 1

rem --- Attach USB device to WSL ---
call "%WINDOWS_DIR%\attach_usb_to_wsl.bat"
if errorlevel 1 exit /b 1

rem --- Convert Windows path to WSL path for this environment ---
for /f "delims=" %%I in ('wsl wslpath -a "%SCRIPT_DIR%"') do set "WSL_SCRIPT_DIR=%%I"

rem --- Ensure dos2unix is installed in WSL ---
wsl bash -c "command -v dos2unix >/dev/null 2>&1 || sudo apt update && sudo apt install -y dos2unix"

rem --- Convert all .sh scripts to LF and make them executable ---
wsl bash -c "find '%WSL_SCRIPT_DIR%/../utils/scripts' -type f -name '*.sh' -exec dos2unix {} \; -exec chmod +x {} \;"
wsl bash -c "dos2unix '%WSL_SCRIPT_DIR%/run_tactilesensorUI.sh' && chmod +x '%WSL_SCRIPT_DIR%/run_tactilesensorUI.sh'"

rem --- Launch run_tactilesensorUI.sh inside WSL ---
echo [INFO] Launching Robotiq TSF Viewer via WSL...
wsl bash -lc "cd '%WSL_SCRIPT_DIR%' && ./run_tactilesensorUI.sh"
if errorlevel 1 (
    echo [ERROR] Failed to launch run_tactilesensorUI.sh inside WSL.
    exit /b 1
)


endlocal
exit /b 0
