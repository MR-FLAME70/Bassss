@echo off
:: ============================================================
::  Bass Nuker — Windows Installer Builder
::  Produces: build\BassNuker-6.9.0-win64-setup.exe
::
::  Prerequisites (edit paths below if different):
::    - Qt 6.5+  installed via Qt Online Installer
::    - vcpkg     installed at C:\vcpkg
::    - PortAudio installed via:  vcpkg install portaudio:x64-windows
::    - CMake 3.20+  on PATH
::    - NSIS 3.x     on PATH  (https://nsis.sourceforge.io)
::    - MSVC 2022    (Visual Studio 17 2022)
:: ============================================================

setlocal EnableDelayedExpansion

:: ── User-configurable paths ──────────────────────────────────────────────────
set QT_DIR=C:\Qt\6.7.0\msvc2019_64
set VCPKG_ROOT=C:\vcpkg
set BUILD_DIR=build
set GENERATOR=Visual Studio 17 2022
set ARCH=x64

:: ── Derived paths (do not edit) ──────────────────────────────────────────────
set TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

:: ── Sanity checks ────────────────────────────────────────────────────────────
where cmake >nul 2>&1 || (echo [ERROR] cmake not found on PATH & exit /b 1)
where cpack >nul 2>&1 || (echo [ERROR] cpack not found on PATH. Install CMake and add it to PATH & exit /b 1)
where makensis >nul 2>&1 || (
    echo [ERROR] NSIS (makensis.exe) not found on PATH.
    echo         Download from https://nsis.sourceforge.io and add to PATH.
    exit /b 1
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [ERROR] windeployqt not found at %QT_DIR%\bin\windeployqt.exe
    echo         Edit QT_DIR at the top of this script to match your Qt install.
    exit /b 1
)

if not exist "%TOOLCHAIN%" (
    echo [ERROR] vcpkg toolchain not found at %TOOLCHAIN%
    echo         Edit VCPKG_ROOT at the top of this script.
    exit /b 1
)

:: ── Step 1: Configure ────────────────────────────────────────────────────────
echo.
echo [1/4] Configuring CMake...
cmake -B "%BUILD_DIR%" ^
      -G "%GENERATOR%" -A %ARCH% ^
      -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_BUILD_TYPE=Release ^
      || (echo [ERROR] CMake configure failed & exit /b 1)

:: ── Step 2: Build (Release) ──────────────────────────────────────────────────
echo.
echo [2/4] Building Release...
cmake --build "%BUILD_DIR%" --config Release --parallel ^
      || (echo [ERROR] Build failed & exit /b 1)

:: ── Step 3: Install into staging prefix ─────────────────────────────────────
echo.
echo [3/4] Installing + staging DLLs (windeployqt)...
cmake --install "%BUILD_DIR%" ^
      --config Release ^
      --prefix "%BUILD_DIR%\_install" ^
      || (echo [ERROR] Install step failed & exit /b 1)

:: ── Step 4: Run CPack to produce the NSIS installer ─────────────────────────
echo.
echo [4/4] Running CPack (NSIS)...
cd "%BUILD_DIR%"
cpack -G NSIS -C Release ^
      || (cd .. & echo [ERROR] CPack failed & exit /b 1)
cd ..

echo.
echo ============================================================
echo  Done!
echo  Installer: %BUILD_DIR%\BassNuker-6.9.0-win64-setup.exe
echo ============================================================
endlocal
