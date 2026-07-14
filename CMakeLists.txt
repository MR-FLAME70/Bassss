cmake_minimum_required(VERSION 3.20)
project(BassNuker VERSION 6.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

# ── PortAudio ────────────────────────────────────────────────────────────────
# On Windows: install PortAudio 19 (e.g. via vcpkg: vcpkg install portaudio)
# On other platforms: brew install portaudio / apt install libportaudio2
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PORTAUDIO portaudio-2.0)
endif()

if(NOT PORTAUDIO_FOUND)
    # Fallback: look in common paths
    find_library(PORTAUDIO_LIBRARIES NAMES portaudio portaudio_x64 portaudio_x86
        PATHS
            "C:/vcpkg/installed/x64-windows/lib"
            "C:/vcpkg/installed/x86-windows/lib"
            "/usr/local/lib" "/usr/lib"
    )
    find_path(PORTAUDIO_INCLUDE_DIRS portaudio.h
        PATHS
            "C:/vcpkg/installed/x64-windows/include"
            "C:/vcpkg/installed/x86-windows/include"
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
    src/ui/LiveTab.cpp
    src/ui/FileTab.cpp
    src/ui/AdvancedTab.cpp
    src/ui/AdvancedAudioTab.cpp
    src/ui/CustomWidgets.cpp
    resources/resources.qrc
)

set(HEADERS
    src/mainwindow.h
    src/settings.h
    src/audio/AudioCapture.h
    src/audio/AudioProcessor.h
    src/audio/WASAPICapture.h
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
    src/ui/LiveTab.h
    src/ui/FileTab.h
    src/ui/AdvancedTab.h
    src/ui/AdvancedAudioTab.h
    src/ui/CustomWidgets.h
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
    ${PORTAUDIO_LIBRARIES}
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
if(WIN32)
    target_link_libraries(BassNuker PRIVATE
        ole32 uuid propsys ksuser)

    # Suppress the console window in Release builds
    set_target_properties(BassNuker PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

# ── Install / Deploy ─────────────────────────────────────────────────────────
install(TARGETS BassNuker RUNTIME DESTINATION bin)

# On Windows, run windeployqt automatically after build (if found)
if(WIN32)
    find_program(WINDEPLOYQT windeployqt)
    if(WINDEPLOYQT)
        add_custom_command(TARGET BassNuker POST_BUILD
            COMMAND ${WINDEPLOYQT} --no-translations "$<TARGET_FILE:BassNuker>"
            COMMENT "Running windeployqt...")
    endif()
endif()
