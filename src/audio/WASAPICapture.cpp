#include "WASAPICapture.h"
#include <QMetaObject>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
// ──────────────────────────────────────────────────────────────────────────────
// Windows-only: full WASAPI implementation
// ──────────────────────────────────────────────────────────────────────────────
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <functiondiscoverykeys_devpkey.h>

template<typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T*  operator->() { return p; }
    operator T*() { return p; }
    void reset() { if (p) { p->Release(); p = nullptr; } }
};

static std::string wstrToUtf8(const wchar_t* w) {
    if (!w) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return {};
    std::string s(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], sz, nullptr, nullptr);
    return s;
}

// ── Enumerate all input sources ───────────────────────────────────────────────
std::vector<AudioDeviceInfo> WASAPICapture::enumerateInputSources() {
    std::vector<AudioDeviceInfo> result;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  (void**)&enumerator);
    if (FAILED(hr)) { CoUninitialize(); return result; }

    auto getFriendlyName = [](IMMDevice* dev) -> std::string {
        ComPtr<IPropertyStore> props;
        if (FAILED(dev->OpenPropertyStore(STGM_READ, &props))) return "Unknown";
        PROPVARIANT var; PropVariantInit(&var);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var))
            && var.vt == VT_LPWSTR && var.pwszVal) {
            std::string name = wstrToUtf8(var.pwszVal);
            PropVariantClear(&var); return name;
        }
        PropVariantClear(&var); return "Unknown";
    };

    auto getDefaultRate = [](IMMDevice* dev) -> double {
        ComPtr<IAudioClient> client;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                 nullptr, (void**)&client))) return 48000.0;
        WAVEFORMATEX* fmt = nullptr; double rate = 48000.0;
        if (SUCCEEDED(client->GetMixFormat(&fmt)) && fmt)
            rate = (double)fmt->nSamplesPerSec;
        CoTaskMemFree(fmt); return rate;
    };

    // 1. Render (playback) endpoints → loopback sources
    {
        ComPtr<IMMDeviceCollection> col;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) {
            UINT count = 0; col->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> dev;
                if (FAILED(col->Item(i, &dev))) continue;
                LPWSTR devId = nullptr; dev->GetId(&devId);
                std::string id = devId ? wstrToUtf8(devId) : "";
                CoTaskMemFree(devId);
                AudioDeviceInfo info;
                info.id = id; info.name = getFriendlyName(dev);
                info.type = AudioDeviceType::Loopback;
                info.defaultSampleRate = getDefaultRate(dev);
                info.channels = 2;
                result.push_back(std::move(info));
            }
        }
    }

    // 2. Capture (microphone) endpoints
    {
        ComPtr<IMMDeviceCollection> col;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col))) {
            UINT count = 0; col->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> dev;
                if (FAILED(col->Item(i, &dev))) continue;
                LPWSTR devId = nullptr; dev->GetId(&devId);
                std::string id = devId ? wstrToUtf8(devId) : "";
                CoTaskMemFree(devId);
                AudioDeviceInfo info;
                info.id = id; info.name = getFriendlyName(dev);
                info.type = AudioDeviceType::Microphone;
                info.defaultSampleRate = getDefaultRate(dev);
                info.channels = 2;
                result.push_back(std::move(info));
            }
        }
    }

    CoUninitialize();
    return result;
}

