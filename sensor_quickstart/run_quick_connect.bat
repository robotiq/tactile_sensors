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
rem On a bump, update the licence link in README.md, and check that the new
rem installer still passes the signature check in :install_python.
set "PY_VERSION=3.12.10"
rem PY_SERIES (3.12) names the licence page and the winget package, PY_TAG
rem (312) the folder the installers put Python in, so neither goes stale.
for /f "tokens=1,2 delims=." %%a in ("%PY_VERSION%") do (
    set "PY_SERIES=%%a.%%b"
    set "PY_TAG=%%a%%b"
)

rem ==========================================
rem Step 1: Check for Python, install it if missing
rem ==========================================
echo [1/6] Checking for Python installation...
echo.

rem SYSPY is the full path of the Python used to build the virtual environment.
call :find_python
if not defined SYSPY (
    echo Python 3.10 or newer was not found on this computer.
    echo Python %PY_VERSION% can be installed for the current user ^(no admin rights needed^),
    echo with winget or from python.org. Python is distributed under the PSF License:
    echo   https://docs.python.org/%PY_SERIES%/license.html
    echo Installing it accepts that licence, and winget's source agreements when
    echo winget is used.
    echo.
    rem Asked rather than accepted silently: the licence is the user's to accept.
    rem Exactly 1 is Y. choice returns 0 when interrupted, which is not a Y.
    choice /c YN /m "Install Python %PY_VERSION% now"
    if errorlevel 1 if not errorlevel 2 (
        echo.
        call :install_python
    )
)
if not defined SYSPY (
    echo.
    echo [ERROR] Python 3.10 or newer is needed and was not installed.
    echo.
    echo Please install Python 3.10 or higher from:
    echo   https://www.python.org/downloads/
    echo.
    echo Make sure to check "Add Python to PATH" during installation.
    echo After installing, close this window and run this script again.
    echo.
    pause
    exit /b 1
)

rem Delayed expansion, on for the whole script, strips a ! from the path (for
rem a user named Hey!You, say), which then points nowhere.
if not exist "%SYSPY%" goto :python_path_unusable

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

rem A virtual environment built by an older version of this script may use a
rem Python older than 3.10, which leaves the gripper controls out, so it is
rem built again from the Python found above.
if exist "%VENV_DIR%" (
    "%VENV_DIR%\Scripts\python.exe" -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
    if errorlevel 1 (
        echo The virtual environment uses an older Python, rebuilding it...
        rmdir /s /q "%VENV_DIR%"
    )
)

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

rem A copy of the requirements.txt last installed from is kept in the virtual
rem environment. While it matches, and the packages are still there, pip is not
rem run at all: it needs the internet, and only the first run may need that. A
rem changed requirements.txt, a new virtual environment, or a package that has
rem gone missing since, installs again. The packages are looked up without
rem being imported, which would be slow.
set "REQ_STAMP=%VENV_DIR%\requirements.installed"
if not exist "%SCRIPT_DIR%\requirements.txt" (
    echo [WARNING] requirements.txt not found, skipping...
    goto :requirements_done
)
fc /b "%SCRIPT_DIR%\requirements.txt" "%REQ_STAMP%" >nul 2>&1
if not errorlevel 1 (
    "%VENV_DIR%\Scripts\python.exe" -c "import importlib.util as u, sys; sys.exit(any(u.find_spec(m) is None for m in ('serial', 'websockets', 'pyrobotiqgripper')))" >nul 2>&1
    if not errorlevel 1 (
        echo [OK] Requirements already installed
        goto :requirements_done
    )
)
echo Upgrading pip...
python -m pip install --upgrade pip --quiet

echo Installing dependencies...
python -m pip install -r "%SCRIPT_DIR%\requirements.txt" --quiet
if errorlevel 1 (
    echo [ERROR] Could not install the requirements. The first run needs an internet connection.
    pause
    exit /b 1
)
copy /y "%SCRIPT_DIR%\requirements.txt" "%REQ_STAMP%" >nul
echo [OK] Requirements installed
:requirements_done
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

rem Sets SYSPY to the full path of a Python 3.10+, or leaves it empty. Tries the
rem one on PATH, then the py launcher, then where :install_python puts it -- the
rem PATH change an install makes only reaches windows opened after it. On
rem Windows on ARM both installers use a folder named Python312-arm64, and on
rem 32-bit Windows Python312-32 (for 3.12; PY_TAG gives the series).
:find_python
set "SYSPY="
call :try_python python
if not defined SYSPY call :try_python py -3
for %%D in (Python%PY_TAG% Python%PY_TAG%-arm64 Python%PY_TAG%-32) do (
    if not defined SYSPY if exist "%LOCALAPPDATA%\Programs\Python\%%D\python.exe" call :try_python "%LOCALAPPDATA%\Programs\Python\%%D\python.exe"
)
exit /b 0

