@echo off
setlocal enabledelayedexpansion

echo Infovox 210 SAPI5 build
echo.

set BUILD_DIR_X86=build_x86
set BUILD_DIR_X64=build_x64
set OUTPUT_DIR=output

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 Build Tools or later.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALLDIR=%%i"
if not defined VSINSTALLDIR (
    echo ERROR: No Visual Studio installation with the C++ toolset was found.
    exit /b 1
)
echo Visual Studio: %VSINSTALLDIR%
echo.

rem A worker left running by an earlier test keeps output\infovox_host.exe and
rem unicorn.dll mapped, which makes the staging copy below fail intermittently.
taskkill /F /IM infovox_host.exe >nul 2>&1

echo === Building x64 (worker, emulator, 64-bit SAPI engine) ===
cmake -A x64 -S . -B %BUILD_DIR_X64% || exit /b 1
cmake --build %BUILD_DIR_X64% --config Release || exit /b 1
echo.

echo === Building x86 (32-bit SAPI engine, settings, diagnostics) ===
cmake -A Win32 -S . -B %BUILD_DIR_X86% || exit /b 1
cmake --build %BUILD_DIR_X86% --config Release || exit /b 1
echo.

echo === Staging %OUTPUT_DIR% ===
if not exist %OUTPUT_DIR% mkdir %OUTPUT_DIR%
if not exist %OUTPUT_DIR%\x64 mkdir %OUTPUT_DIR%\x64
if not exist %OUTPUT_DIR%\engine mkdir %OUTPUT_DIR%\engine

copy /Y "%BUILD_DIR_X86%\bin\Release\InfovoxSAPI.dll"        "%OUTPUT_DIR%\" >nul || exit /b 1
copy /Y "%BUILD_DIR_X86%\bin\Release\InfovoxConfig.exe"      "%OUTPUT_DIR%\" >nul || exit /b 1
copy /Y "%BUILD_DIR_X86%\bin\Release\InfovoxDiagnostics.exe" "%OUTPUT_DIR%\" >nul || exit /b 1
copy /Y "%BUILD_DIR_X64%\bin\Release\InfovoxSAPI.dll"        "%OUTPUT_DIR%\x64\" >nul || exit /b 1
copy /Y "%BUILD_DIR_X64%\bin\Release\infovox_host.exe"       "%OUTPUT_DIR%\" >nul || exit /b 1
copy /Y "third_party\unicorn\lib\x64\unicorn.dll"            "%OUTPUT_DIR%\" >nul || exit /b 1
copy /Y "engine\*.ivp"                                       "%OUTPUT_DIR%\engine\" >nul || exit /b 1
echo.

echo === Building installer ===
set "ISCC="
for %%p in (
    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
) do (
    if not defined ISCC if exist %%p set "ISCC=%%~p"
)
if not defined ISCC (
    echo WARNING: Inno Setup 6 not found; skipping installer.
    echo          The staged files in %OUTPUT_DIR% are complete and usable.
    goto :done
)
"%ISCC%" /Q "installer\InfovoxSAPI.iss" || exit /b 1

:done
echo Build finished.
for %%f in ("%OUTPUT_DIR%\Infovox210_SAPI5_*_Setup.exe") do echo Installer: %%f
endlocal
