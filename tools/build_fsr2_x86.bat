@echo off
setlocal enabledelayedexpansion

REM ===========================================================================
REM Build the FSR2 2.2 DX11 API static libs for x86 (Win32).
REM
REM The x64 libs in OptiScaler\library\fsr2\ (ffx_fsr2_api_dx11_x64.lib etc.)
REM were produced by the FidelityFX-FSR2-DX11 fork's build\BuildLibs.bat, which
REM hardcodes "-A x64". The DX9->DX11 bridge is 32-bit, so it needs the x86
REM equivalents — and the fork's CMake already supports them first-class
REM (CMAKE_GENERATOR_PLATFORM == Win32 -> FSR2_PLATFORM_NAME x86). This script
REM is just that build with "-A Win32", copying the result next to the x64 libs.
REM
REM Run on Windows with CMake and a Visual Studio toolchain in PATH. The shader
REM permutations are compiled by the fork's tools\sc\FidelityFX_SC.exe as a
REM CMake prebuild step, which is why this can't run on non-Windows.
REM ===========================================================================

set ROOT=%~dp0..
set API=%ROOT%\external\FidelityFX-FSR2-DX11\src\ffx-fsr2-api
set BUILD=%ROOT%\external\FidelityFX-FSR2-DX11\build\X86
set OUT=%API%\bin\ffx_fsr2_api
set DEST=%ROOT%\OptiScaler\library\fsr2

cmake --version >nul 2>&1
if %errorlevel% NEQ 0 (
    echo [ERROR] CMake not found in PATH. Install CMake and retry.
    exit /b 1
)

if not exist "%API%\CMakeLists.txt" (
    echo [ERROR] FidelityFX-FSR2-DX11 submodule not initialised. Run:
    echo     git submodule update --init external/FidelityFX-FSR2-DX11
    exit /b 1
)

echo Configuring FSR2 DX11 API for x86 (Win32)...
cmake -A Win32 -S "%API%" -B "%BUILD%" -DFFX_FSR2_API_DX11=ON -DFFX_FSR2_API_DX12=OFF -DFFX_FSR2_API_VK=OFF
if %errorlevel% NEQ 0 ( echo [ERROR] CMake configure failed. & exit /b 1 )

echo Building Debug...
cmake --build "%BUILD%" --config Debug
if %errorlevel% NEQ 0 ( echo [ERROR] Debug build failed. & exit /b 1 )

echo Building Release...
cmake --build "%BUILD%" --config Release
if %errorlevel% NEQ 0 ( echo [ERROR] Release build failed. & exit /b 1 )

echo.
echo Copying x86 libs to %DEST% ...
for %%L in (ffx_fsr2_api_x86.lib ffx_fsr2_api_x86d.lib ffx_fsr2_api_dx11_x86.lib ffx_fsr2_api_dx11_x86d.lib) do (
    if not exist "%OUT%\%%L" (
        echo [ERROR] Expected lib missing: %OUT%\%%L
        exit /b 1
    )
    copy /Y "%OUT%\%%L" "%DEST%\" >nul
    echo   %%L
)

echo.
echo Done. x86 FSR2 DX11 libs are in OptiScaler\library\fsr2\.
echo Next: un-exclude FSR2Feature_Dx11.cpp for Win32 and add the x86 libs to
echo the Win32 link inputs in OptiScaler.vcxproj.
endlocal
