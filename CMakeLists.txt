cmake_minimum_required(VERSION 3.20)
project(BassNuker VERSION 6.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Concurrent)

# ── PortAudio ────────────────────────────────────────────────────────────────
# On Windows: install PortAudio 19 (e.g. via vcpkg: vcpkg install portaudio)
# On other platforms: brew install portaudio / apt install libportaudio2
#
# Preferred path: vcpkg (and modern PortAudio installs generally) ship a real
# CMake package config exposing an imported target ("portaudio") that carries
# its own full, correct path to the .lib/.dll — no path guessing needed, and
# it works whether vcpkg is in classic mode (C:/vcpkg/installed/...) or
# manifest mode (<build-dir>/vcpkg_installed/...), since the toolchain file
# already points find_package() at the right triplet directory.
find_package(portaudio CONFIG QUIET)
if(TARGET portaudio)
    set(PORTAUDIO_FOUND TRUE)
    set(PORTAUDIO_TARGET portaudio)
endif()

if(NOT PORTAUDIO_FOUND)
    # Fallback for systems without a CMake package config (typical of
    # apt/Homebrew installs on Linux/macOS, which rely on pkg-config).
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PORTAUDIO portaudio-2.0)
        if(PORTAUDIO_FOUND)
            # pkg_check_modules only returns bare library names
            # (e.g. "portaudio"), not full paths — without also telling the
            # linker which directory to search, it fails with
            # "cannot open input file 'portaudio.lib'"/"-lportaudio not
            # found". LINK_DIRECTORIES must be set explicitly.
            link_directories(${PORTAUDIO_LIBRARY_DIRS})
        endif()
    endif()
endif()

if(NOT PORTAUDIO_FOUND)
    # Last resort: manual search in common install locations.
    find_library(PORTAUDIO_LIBRARIES NAMES portaudio portaudio_x64 portaudio_x86
        PATHS
            "C:/vcpkg/installed/x64-windows/lib"
            "C:/vcpkg/installed/x86-windows/lib"
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/lib"
            "/usr/local/lib" "/usr/lib"
    )
    find_path(PORTAUDIO_INCLUDE_DIRS portaudio.h
        PATHS
            "C:/vcpkg/installed/x64-windows/include"
            "C:/vcpkg/installed/x86-windows/include"
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/include"
            "/usr/local/include" "/usr/include"
    )
    if(PORTAUDIO_LIBRARIES AND PORTAUDIO_INCLUDE_DIRS)
        set(PORTAUDIO_FOUND TRUE)
    endif()
endif()

if(NOT PORTAUDIO_FOUND)
    message(FATAL_ERROR
        "PortAudio not found.\n"
        "Windows: run  vcpkg install portaudio  and set CMAKE_TOOLCHAIN_FILE.\n"
        "macOS:   brew install portaudio\n"
        "Linux:   apt install libportaudio-dev\n"
    )
endif()

# ── libsndfile (for WAV/MP3 file export tab) ─────────────────────────────────
find_package(SndFile QUIET)
if(NOT SndFile_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(SNDFILE sndfile)
endif()
if(NOT SNDFILE_FOUND)
    find_library(SNDFILE_LIBRARIES NAMES sndfile libsndfile
        PATHS "C:/vcpkg/installed/x64-windows/lib" "/usr/local/lib" "/usr/lib")
    find_path(SNDFILE_INCLUDE_DIRS sndfile.h
        PATHS "C:/vcpkg/installed/x64-windows/include" "/usr/local/include" "/usr/include")
    if(SNDFILE_LIBRARIES AND SNDFILE_INCLUDE_DIRS)
        set(SNDFILE_FOUND TRUE)
    endif()
endif()

# ── Sources ──────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/settings.cpp
    src/audio/AudioCapture.cpp
    src/audio/AudioProcessor.cpp
    src/audio/WASAPICapture.cpp      # Windows-native WASAPI capture (stub on non-Win)
    src/dsp/BiquadFilter.cpp
    src/dsp/FDNReverb.cpp
    src/dsp/ReverbEngine.cpp
    src/dsp/PitchShifter.cpp
    src/dsp/Rotator.cpp
    src/dsp/Compressor.cpp
    src/dsp/Equalizer.cpp
    src/dsp/StereoWidth.cpp
    src/dsp/AcousticEngine.cpp
    src/dsp/SpeakerConfig.cpp
    src/dsp/DynamicBass.cpp
    src/dsp/ClampProcessor.cpp
    src/dsp/EchoEngine.cpp
    src/ui/LiveTab.cpp
    src/ui/AdvancedTab.cpp
    src/ui/AdvancedAudioTab.cpp
    src/ui/CustomWidgets.cpp
    src/ui/PillTabBar.cpp
    src/ui/SplashScreen.cpp
    resources/resources.qrc
)

