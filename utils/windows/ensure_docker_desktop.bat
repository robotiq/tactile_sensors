@echo off
setlocal EnableDelayedExpansion

rem --- Entry Point ---
call :ensure_cli
if errorlevel 1 goto :end

call :ensure_running
goto :end

:ensure_cli
where docker >nul 2>&1
if not errorlevel 1 exit /b 0

echo [INFO] Docker Desktop (docker.exe) not found on PATH.
set /p INSTALL_CHOICE=Install Docker Desktop via winget now? [Y/N]: 

if /I "!INSTALL_CHOICE!" NEQ "Y" (
    echo [ERROR] Docker Desktop is required.
    exit /b 1
)

echo [INFO] Installing Docker Desktop...
powershell -NoProfile -Command "Start-Process winget -Verb RunAs -ArgumentList 'install','--id','Docker.DockerDesktop','-e','--accept-source-agreements','--accept-package-agreements' -Wait"

if errorlevel 1 (
    echo [ERROR] Winget failed.
    exit /b 1
)

timeout /t 5 >nul
where docker >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Installation incomplete. Please finish manually.
    exit /b 1
)
exit /b 0

:ensure_running
docker info >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Docker Desktop daemon is running.
    exit /b 0
)

echo [INFO] Docker Desktop is not running. Attempting to start...
call :start_desktop
if errorlevel 1 exit /b 1

call :wait_for_desktop
exit /b %ERRORLEVEL%

:start_desktop
set "DOCKER_EXE="

rem --- Strategy 1: Check Standard Program Files ---
set "TEST_PATH=%ProgramFiles%\Docker\Docker\Docker Desktop.exe"
if exist "%TEST_PATH%" set "DOCKER_EXE=%TEST_PATH%"
if defined DOCKER_EXE goto :launch_now

rem --- Strategy 2: Check Program Files (x86) ---
rem We assign to a temp variable to hide the parentheses from the parser
set "P86=%ProgramFiles(x86)%"
if not defined P86 goto :check_registry
set "TEST_PATH=%P86%\Docker\Docker\Docker Desktop.exe"
if exist "%TEST_PATH%" set "DOCKER_EXE=%TEST_PATH%"
if defined DOCKER_EXE goto :launch_now

:check_registry
rem --- Strategy 3: PowerShell Registry Lookup ---
for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "$p = Get-Item 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\Docker Desktop.exe' -ErrorAction SilentlyContinue; if($p){ $p.GetValue('') }"`) do set "DOCKER_EXE=%%I"

:launch_now
if not defined DOCKER_EXE (
    echo [ERROR] Could not find Docker Desktop.exe.
    exit /b 1
)

if not exist "!DOCKER_EXE!" (
    echo [ERROR] File not found: "!DOCKER_EXE!"
    exit /b 1
)

start "" "!DOCKER_EXE!" >nul 2>&1
exit /b 0

:wait_for_desktop
set "MAX_ATTEMPTS=30"
set /a COUNT=0
echo [INFO] Waiting for Docker daemon (this may take 1-2 minutes)...

:wait_loop
docker info >nul 2>&1
if not errorlevel 1 (
    echo [INFO] Docker is ready.
    exit /b 0
)

set /a COUNT+=1
if !COUNT! GEQ !MAX_ATTEMPTS! (
    echo [ERROR] Docker timed out.
    exit /b 1
)

timeout /t 5 >nul
goto :wait_loop

:end
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%