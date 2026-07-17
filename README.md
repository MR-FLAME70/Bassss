# Bass Nuker — Native Windows Desktop Application

A pixel-faithful conversion of the Bass Nuker Chrome extension into a fully native
Windows application using C++ and Qt6. No browser technology (Electron, Tauri,
WebView2, CEF, HTML) is used.

---

## Features

| Feature | Details |
|---|---|
| **System Audio Loopback** | Captures all audio playing through Windows speakers via WASAPI loopback (equivalent of `tabCapture` in the browser extension) |
| **Bass Boost** | Low-shelf BiquadFilter — exact Web Audio API coefficient formulas |
| **FDN Reverb** | 8-line Feedback Delay Network with Householder matrix, per-line allpass diffusion, RT60-correct gains, HF/LF damping, and sinusoidal LFO modulation |
| **Early Reflections** | Synthesised cave impulse response (mulberry32 PRNG, golden-ratio scatter, same algorithm as `reverb-engine.js`) |
| **Pitch Shifter** | Two-grain Hann-windowed overlap-add — grain=4096, ring=16384, same as `pitch-shift-worklet.js` |
| **Rotator** | 2×2 stereo rotation matrix advancing at rateHz — same as `rotator-worklet.js` |
| **Acoustic Engine** | SBX Pro Studio–style: Surround, Crystalizer, Bass shelf, Smart Volume (compressor), Dialog Plus (peaking) |
| **10-Band EQ** | Peaking biquads at 31/62/125/250/500/1k/2k/4k/8k/16k Hz with preset library |
| **Compressor** | Web Audio DynamicsCompressor semantics — threshold, knee, ratio, attack/release |
| **Limiter** | Same compressor, ratio=20, knee=0, attack=1ms |
| **Stereo Width** | Mid/Side encode → scale side → decode (matches offscreen.js M/S module) |
| **Dynamic Bass** | Additive lowpass+compressor branch, dry always passes through |
| **Speaker Config** | Headphones, Stereo, 2.1, 4.0, 4.1, 5.1, 7.1 virtual surround with per-channel levels |
| **Soft Clamp** | tanh at ±0.8 threshold — exact port of `clamp-worklet.js` |
| **Dark GUI** | 800×600 fixed frameless window matching popup.css exactly |

---

## Building

### Prerequisites

