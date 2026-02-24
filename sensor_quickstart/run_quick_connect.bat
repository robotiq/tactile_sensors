@echo off
setlocal EnableDelayedExpansion

echo ==========================================
echo Simple Tactile Sensor Checker - Windows
echo ==========================================
echo.

rem Keep window open on any error
if not defined _KEEP_OPEN (
    set "_KEEP_OPEN=1"
    cmd /k "%~f0" %*
    exit /b
)
set "_KEEP_OPEN="
echo.

rem Get the directory where this script is located
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "PARENT_DIR=%SCRIPT_DIR%\.."
set "VENV_DIR=%SCRIPT_DIR%\.venvSimpleCheck"

rem ==========================================
rem Step 1: Check for Python
rem ==========================================
echo [1/6] Checking for Python installation...
echo.

python --version
if errorlevel 1 (
    echo.
    echo [ERROR] Python not found!
    echo.
    echo Please install Python 3.7 or higher from:
    echo   https://www.python.org/downloads/
    echo.
    echo Make sure to check "Add Python to PATH" during installation.
    echo After installing, close this window and open a new command prompt.
    echo.
    pause
    exit /b 1
)

echo [OK] Python found

echo Checking for venv module...
python -m venv --help >nul 2>&1
if errorlevel 1 (
    echo [WARNING] Python venv module not found, installing virtualenv via pip...
    pip install virtualenv --quiet
    if errorlevel 1 (
        echo [ERROR] Failed to install virtualenv
        echo Please reinstall Python from: https://www.python.org/downloads/
        echo Make sure to check "Install pip" and do not uncheck any optional features.
        echo.
        pause
        exit /b 1
    )
    echo [OK] virtualenv installed
    set "USE_VIRTUALENV=1"
) else (
    set "USE_VIRTUALENV=0"
)
echo [OK] venv module available
echo.

rem ==========================================
rem Step 2: Release USB device from WSL (if usbipd is present)
rem ==========================================
echo [2/6] Checking for locked USB devices...

where usbipd >nul 2>&1
if not errorlevel 1 (
    rem Check if any devices are actually bound/attached to WSL before prompting for admin
    set "NEED_UNBIND=0"
    for /f "tokens=*" %%i in ('usbipd list 2^>nul') do (
        echo %%i | findstr /i "Shared Attached" >nul 2>&1
        if not errorlevel 1 set "NEED_UNBIND=1"
    )
    if "!NEED_UNBIND!"=="1" (
        echo Releasing USB devices bound to WSL...
        echo   (This is needed if the sensor was previously used with the WSL-based UI^)
        powershell -NoProfile -Command "Start-Process usbipd -Verb RunAs -ArgumentList 'unbind','--all' -Wait" >nul 2>&1
        if not errorlevel 1 (
            echo [OK] USB devices released
        ) else (
            echo [WARNING] Could not release USB devices (admin privileges may be needed^)
        )
    ) else (
        echo [OK] No USB devices bound to WSL, no action needed
    )
) else (
    echo [OK] usbipd not installed, skipping (no WSL USB redirection to undo^)
)
echo.

rem ==========================================
rem Step 3: Create/Activate Virtual Environment
rem ==========================================
echo [3/6] Setting up virtual environment...

if not exist "%VENV_DIR%" (
    echo Creating virtual environment...
    if "%USE_VIRTUALENV%"=="1" (
        python -m virtualenv "%VENV_DIR%"
    ) else (
        python -m venv "%VENV_DIR%"
    )
    if errorlevel 1 (
        echo [ERROR] Failed to create virtual environment
        echo Please reinstall Python from: https://www.python.org/downloads/
        pause
        exit /b 1
    )
    echo [OK] Virtual environment created
) else (
    echo [OK] Virtual environment already exists
)

echo Activating virtual environment...
call "%VENV_DIR%\Scripts\activate.bat"
if errorlevel 1 (
    echo [WARNING] Failed to activate virtual environment, recreating...
    rmdir /s /q "%VENV_DIR%"
    if "%USE_VIRTUALENV%"=="1" (
        python -m virtualenv "%VENV_DIR%"
    ) else (
        python -m venv "%VENV_DIR%"
    )
    call "%VENV_DIR%\Scripts\activate.bat"
    if errorlevel 1 (
        echo [ERROR] Failed to activate virtual environment
        pause
        exit /b 1
    )
)

echo [OK] Virtual environment activated
echo.

rem ==========================================
rem Step 4: Install Requirements
rem ==========================================
echo [4/6] Installing requirements...

if exist "%SCRIPT_DIR%\requirements.txt" (
    echo Upgrading pip...
    python -m pip install --upgrade pip --quiet

    echo Installing dependencies...
    pip install -r "%SCRIPT_DIR%\requirements.txt" --quiet
    if errorlevel 1 (
        echo [WARNING] Some packages failed to install
    ) else (
        echo [OK] Requirements installed
    )
) else (
    echo [WARNING] requirements.txt not found, skipping...
)
echo.

rem ==========================================
rem Step 5: Check for Sensor
rem ==========================================
echo [5/6] Checking for sensor...
echo.

python -c "import serial.tools.list_ports; ports = list(serial.tools.list_ports.comports()); print(f'Found {len(ports)} serial port(s):'); [print(f'  {p.device}: {p.description}') for p in ports]"
echo.

rem ==========================================
rem Step 6: Find Sensor
rem ==========================================
echo [6/6] Looking for tactile sensor...
python -c "import serial.tools.list_ports; sensor = next((p for p in serial.tools.list_ports.comports() if 'Robotiq' in (p.description or '') or 'Cypress' in (p.description or '') or (p.vid == 0x04b4 and p.pid == 0xf232)), None); print(f'[OK] Found sensor at {sensor.device}' if sensor else '[WARNING] Sensor not found - make sure it is plugged in')"
echo.

echo ==========================================
echo Starting Simple Sensor Checker
echo ==========================================
echo.
echo Using Python from: %VENV_DIR%\Scripts\python.exe
echo Virtual environment: %VIRTUAL_ENV%
echo.

rem Run the sensor checker
cd /d "%SCRIPT_DIR%"
python quick_connect.py %*

rem ==========================================
rem Cleanup
rem ==========================================
echo.
echo ==========================================
echo Sensor checker stopped.
echo ==========================================

if defined VIRTUAL_ENV (
    echo Deactivating virtual environment...
    call deactivate
    echo [OK] Virtual environment deactivated
) else (
    echo No virtual environment was active.
)

echo Done.
echo.
pause
endlocal
exit /b 0