// ── CaptureWorker ─────────────────────────────────────────────────────────────
// Responsibility: read raw WASAPI frames → convert to float32 stereo →
// push to ring buffer. NO DSP here. DSP runs in the PortAudio output callback
// on the real-time audio thread (see AudioCapture::outCallback).
class WASAPICapture::CaptureWorker : public QObject {
    Q_OBJECT
public:
    std::string      deviceId;
    AudioDeviceType  deviceType;
    double           requestedRate;
    std::atomic<bool>* pRunning;
    double*            pActualRate;
    StereoRingBuffer*  ring;

public slots:
    void run() {
        // Raise the capture thread to time-critical priority so Windows
        // schedules it promptly even under system load. Without this,
        // the OS can delay the capture thread by 15-100 ms, draining the
        // ring buffer and causing audible freeze/dropout in the output.
        // SetThreadPriority on the current Win32 thread is instant and
        // safe to call from any thread context.
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (!doCapture()) pRunning->store(false);
        CoUninitialize();
    }

signals:
    void errorOccurred(const QString& msg);

private:
    // WASAPI's IAudioClient::GetMixFormat() almost always returns a
    // WAVEFORMATEXTENSIBLE on modern Windows (wFormatTag == WAVE_FORMAT_EXTENSIBLE,
    // typically 32-bit IEEE float). The *real* sample format lives in the
    // extensible struct's SubFormat GUID, not in the outer wFormatTag/bps —
    // those just describe the container. Reading wFormatTag directly and
    // falling through to the bps==32 "PCM int32" branch reinterprets raw
    // float32 bit patterns as integers, which sounds like loud broadband
    // hiss/static. KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT encode the classic
    // format tag (1 / 3) in the GUID's first 4 bytes, so we can resolve the
    // true tag without pulling in ksmedia.h.
    static WORD effectiveFormatTag(const WAVEFORMATEX* fmt) {
        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            fmt->cbSize >= (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))) {
            const auto* wfex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
            return (WORD)wfex->SubFormat.Data1;
        }
        return fmt->wFormatTag;
    }

    // Convert one WAVEFORMATEX frame to float32 stereo [-1, 1].
    static void convertFrame(const BYTE* src, WAVEFORMATEX* fmt,
                              UINT32 offset, float& l, float& r) {
        const WORD ch  = fmt->nChannels;
        const WORD bps = fmt->wBitsPerSample;
        const WORD tag = effectiveFormatTag(fmt);

        if (tag == WAVE_FORMAT_IEEE_FLOAT) {
            const float* fp = reinterpret_cast<const float*>(src) + offset * ch;
            l = fp[0]; r = (ch >= 2) ? fp[1] : fp[0];
        } else if (tag == WAVE_FORMAT_PCM || bps == 16) {
            const int16_t* ip = reinterpret_cast<const int16_t*>(src) + offset * ch;
            l = ip[0] / 32768.f; r = (ch >= 2) ? ip[1] / 32768.f : l;
        } else if (bps == 24) {
            const BYTE* p = src + offset * ch * 3;
            auto to24 = [](const BYTE* b) -> float {
                int32_t v = (int32_t)(b[0]) | ((int32_t)(b[1])<<8) | ((int32_t)(b[2])<<16);
                if (v & 0x800000) v |= 0xFF000000;
                return v / 8388608.f;
            };
            l = to24(p); r = (ch >= 2) ? to24(p + 3) : l;
        } else if (bps == 32) {
            const int32_t* ip = reinterpret_cast<const int32_t*>(src) + offset * ch;
            l = ip[0] / 2147483648.f; r = (ch >= 2) ? ip[1] / 2147483648.f : l;
        } else {
            l = r = 0.f;
        }
        l = std::max(-1.f, std::min(1.f, l));
        r = std::max(-1.f, std::min(1.f, r));
    }

    bool doCapture() {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                      (void**)&enumerator);
        if (FAILED(hr)) {
            emit errorOccurred("WASAPI: CoCreateInstance(MMDeviceEnumerator) failed.");
            return false;
        }

        ComPtr<IMMDevice> device;
        if (deviceId.empty()) {
            EDataFlow flow = (deviceType == AudioDeviceType::Loopback) ? eRender : eCapture;
            hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
        } else {
            int wsz = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
            std::wstring wid(wsz, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, &wid[0], wsz);
            hr = enumerator->GetDevice(wid.c_str(), &device);
        }
        if (FAILED(hr) || !device.p) {
            emit errorOccurred("WASAPI: Could not find audio device. Is it still connected?");
            return false;
        }

        ComPtr<IAudioClient> audioClient;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                               nullptr, (void**)&audioClient);
        if (FAILED(hr)) {
            emit errorOccurred("WASAPI: IAudioClient activate failed.");
            return false;
        }

        WAVEFORMATEX* mixFmt = nullptr;
        hr = audioClient->GetMixFormat(&mixFmt);
        if (FAILED(hr) || !mixFmt) {
            emit errorOccurred("WASAPI: GetMixFormat failed.");
            return false;
        }
        *pActualRate = (double)mixFmt->nSamplesPerSec;

        // ── "Both" mode clock-matching ──────────────────────────────────────
        // The mic and loopback devices are two independently-clocked WASAPI
        // endpoints; each negotiates its OWN native mix rate above (e.g.
        // speakers at 48000 Hz, a USB/headset mic at 44100 or 16000 Hz).
        // AudioCapture::outCallback pops one frame from each ring per output
        // sample in lockstep — if the two producer rates don't match, one
        // ring silently drains faster than the other and the mismatch
        // accumulates, causing intermittent, seemingly-random dropouts/
        // freezes (heard only in "Both" mode, not with a single source).
        //
        // Fix: when the caller requested a specific rate (AudioCapture passes
        // the loopback's already-negotiated rate when opening the mic), force
        // this endpoint to that rate and let AUTOCONVERTPCM have the WASAPI
        // engine resample internally, so both rings are always fed at the
        // exact same rate the output callback consumes them at.
        if (deviceType != AudioDeviceType::Loopback && requestedRate > 0.0 &&
            (UINT32)requestedRate != mixFmt->nSamplesPerSec) {
            mixFmt->nSamplesPerSec  = (DWORD)requestedRate;
            mixFmt->nAvgBytesPerSec = mixFmt->nSamplesPerSec * mixFmt->nBlockAlign;
            *pActualRate = requestedRate;
        }

        REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
        audioClient->GetDevicePeriod(&defaultPeriod, &minPeriod);

        // Use AUDCLNT_STREAMFLAGS_EVENTCALLBACK for wakeup.
        // For loopback we must use the default (not minimum) period.
        DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        if (deviceType == AudioDeviceType::Loopback)
            streamFlags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        else if (requestedRate > 0.0)
            // Let the WASAPI shared-mode engine resample the mic stream to
            // the forced rate above instead of rejecting the format.
            streamFlags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

        REFERENCE_TIME bufDuration = (deviceType == AudioDeviceType::Loopback)
                                         ? defaultPeriod : (minPeriod > 0 ? minPeriod : defaultPeriod);

        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags,
                                     bufDuration, 0, mixFmt, nullptr);
        if (FAILED(hr)) {
            // Fallback: retry with default period
            audioClient.reset();
            device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
            hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags,
                                         defaultPeriod, 0, mixFmt, nullptr);
            if (FAILED(hr)) {
                CoTaskMemFree(mixFmt);
                emit errorOccurred("WASAPI: AudioClient Initialize failed. "
                                   "Try running as Administrator for loopback.");
                return false;
            }
        }

        HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!hEvent) {
            CoTaskMemFree(mixFmt); emit errorOccurred("WASAPI: CreateEvent failed.");
            return false;
        }
        audioClient->SetEventHandle(hEvent);

        ComPtr<IAudioCaptureClient> captureClient;
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
        if (FAILED(hr)) {
            CoTaskMemFree(mixFmt); CloseHandle(hEvent);
            emit errorOccurred("WASAPI: GetService(IAudioCaptureClient) failed.");
            return false;
        }

        hr = audioClient->Start();
        if (FAILED(hr)) {
            CoTaskMemFree(mixFmt); CloseHandle(hEvent);
            emit errorOccurred("WASAPI: AudioClient Start failed.");
            return false;
        }

        pRunning->store(true);

        // ── Capture loop ──────────────────────────────────────────────────────
        // All we do here: convert raw WASAPI frames → push to ring buffer.
        // DSP (reverb, EQ, etc.) runs in the PortAudio output callback thread.
        while (pRunning->load(std::memory_order_relaxed)) {
            DWORD wr = WaitForSingleObject(hEvent, 200);
            if (wr == WAIT_TIMEOUT) continue;
            if (wr != WAIT_OBJECT_0) break;

            UINT32 packetSize = 0;
            hr = captureClient->GetNextPacketSize(&packetSize);
            if (FAILED(hr)) break;

            while (packetSize > 0) {
                BYTE*  data       = nullptr;
                UINT32 numFrames  = 0;
                DWORD  flags      = 0;
                hr = captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;

                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

                if (!silent && data && numFrames > 0) {
                    for (UINT32 i = 0; i < numFrames; ++i) {
                        float l = 0.f, r = 0.f;
                        convertFrame(data, mixFmt, i, l, r);
                        ring->push(l, r);   // raw — no DSP here
                    }
                } else {
                    // Push silence so output stays in sync with input timing
                    for (UINT32 i = 0; i < numFrames; ++i)
                        ring->push(0.f, 0.f);
                }

                captureClient->ReleaseBuffer(numFrames);
                captureClient->GetNextPacketSize(&packetSize);
            }
        }

        audioClient->Stop();
        CloseHandle(hEvent);
        CoTaskMemFree(mixFmt);
        return true;
    }
};