| Tool | Version | Notes |
|---|---|---|
| CMake | ≥ 3.20 | |
| Qt | 6.5+ | Install via the [Qt Online Installer](https://www.qt.io/download) |
| PortAudio | 19.7+ | On Windows, install via `vcpkg install portaudio:x64-windows` |
| Compiler | MSVC 2022 / MinGW-w64 | C++17 required |

### Windows — Quick installer build (recommended)

Edit the two paths at the top of **`build-installer.bat`** to match your
Qt and vcpkg locations, then double-click it (or run from a Developer Command
Prompt). It will:

1. Configure CMake (Release, x64, MSVC)
2. Build `BassNuker.exe`
3. Stage all Qt DLLs via `windeployqt` and the PortAudio DLL
4. Run CPack/NSIS and produce **`build\BassNuker-6.9.0-win64-setup.exe`**

```
Prerequisites
─────────────
  Qt 6.5+      Qt Online Installer → https://www.qt.io/download
  vcpkg         git clone https://github.com/microsoft/vcpkg C:\vcpkg
                C:\vcpkg\bootstrap-vcpkg.bat
                C:\vcpkg\vcpkg install portaudio:x64-windows
  CMake 3.20+  https://cmake.org/download/
  NSIS 3.x     https://nsis.sourceforge.io  (add to PATH)
  MSVC 2022    Visual Studio 17 2022 with C++ workload
```

```bat
:: Edit these two lines in build-installer.bat:
set QT_DIR=C:\Qt\6.7.0\msvc2019_64
set VCPKG_ROOT=C:\vcpkg

:: Then run:
build-installer.bat
```

---

### Windows — Manual CMake build (without installer)

```powershell
# 1. Install vcpkg + PortAudio
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install portaudio:x64-windows

# 2. Configure and build
cd bass-nuker-qt
cmake -B build -G "Visual Studio 17 2022" -A x64 \
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64"

cmake --build build --config Release

# 3. Deploy Qt DLLs (run from the build/Release directory)
cd build\Release
windeployqt --release BassNuker.exe
```

---

### Manual installer (CPack only — if you already built)

```powershell
cd build
cpack -G NSIS -C Release
# → BassNuker-6.9.0-win64-setup.exe
```

### macOS (Homebrew — for development/testing; loopback requires BlackHole or Loopback app)

```bash
brew install qt portaudio cmake
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build --config Release
```

### Linux (ALSA/PulseAudio — loopback via PulseAudio monitor source)

```bash
sudo apt install qt6-base-dev libportaudio-dev cmake
cmake -B build
cmake --build build --config Release
```

---

## Signal Chain

The DSP graph implemented in `AudioProcessor::processStereo()` matches
`buildCaptureGraph()` in the original `offscreen.js` exactly:

```
System Audio (WASAPI Loopback)
  → BiquadFilter (LowShelf, bass boost)
  → VolumeGain
  ├→ DryGain ──────────────────────────────────────────────────┐
  └→ ReverbEngine                                               │
       (PreDelay → ER convolver + FDN late)                     │
       → ResonanceFilter (peaking)                              │
       → WetGain ──────────────────────────────────────────────┤
       → Rotator (stereo rotation) → SurroundGain ─────────────┤
                                                                ↓
                                                             SumNode
                                                                ↓
                                               ProcessedMasterGain / BypassGain
                                                                ↓
                                                       AcousticEngine
                                                    (Surround · Crystalizer · Bass
                                                     · Smart Volume · Dialog Plus)
                                                                ↓
                                                  10-Band Peaking EQ
                                                  DynamicBass (additive)
                                                  Compressor (dynamics)
                                                  Limiter (brickwall)
                                                  StereoWidth (M/S)
                                                  PitchShifter (granular OLA)
                                                                ↓
                                                     SpeakerConfigEngine
                                                   (HRTF / 2.1 / 5.1 / 7.1)
                                                                ↓
                                                    AnalyserNode (VU + spectrum)
                                                                ↓
                                                ClampProcessor (soft tanh at 0.8)
                                                                ↓
                                                     Audio Output Device
```

---

## Default Preset (hauntedcavernv2)

| Parameter | Value |
|---|---|
| Bass frequency | 150 Hz |
| Bass gain | 12 dB |
| Reverb decay | 9.0 s |
| Reverb mix | 74 % |
| Pre-delay | 38 ms |
| Diffusion | 78 % |
| Tone | 3600 Hz |
| Room size | 2.6 |
| Stereo width | 165 % |
| HF damping | 35 % |
| LF damping | 15 % |
| Mod depth | 55 % |
| Mod rate | 30 % |
| Low cut | 90 Hz |

---

## Project Structure

```
bass-nuker-qt/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp                   Entry point
│   ├── mainwindow.h/cpp           800×600 frameless main window + title bar + tabs
│   ├── settings.h/cpp             AppSettings — full mirror of chrome.storage siteSettings
│   ├── dsp/
│   │   ├── BiquadFilter.h         Web Audio API biquad (lowshelf/highshelf/peaking/…)
│   │   ├── FDNReverb.h/cpp        8-line FDN — port of fdn-reverb-worklet.js
│   │   ├── ReverbEngine.h/cpp     Hybrid reverb — port of reverb-engine.js
│   │   ├── PitchShifter.h/cpp     Granular OLA — port of pitch-shift-worklet.js
│   │   ├── Rotator.h/cpp          2×2 rotation matrix — port of rotator-worklet.js
│   │   ├── Compressor.h/cpp       DynamicsCompressor (Web Audio semantics)
│   │   ├── Equalizer.h/cpp        10-band peaking EQ
│   │   ├── StereoWidth.h/cpp      M/S stereo width
│   │   ├── AcousticEngine.h/cpp   SBX Pro Studio (Surround/Crystalizer/Bass/…)
│   │   ├── SpeakerConfig.h/cpp    Virtual surround (Headphones/2.1/5.1/7.1)
│   │   ├── DynamicBass.h/cpp      Additive bass enhancer
│   │   └── ClampProcessor.h/cpp   Soft tanh clamp at ±0.8
│   ├── audio/
│   │   ├── AudioProcessor.h/cpp   Full DSP chain (thread-safe parameter updates)
│   │   └── AudioCapture.h/cpp     PortAudio WASAPI loopback capture + output
│   └── ui/
│       ├── CustomWidgets.h/cpp    ToggleSwitch, DarkSlider, DarkKnob, VUMeter, …
│       ├── LiveTab.h/cpp          Tab 1: Bass Boost + Reverb + Output/Record
│       ├── AdvancedTab.h/cpp      Tab 2: Acoustic Engine + Speaker Config + Adv Reverb
│       └── AdvancedAudioTab.h/cpp Tab 3: EQ + Dyn Bass + Comp + Limiter + Width + Pitch
└── resources/
    ├── resources.qrc
    └── icons/                     icon16/48/128.png + preview.gif
```

---

## Notes on WASAPI Loopback

On Windows, the application opens the **default output device in loopback mode**
via PortAudio's WASAPI backend. This is the exact equivalent of what
`chrome.tabCapture.getMediaStreamId()` provides in the extension — it captures
everything playing through the system speakers, processes it, and outputs the
result back to the speakers (or a separate output device).

To use with a specific application only (instead of all system audio), configure
a per-application audio routing tool such as **Voicemeeter** or use Windows'
**App Volume** settings to route specific apps to a virtual sink.

---

## License

This software is provided for personal/educational use. All DSP algorithms are
independent C++ implementations derived from first-principles analysis of the
open-source Chrome extension source code.