rem Sets SYSPY if the command given runs a Python 3.10+. The "python" that
rem Windows ships when none is installed only opens the Microsoft Store and
rem fails here, so it is not mistaken for a real one.
:try_python
%* -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1 || exit /b 0
rem Read straight into SYSPY: a temp file would be shared by two launches
rem running at once. The code is passed through !_PYCODE! because the for
rem parses the command before running it, and would drop the ; and stop at
rem the ) were they written out here. The outer quotes are stripped by the
rem cmd /c that runs the command, which keeps a quoted path intact.
set "_PYCODE=import sys; print(sys.executable)"
for /f "usebackq delims=" %%P in (`"%* -c "!_PYCODE!" 2>nul"`) do set "SYSPY=%%P"
exit /b 0

rem Installs Python for the current user only, so no admin rights are needed.
rem winget first, as it ships with Windows 10 and 11; the python.org installer
rem when winget is missing or fails. Sets SYSPY through :find_python when the
rem install worked.
:install_python
where winget >nul 2>&1
if not errorlevel 1 (
    echo Installing Python with winget...
    winget install --id Python.Python.%PY_SERIES% --version %PY_VERSION% --exact --source winget --scope user --silent --accept-package-agreements --accept-source-agreements
    call :find_python
    if defined SYSPY exit /b 0
    echo [WARNING] winget could not install Python, downloading it from python.org instead...
)
rem The installer for this Windows: -amd64, -arm64, or no suffix for 32-bit.
rem PROCESSOR_ARCHITEW6432 is the real one when this runs in a 32-bit cmd.
set "_ARCH=%PROCESSOR_ARCHITECTURE%"
if defined PROCESSOR_ARCHITEW6432 set "_ARCH=%PROCESSOR_ARCHITEW6432%"
set "_PYARCH=-amd64"
if /i "%_ARCH%"=="ARM64" set "_PYARCH=-arm64"
if /i "%_ARCH%"=="x86" set "_PYARCH="
set "_PYURL=https://www.python.org/ftp/python/%PY_VERSION%/python-%PY_VERSION%%_PYARCH%.exe"
rem Downloaded into a new folder of its own rather than a fixed path in TEMP,
rem so nothing can put a file there, or swap it, between the signature check
rem and the run.
set "_PYDIR=%TEMP%\rq-python-%RANDOM%%RANDOM%%RANDOM%"
rem || rather than errorlevel: a mkdir that works may leave an earlier
rem winget failure's errorlevel in place. A folder already there fails too.
mkdir "%_PYDIR%" 2>nul || (
    echo [ERROR] Could not create a download folder in %TEMP%.
    exit /b 1
)
set "_PYEXE=%_PYDIR%\python-%PY_VERSION%%_PYARCH%.exe"
rem The URL and path reach PowerShell through the environment, not inside its
rem quotes, so a TEMP with an apostrophe in it (a user named O'Brien) works.
echo Downloading %_PYURL%...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference = 'SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -UseBasicParsing -Uri $env:_PYURL -OutFile $env:_PYEXE"
if errorlevel 1 (
    rmdir /s /q "%_PYDIR%" >nul 2>&1
    echo [ERROR] Download failed. Check the internet connection.
    exit /b 1
)
rem Run only an installer signed by the Python Software Foundation, as the
rem macOS launcher does with pkgutil. The whole signer subject must match, and
rem the issuer must be Microsoft's identity-verified code signing CA, which
rem python.org signs through: its certificates last days, so no thumbprint can
rem be pinned. Exit code 2 is a bad signature; anything else non-zero means the
rem check itself could not run, which is not reported as tampering.
echo Checking the installer's signature...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference = 'Stop'; $s = Get-AuthenticodeSignature -LiteralPath $env:_PYEXE; $c = $s.SignerCertificate; if ($s.Status -ne 'Valid' -or $c.Subject -cne 'CN=Python Software Foundation, O=Python Software Foundation, L=Beaverton, S=Oregon, C=US' -or $c.Issuer -cnotlike 'CN=Microsoft ID Verified CS *, O=Microsoft Corporation, C=US') { exit 2 }; exit 0"
set "_SIGERR=%errorlevel%"
if not "%_SIGERR%"=="0" (
    rmdir /s /q "%_PYDIR%" >nul 2>&1
    if "%_SIGERR%"=="2" (
        echo [ERROR] The downloaded installer is not signed by the Python Software Foundation, so it was not run.
    ) else (
        echo [ERROR] The installer's signature could not be checked ^(PowerShell exit code %_SIGERR%^), so it was not run.
    )
    exit /b 1
)
echo [OK] Signed by the Python Software Foundation
echo Running the Python installer, this takes a minute...
"%_PYEXE%" /quiet InstallAllUsers=0 InstallLauncherAllUsers=0 PrependPath=1 Include_test=0
set "_PYERR=%errorlevel%"
rmdir /s /q "%_PYDIR%" >nul 2>&1
if not "%_PYERR%"=="0" (
    echo [ERROR] The Python installer failed with code %_PYERR%.
    exit /b 1
)
echo [OK] Python %PY_VERSION% installed
call :find_python
exit /b 0

:python_path_unusable
echo.
echo [ERROR] Python was found, but its path has a character this script cannot
echo handle, such as an exclamation mark. Install Python 3.10 or higher in a
echo folder without one, for example C:\Python312, then run this script again.
echo.
pause
exit /b 1