set(HEADERS
    src/mainwindow.h
    src/settings.h
    src/audio/AudioCapture.h
    src/audio/AudioProcessor.h
    src/audio/WASAPICapture.h
    src/audio/RingBuffer.h
    src/dsp/BiquadFilter.h
    src/dsp/FDNReverb.h
    src/dsp/ReverbEngine.h
    src/dsp/PitchShifter.h
    src/dsp/Rotator.h
    src/dsp/Compressor.h
    src/dsp/Equalizer.h
    src/dsp/StereoWidth.h
    src/dsp/AcousticEngine.h
    src/dsp/SpeakerConfig.h
    src/dsp/DynamicBass.h
    src/dsp/ClampProcessor.h
    src/dsp/EchoEngine.h
    src/ui/LiveTab.h
    src/ui/AdvancedTab.h
    src/ui/AdvancedAudioTab.h
    src/ui/CustomWidgets.h
    src/ui/PillTabBar.h
    src/ui/SplashScreen.h
)

if(WIN32)
    set(APP_ICON_RESOURCE resources/app_icon.rc)
endif()

add_executable(BassNuker WIN32 ${SOURCES} ${HEADERS} ${APP_ICON_RESOURCE})

# MSVC's <cmath> only exposes M_PI/M_SQRT2/etc. when _USE_MATH_DEFINES is
# defined before the first include. The DSP code relies on M_PI.
if(MSVC)
    target_compile_definitions(BassNuker PRIVATE _USE_MATH_DEFINES NOMINMAX)
endif()

# The Visual Studio generator does not support AUTOMOC_COMPILER_PREDEFINES,
# so moc never automatically learns that _WIN32 is defined. Without this,
# moc silently skips any Q_OBJECT class guarded by #ifdef _WIN32 (e.g.
# WASAPICapture::CaptureWorker), producing "No relevant classes found" and
# unresolved external symbol linker errors for that class's meta-object.
if(WIN32)
    target_compile_definitions(BassNuker PRIVATE _WIN32)
endif()

target_include_directories(BassNuker PRIVATE
    src
    ${PORTAUDIO_INCLUDE_DIRS}
)

target_link_libraries(BassNuker PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Concurrent
    $<$<BOOL:${PORTAUDIO_TARGET}>:${PORTAUDIO_TARGET}>
    $<$<NOT:$<BOOL:${PORTAUDIO_TARGET}>>:${PORTAUDIO_LIBRARIES}>
)

if(SNDFILE_FOUND)
    target_compile_definitions(BassNuker PRIVATE HAVE_SNDFILE=1)
    target_include_directories(BassNuker PRIVATE ${SNDFILE_INCLUDE_DIRS})
    target_link_libraries(BassNuker PRIVATE ${SNDFILE_LIBRARIES})
endif()

