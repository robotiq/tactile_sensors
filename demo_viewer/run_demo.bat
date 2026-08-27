@echo off
setlocal EnableDelayedExpansion

rem Trade-show demo viewer: tactile pads, gripper pose, force/torque.
rem
rem   run_demo.bat              connect to the sensor and open the viewer
rem   run_demo.bat --sim        synthetic data, no hardware
rem
rem Any other arguments are forwarded (e.g. --port 8099).

rem Keep the window open if something fails, so the error is readable.
if not defined _KEEP_OPEN (
    set "_KEEP_OPEN=1"
    cmd /k "%~f0" %*
    exit /b
)
set "_KEEP_OPEN="

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "VENV_DIR=%SCRIPT_DIR%\.venvDemo"

python --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found. Install Python 3.8+ from
    echo   https://www.python.org/downloads/
    echo and tick "Add Python to PATH" during installation.
    exit /b 1
)

if not exist "%VENV_DIR%" (
    echo Creating virtual environment at %VENV_DIR%...
    python -m venv "%VENV_DIR%"
    if errorlevel 1 exit /b 1
)

rem Windows puts the interpreter in Scripts\, not bin/.
set "VENV_PY=%VENV_DIR%\Scripts\python.exe"
"%VENV_PY%" -m pip install --quiet --upgrade pip
"%VENV_PY%" -m pip install --quiet -r "%SCRIPT_DIR%\requirements.txt"
if errorlevel 1 exit /b 1

if /i "%~1"=="--sim" (
    rem Drop --sim and hand the rest to the simulator.
    set "ARGS=%*"
    set "ARGS=!ARGS:~5!"
    "%VENV_PY%" "%SCRIPT_DIR%\tools\simulate_sensor.py" !ARGS!
) else (
    "%VENV_PY%" "%SCRIPT_DIR%\quick_connect.py" --web %*
)
