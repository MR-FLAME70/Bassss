@echo off
:: ============================================================
::  Bass Nuker — Portable Build (no installer)
::  Produces a plain folder: build\_install\  containing
::  BassNuker.exe + all required DLLs (Qt, PortAudio) — just
::  copy that folder anywhere and run BassNuker.exe.
::
::  Prerequisites (edit paths below if different):
::    - Qt 6.5+  installed via Qt Online Installer
::    - vcpkg     installed at C:\vcpkg
::    - PortAudio installed via:  vcpkg install portaudio:x64-windows
::    - CMake 3.20+  on PATH
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
set INSTALL_DIR=%BUILD_DIR%\_install

:: ── Sanity checks ────────────────────────────────────────────────────────────
where cmake >nul 2>&1 || (echo [ERROR] cmake not found on PATH & exit /b 1)

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
echo [1/3] Configuring CMake...
cmake -B "%BUILD_DIR%" ^
      -G "%GENERATOR%" -A %ARCH% ^
      -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
      -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_BUILD_TYPE=Release ^
      || (echo [ERROR] CMake configure failed & exit /b 1)

:: ── Step 2: Build (Release) ──────────────────────────────────────────────────
echo.
echo [2/3] Building Release...
cmake --build "%BUILD_DIR%" --config Release --parallel ^
      || (echo [ERROR] Build failed & exit /b 1)

:: ── Step 3: Install into a plain folder (exe + Qt/PortAudio DLLs, no NSIS) ───
echo.
echo [3/3] Collecting files into %INSTALL_DIR% ...
cmake --install "%BUILD_DIR%" ^
      --config Release ^
      --prefix "%INSTALL_DIR%" ^
      || (echo [ERROR] Install step failed & exit /b 1)

echo.
echo ============================================================
echo  Done! No installer was created.
echo  Your program files are here:
echo    %INSTALL_DIR%
echo  Just copy that whole folder anywhere and run BassNuker.exe
echo ============================================================
endlocal