# ── Windows: link Core Audio / COM libraries for WASAPI capture ───────────────
# IMMDeviceEnumerator / IMMDevice (mmdeviceapi.h) and IAudioClient /
# IAudioCaptureClient (audioclient.h) are pure COM interfaces obtained via
# CoCreateInstance / IMMDevice::Activate — there is no mmdevapi.lib or
# audioclient.lib in the Windows SDK to link against, so they're deliberately
# left out below (listing them causes LNK1181: cannot open input file).
# ole32      — CoCreateInstance, CoInitializeEx, PropVariantClear
# uuid       — GUID definitions (__uuidof)
# propsys    — IPropertyStore / PKEY_Device_FriendlyName
# ksuser     — Required by PortAudio WASAPI host (KS streaming)
# avrt       — AvSetMmThreadCharacteristics / AvRevertMmThreadCharacteristics
#              (MMCSS real-time scheduling for the WASAPI capture thread)
if(WIN32)
    target_link_libraries(BassNuker PRIVATE
        ole32 uuid propsys ksuser avrt)

    # Suppress the console window in Release builds
    set_target_properties(BassNuker PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

# ── Install / Deploy ─────────────────────────────────────────────────────────
# Install the executable to the root of the install prefix (flat layout).
# CPack will package whatever ends up under CMAKE_INSTALL_PREFIX, so a flat
# layout means BassNuker.exe sits directly in the installer's root directory.
install(TARGETS BassNuker RUNTIME DESTINATION .)

if(WIN32)
    find_program(WINDEPLOYQT windeployqt)

    # ── POST_BUILD: pull Qt DLLs next to the .exe for local dev runs ──────────
    if(WINDEPLOYQT)
        add_custom_command(TARGET BassNuker POST_BUILD
            COMMAND ${WINDEPLOYQT}
                    --no-translations
                    --no-opengl-sw
                    "$<TARGET_FILE:BassNuker>"
            COMMENT "Running windeployqt (dev build)...")
    endif()

    # ── INSTALL-TIME: pull Qt DLLs into the CPack staging tree ───────────────
    # We must run windeployqt a second time, targeting the *installed* exe,
    # because POST_BUILD output lives in the build dir, not the install prefix.
    if(WINDEPLOYQT)
        install(CODE "
            message(STATUS \"windeployqt: staging Qt DLLs for installer…\")
            execute_process(
                COMMAND \"${WINDEPLOYQT}\"
                        --no-translations
                        --no-opengl-sw
                        --dir \"\${CMAKE_INSTALL_PREFIX}\"
                        \"\${CMAKE_INSTALL_PREFIX}/BassNuker.exe\"
                RESULT_VARIABLE _wdq_result
            )
            if(NOT _wdq_result EQUAL 0)
                message(WARNING \"windeployqt returned \${_wdq_result} — Qt DLLs may be missing from the installer.\")
            endif()
        ")
    endif()

    # ── PortAudio DLL ─────────────────────────────────────────────────────────
    # When using the vcpkg imported target the DLL lives next to the .lib in
    # the bin/ sub-directory of the triplet. Try to locate it and stage it.
    if(TARGET portaudio)
        get_target_property(_pa_implib portaudio IMPORTED_IMPLIB_RELEASE)
        if(_pa_implib)
            get_filename_component(_pa_lib_dir "${_pa_implib}" DIRECTORY)
            # vcpkg layout: …/lib/portaudio.lib  →  …/bin/portaudio.dll
            get_filename_component(_pa_root "${_pa_lib_dir}" DIRECTORY)
            file(GLOB _pa_dll "${_pa_root}/bin/portaudio*.dll")
        endif()
    endif()
    if(NOT _pa_dll)
        # Fallback: search the standard vcpkg locations
        file(GLOB _pa_dll
            "C:/vcpkg/installed/x64-windows/bin/portaudio*.dll"
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/portaudio*.dll"
        )
    endif()
    if(_pa_dll)
        install(FILES ${_pa_dll} DESTINATION .)
    else()
        message(WARNING
            "PortAudio DLL not found — the installer will be missing portaudio.dll.\n"
            "Copy portaudio.dll to the install prefix manually before running CPack.")
    endif()
endif()

# ── CPack / NSIS Installer ────────────────────────────────────────────────────
if(WIN32)
    set(CPACK_GENERATOR "NSIS")

    # ── Package metadata ──────────────────────────────────────────────────────
    set(CPACK_PACKAGE_NAME              "BassNuker")
    set(CPACK_PACKAGE_VENDOR            "Bass Nuker")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Bass Nuker — System Audio Processor (Bass Boost · FDN Reverb · EQ · …)")
    set(CPACK_PACKAGE_VERSION_MAJOR     ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR     ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH     ${PROJECT_VERSION_PATCH})
    set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "BassNuker")

    # ── Output filename ───────────────────────────────────────────────────────
    set(CPACK_PACKAGE_FILE_NAME "BassNuker-${PROJECT_VERSION}-win64-setup")

    # ── Icons (MUI2) ──────────────────────────────────────────────────────────
    set(CPACK_NSIS_MUI_ICON   "${CMAKE_SOURCE_DIR}/resources/icons/icon128.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/icons/icon128.ico")

    # ── NSIS display strings ──────────────────────────────────────────────────
    set(CPACK_NSIS_PACKAGE_NAME    "Bass Nuker ${PROJECT_VERSION}")
    set(CPACK_NSIS_DISPLAY_NAME    "Bass Nuker ${PROJECT_VERSION}")
    set(CPACK_NSIS_INSTALL_ROOT    "$PROGRAMFILES64")

    # ── Start Menu shortcut ───────────────────────────────────────────────────
    set(CPACK_PACKAGE_EXECUTABLES  "BassNuker" "Bass Nuker")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Bass Nuker.lnk' \
'$INSTDIR\\\\BassNuker.exe' '' '$INSTDIR\\\\BassNuker.exe' 0")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Bass Nuker.lnk'")

    # ── Desktop shortcut (opt-in checkbox during install) ────────────────────
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        MessageBox MB_YESNO 'Create a desktop shortcut?' IDNO skip_desktop
            CreateShortCut '$DESKTOP\\\\Bass Nuker.lnk' '$INSTDIR\\\\BassNuker.exe' \
'' '$INSTDIR\\\\BassNuker.exe' 0
        skip_desktop:
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "Delete '$DESKTOP\\\\Bass Nuker.lnk'")

    # ── Installer behaviour ───────────────────────────────────────────────────
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)

    # ── License page (optional — comment out if you have no LICENSE.txt) ──────
    # set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

    include(CPack)
endif()