// ── WASAPICapture public API ──────────────────────────────────────────────────

WASAPICapture::WASAPICapture(StereoRingBuffer& ringBuf, QObject* parent)
    : QObject(parent), m_ring(ringBuf) {}

WASAPICapture::~WASAPICapture() { close(); }

bool WASAPICapture::open(const std::string& deviceId, AudioDeviceType type,
                         double requestedSampleRate, QString& errorOut) {
    close();
    m_ring.reset();

    m_thread = new QThread(this);
    m_worker = new CaptureWorker();
    m_worker->deviceId       = deviceId;
    m_worker->deviceType     = type;
    m_worker->requestedRate  = requestedSampleRate;
    m_worker->pRunning       = &m_running;
    m_worker->pActualRate    = &m_actualRate;
    m_worker->ring           = &m_ring;

    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started,   m_worker, &CaptureWorker::run);
    connect(m_worker, &CaptureWorker::errorOccurred, this, &WASAPICapture::errorOccurred);
    connect(m_worker, &CaptureWorker::errorOccurred, this, [this](const QString&){
        m_running.store(false);
    });
    connect(m_thread, &QThread::finished,  m_worker, &QObject::deleteLater);
    m_thread->start();

    // Spin briefly (max 2 s) waiting for the worker to confirm it started
    for (int i = 0; i < 200 && !m_running.load(); ++i)
        QThread::msleep(10);

    if (!m_running.load()) {
        m_thread->quit();
        m_thread->wait(3000);
        errorOut = "Failed to start WASAPI capture. "
                   "Check device selection and run as Administrator for loopback.";
        return false;
    }
    return true;
}

void WASAPICapture::close() {
    m_running.store(false);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
        m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

#include "WASAPICapture.moc"

#else
// ──────────────────────────────────────────────────────────────────────────────
// Non-Windows stub
// ──────────────────────────────────────────────────────────────────────────────

std::vector<AudioDeviceInfo> WASAPICapture::enumerateInputSources() { return {}; }

WASAPICapture::WASAPICapture(StereoRingBuffer& ringBuf, QObject* parent)
    : QObject(parent), m_ring(ringBuf) {}

WASAPICapture::~WASAPICapture() {}

bool WASAPICapture::open(const std::string&, AudioDeviceType,
                         double, QString& errorOut) {
    errorOut = "WASAPICapture not available on this platform.";
    return false;
}

void WASAPICapture::close() {}

#endif // _WIN32
