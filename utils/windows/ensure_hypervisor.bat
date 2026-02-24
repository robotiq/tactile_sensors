@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

@REM Check firmware virtualization (Robust Method).
@REM If HypervisorPresent is true, virtualization is active. If false, we check VirtualizationFirmwareEnabled.
powershell -NoProfile -Command ^
 " $sys = Get-CimInstance -ClassName Win32_ComputerSystem; $cpu = Get-CimInstance -ClassName Win32_Processor | Select-Object -First 1; if ($sys.HypervisorPresent -or $cpu.VirtualizationFirmwareEnabled) { exit 0 } else { exit 1 } " >nul 2>&1

if errorlevel 1 (
  echo [ERROR] Hardware virtualization is disabled in BIOS/UEFI.
  echo [INFO] Enable Intel VT-x/AMD-V ^(SVM^) in your firmware settings, save, and reboot.
  echo [INFO] Typical steps: restart, press Del/F2/F10 to enter BIOS/UEFI, enable "Virtualization Technology"/"Intel VT-x"/"AMD-V ^(SVM^)", then save and exit.
  echo [INFO] Guides: Docker Desktop virtualization help https://docs.docker.com/desktop/troubleshoot/topics/#virtualization , Microsoft VT verification https://learn.microsoft.com/windows/security/operating-system-security/virtualization/hyper-v-requirements#verify-support-for-cpu-vt , plus vendor BIOS steps ^(Lenovo/Dell/HP/ASUS^).
  exit /b 1
)

@REM Ensure the Windows hypervisor can start (WSL2 requirement).
bcdedit /enum {current} | findstr /I "hypervisorlaunchtype" | findstr /I "Auto" >nul 2>&1
if errorlevel 1 (
  echo [WARN] Hypervisor launch type is not set to Auto.
  set /p SET_HYPERVISOR=Set "hypervisorlaunchtype" to Auto now? This will open an elevated prompt. [Y/N]:
  if /I "%SET_HYPERVISOR%"=="Y" (
    powershell -NoProfile -Command ^
     "Start-Process cmd -Verb RunAs -ArgumentList '/c bcdedit /set hypervisorlaunchtype auto'"
    echo [INFO] If prompted, allow elevation. A reboot may be required for the change to take effect.
  ) else (
    echo [ERROR] Hypervisor is required for WSL2. Set it to Auto and rerun this script.
    exit /b 1
  )
)

@REM Ensure Virtual Machine Platform feature is enabled.
powershell -NoProfile -Command ^
 " $f = (Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -ErrorAction SilentlyContinue); if ($f.State -eq 'Enabled') { exit 0 } else { exit 1 } " >nul 2>&1
if errorlevel 1 (
  echo [WARN] Windows feature 'Virtual Machine Platform' is disabled; WSL2 needs it.
  set /p ENABLE_VMP=Enable it now? This will open an elevated prompt. [Y/N]:
  if /I "%ENABLE_VMP%"=="Y" (
    powershell -NoProfile -Command ^
     "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -Command Enable-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -All -NoRestart'"
    echo [INFO] If prompted, allow elevation. Please reboot after the feature is enabled.
    exit /b 1
  ) else (
    echo [ERROR] Virtual Machine Platform is required. Enable it and rerun this script.
    exit /b 1
  )
)

echo [INFO] Hypervisor launch type is set and Virtual Machine Platform is enabled.
endlocal
exit /b 0