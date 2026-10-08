@echo off
setlocal EnableDelayedExpansion

rem --help / -h: print the options and stop. Nothing below is needed for that,
rem and some of it is slow or asks for admin rights (releasing USB devices from
rem WSL), so it is all skipped. Uses the virtual environment's Python when it
rem exists; quick_connect.py prints its help with nothing installed.
set "_HELP="
for %%A in (%*) do (
    if /i "%%~A"=="--help" set "_HELP=1"
    if /i "%%~A"=="-h" set "_HELP=1"
)
if defined _HELP (
    set "_PY=python"
    if exist "%~dp0.venvSimpleCheck\Scripts\python.exe" set "_PY=%~dp0.venvSimpleCheck\Scripts\python.exe"
    "!_PY!" "%~dp0quick_connect.py" %*
    exit /b !errorlevel!
)

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
rem The Python installed when none is found. Pinned so every machine gets one
rem that is known to work; 3.12.10 is the last 3.12 with Windows installers.
set "PY_VERSION=3.12.10"

rem ==========================================
rem Step 1: Check for Python, install it if missing
rem ==========================================
echo [1/6] Checking for Python installation...
echo.

rem SYSPY is the full path of the Python used to build the virtual environment.
call :find_python
if not defined SYSPY (
    echo Python 3.8 or newer was not found on this computer.
    echo Installing Python %PY_VERSION% for the current user ^(no admin rights needed^)...
    echo.
    call :install_python
    call :find_python
)
if not defined SYSPY (
    echo.
    echo [ERROR] Python could not be installed automatically.
    echo.
    echo Please install Python 3.8 or higher from:
    echo   https://www.python.org/downloads/
    echo.
    echo Make sure to check "Add Python to PATH" during installation.
    echo After installing, close this window and run this script again.
    echo.
    pause
    exit /b 1
)

"%SYSPY%" --version
echo [OK] Python found at %SYSPY%

echo Checking for venv module...
"%SYSPY%" -m venv --help >nul 2>&1
if errorlevel 1 (
    echo [WARNING] Python venv module not found, installing virtualenv via pip...
    "%SYSPY%" -m pip install virtualenv --quiet
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
        "%SYSPY%" -m virtualenv "%VENV_DIR%"
    ) else (
        "%SYSPY%" -m venv "%VENV_DIR%"
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
        "%SYSPY%" -m virtualenv "%VENV_DIR%"
    ) else (
        "%SYSPY%" -m venv "%VENV_DIR%"
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
python -c "import serial.tools.list_ports; sensor = next((p for p in serial.tools.list_ports.comports() if 'Robotiq' in (p.description or '') or 'Cypress' in (p.description or '') or (p.vid == 0x16d0 and p.pid == 0x14cc) or (p.vid == 0x04b4 and p.pid == 0xf232)), None); print(f'[OK] Found sensor at {sensor.device}' if sensor else '[WARNING] Sensor not found - make sure it is plugged in')"
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

rem ==========================================
rem Subroutines
rem ==========================================

rem Sets SYSPY to the full path of a Python 3.8+, or leaves it empty. Tries the
rem one on PATH, then the py launcher, then where :install_python puts it -- the
rem PATH change an install makes only reaches windows opened after it.
:find_python
set "SYSPY="
call :try_python python
if not defined SYSPY call :try_python py -3
if not defined SYSPY if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" call :try_python "%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
exit /b 0

rem Sets SYSPY if the command given runs a Python 3.8+. The "python" that
rem Windows ships when none is installed only opens the Microsoft Store and
rem fails here, so it is not mistaken for a real one.
:try_python
%* -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1 || exit /b 0
set "_PYPATH_FILE=%TEMP%\tactile_sensor_python_path.txt"
%* -c "import sys; print(sys.executable)" > "%_PYPATH_FILE%" 2>nul
set /p SYSPY=<"%_PYPATH_FILE%"
del "%_PYPATH_FILE%" >nul 2>&1
exit /b 0

rem Installs Python for the current user only, so no admin rights are needed.
rem winget first, as it ships with Windows 10 and 11; the python.org installer
rem when winget is missing or fails.
:install_python
where winget >nul 2>&1
if not errorlevel 1 (
    echo Installing Python with winget...
    winget install --id Python.Python.3.12 --exact --source winget --scope user --silent --accept-package-agreements --accept-source-agreements
    call :find_python
    if defined SYSPY exit /b 0
    echo [WARNING] winget could not install Python, downloading it from python.org instead...
)
set "_PYARCH=amd64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "_PYARCH=arm64"
set "_PYURL=https://www.python.org/ftp/python/%PY_VERSION%/python-%PY_VERSION%-%_PYARCH%.exe"
set "_PYEXE=%TEMP%\python-%PY_VERSION%-%_PYARCH%.exe"
echo Downloading %_PYURL%...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference = 'SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri '%_PYURL%' -OutFile '%_PYEXE%'"
if errorlevel 1 (
    echo [ERROR] Download failed. Check the internet connection.
    exit /b 1
)
echo Running the Python installer, this takes a minute...
"%_PYEXE%" /quiet InstallAllUsers=0 InstallLauncherAllUsers=0 PrependPath=1 Include_test=0
set "_PYERR=%errorlevel%"
del "%_PYEXE%" >nul 2>&1
if not "%_PYERR%"=="0" (
    echo [ERROR] The Python installer failed with code %_PYERR%.
    exit /b 1
)
echo [OK] Python %PY_VERSION% installed
exit /b 0
