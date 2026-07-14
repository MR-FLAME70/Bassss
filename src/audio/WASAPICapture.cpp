#include "WASAPICapture.h"
#include "AudioProcessor.h"
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
#include <functiondiscoverykeys_devpkey.h>
#include <wchar.h>

// Convenience smart-pointer for COM interfaces
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

    // Helper: get friendly name for a device
    auto getFriendlyName = [](IMMDevice* dev) -> std::string {
        ComPtr<IPropertyStore> props;
        if (FAILED(dev->OpenPropertyStore(STGM_READ, &props))) return "Unknown";
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var))
            && var.vt == VT_LPWSTR && var.pwszVal) {
            std::string name = wstrToUtf8(var.pwszVal);
            PropVariantClear(&var);
            return name;
        }
        PropVariantClear(&var);
        return "Unknown";
    };

    // Helper: get default sample rate from device
    auto getDefaultRate = [](IMMDevice* dev) -> double {
        ComPtr<IAudioClient> client;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                 nullptr, (void**)&client)))
            return 48000.0;
        WAVEFORMATEX* fmt = nullptr;
        double rate = 48000.0;
        if (SUCCEEDED(client->GetMixFormat(&fmt)) && fmt)
            rate = (double)fmt->nSamplesPerSec;
        CoTaskMemFree(fmt);
        return rate;
    };

    // ── 1. Render (playback) endpoints → shown as loopback sources ───────────
    {
        ComPtr<IMMDeviceCollection> col;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) {
            UINT count = 0;
            col->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> dev;
                if (FAILED(col->Item(i, &dev))) continue;

                LPWSTR devId = nullptr;
                dev->GetId(&devId);
                std::string id = devId ? wstrToUtf8(devId) : "";
                CoTaskMemFree(devId);

                AudioDeviceInfo info;
                info.id      = id;
                info.name    = getFriendlyName(dev);
                info.type    = AudioDeviceType::Loopback;
                info.defaultSampleRate = getDefaultRate(dev);
                info.channels = 2;
                result.push_back(std::move(info));
            }
        }
    }

    // ── 2. Capture (microphone) endpoints ────────────────────────────────────
    {
        ComPtr<IMMDeviceCollection> col;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col))) {
            UINT count = 0;
            col->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> dev;
                if (FAILED(col->Item(i, &dev))) continue;

                LPWSTR devId = nullptr;
                dev->GetId(&devId);
                std::string id = devId ? wstrToUtf8(devId) : "";
                CoTaskMemFree(devId);

                AudioDeviceInfo info;
                info.id      = id;
                info.name    = getFriendlyName(dev);
                info.type    = AudioDeviceType::Microphone;
                info.defaultSampleRate = getDefaultRate(dev);
                info.channels = 2;
                result.push_back(std::move(info));
            }
        }
    }

    CoUninitialize();
    return result;
}

// ── CaptureWorker — runs on its own QThread, owns all COM objects ─────────────
class WASAPICapture::CaptureWorker : public QObject {
    Q_OBJECT
public:
    WASAPICapture* parent;
    std::string    deviceId;
    AudioDeviceType deviceType;
    double         requestedRate;

    std::atomic<bool>* pRunning;
    double*            pActualRate;

    AudioProcessor*   proc;
    StereoRingBuffer* ring;

public slots:
    void run() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        if (!doCapture()) {
            pRunning->store(false);
        }

        CoUninitialize();
    }

signals:
    void errorOccurred(const QString& msg);

private:
    // Convert a WAVEFORMATEX (any common format) frame to float32 stereo.
    // Returns L and R in [−1, 1].
    static void convertFrame(const BYTE* src, WAVEFORMATEX* fmt, UINT32 offset,
                              float& l, float& r) {
        const WORD channels = fmt->nChannels;
        const WORD bps      = fmt->wBitsPerSample;

        if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            // 32-bit float
            const float* fp = reinterpret_cast<const float*>(src) + offset * channels;
            l = fp[0];
            r = (channels >= 2) ? fp[1] : fp[0];
        } else if (fmt->wFormatTag == WAVE_FORMAT_PCM || bps == 16) {
            // 16-bit PCM (also handles EXTENSIBLE with 16-bit sub-format)
            const int16_t* ip = reinterpret_cast<const int16_t*>(src) + offset * channels;
            l = ip[0] / 32768.f;
            r = (channels >= 2) ? ip[1] / 32768.f : l;
        } else if (bps == 24) {
            // 24-bit PCM packed
            const BYTE* p = src + offset * channels * 3;
            auto to24 = [](const BYTE* b) -> float {
                int32_t v = (int32_t)(b[0]) | ((int32_t)(b[1]) << 8) | ((int32_t)(b[2]) << 16);
                if (v & 0x800000) v |= 0xFF000000;
                return v / 8388608.f;
            };
            l = to24(p);
            r = (channels >= 2) ? to24(p + 3) : l;
        } else if (bps == 32 && fmt->wFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
            // 32-bit PCM integer
            const int32_t* ip = reinterpret_cast<const int32_t*>(src) + offset * channels;
            l = ip[0] / 2147483648.f;
            r = (channels >= 2) ? ip[1] / 2147483648.f : l;
        } else {
            l = r = 0.f;
        }
        // Clamp
        l = std::max(-1.f, std::min(1.f, l));
        r = std::max(-1.f, std::min(1.f, r));
    }

    bool doCapture() {
        // ── Get device ────────────────────────────────────────────────────────
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
            // Default device
            EDataFlow flow = (deviceType == AudioDeviceType::Loopback) ? eRender : eCapture;
            hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
        } else {
            // Convert UTF-8 id → wstring
            int wsz = MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, nullptr, 0);
            std::wstring wid(wsz, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, deviceId.c_str(), -1, &wid[0], wsz);
            hr = enumerator->GetDevice(wid.c_str(), &device);
        }
        if (FAILED(hr) || !device.p) {
            emit errorOccurred("WASAPI: Could not find audio device. "
                               "Is it still connected?");
            return false;
        }

        // ── Activate IAudioClient ─────────────────────────────────────────────
        ComPtr<IAudioClient> audioClient;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                               nullptr, (void**)&audioClient);
        if (FAILED(hr)) {
            emit errorOccurred("WASAPI: IAudioClient activate failed.");
            return false;
        }

        // ── Get mix format ────────────────────────────────────────────────────
        WAVEFORMATEX* mixFmt = nullptr;
        hr = audioClient->GetMixFormat(&mixFmt);
        if (FAILED(hr) || !mixFmt) {
            emit errorOccurred("WASAPI: GetMixFormat failed.");
            return false;
        }

        *pActualRate = (double)mixFmt->nSamplesPerSec;

        // ── Initialize stream ──────────────────────────────────────────────────
        // Request minimum possible latency (event-driven, minimum buffer).
        REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
        audioClient->GetDevicePeriod(&defaultPeriod, &minPeriod);

        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        if (deviceType == AudioDeviceType::Loopback) {
            flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        }

        // Use default period for loopback (minimum is not supported for loopback)
        REFERENCE_TIME period = (deviceType == AudioDeviceType::Loopback)
                                    ? defaultPeriod
                                    : minPeriod;
        if (period == 0) period = defaultPeriod;

        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            flags,
            period,       // buffer duration
            0,            // periodicity (must be 0 for shared mode)
            mixFmt,
            nullptr);

        if (FAILED(hr)) {
            // Retry with default period
            audioClient.reset();
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                   nullptr, (void**)&audioClient);
            if (SUCCEEDED(hr)) {
                hr = audioClient->Initialize(
                    AUDCLNT_SHAREMODE_SHARED,
                    flags,
                    defaultPeriod, 0, mixFmt, nullptr);
            }
            if (FAILED(hr)) {
                CoTaskMemFree(mixFmt);
                emit errorOccurred("WASAPI: AudioClient Initialize failed. "
                                   "Try running as administrator for loopback devices.");
                return false;
            }
        }

        // ── Set up event for low-latency wakeup ───────────────────────────────
        HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!hEvent) {
            CoTaskMemFree(mixFmt);
            emit errorOccurred("WASAPI: CreateEvent failed.");
            return false;
        }
        audioClient->SetEventHandle(hEvent);

        // ── Get capture client ────────────────────────────────────────────────
        ComPtr<IAudioCaptureClient> captureClient;
        hr = audioClient->GetService(__uuidof(IAudioCaptureClient),
                                     (void**)&captureClient);
        if (FAILED(hr)) {
            CoTaskMemFree(mixFmt);
            CloseHandle(hEvent);
            emit errorOccurred("WASAPI: GetService(IAudioCaptureClient) failed.");
            return false;
        }

        // ── Start ─────────────────────────────────────────────────────────────
        hr = audioClient->Start();
        if (FAILED(hr)) {
            CoTaskMemFree(mixFmt);
            CloseHandle(hEvent);
            emit errorOccurred("WASAPI: AudioClient Start failed.");
            return false;
        }

        pRunning->store(true);

        // ── Capture loop ──────────────────────────────────────────────────────
        while (pRunning->load(std::memory_order_relaxed)) {
            DWORD waitResult = WaitForSingleObject(hEvent, 200 /*ms timeout*/);
            if (waitResult == WAIT_TIMEOUT) {
                // Check if we should stop (pRunning cleared externally)
                continue;
            }
            if (waitResult != WAIT_OBJECT_0) break;

            UINT32 packetSize = 0;
            hr = captureClient->GetNextPacketSize(&packetSize);
            if (FAILED(hr)) break;

            while (packetSize > 0) {
                BYTE*  data  = nullptr;
                UINT32 framesAvail = 0;
                DWORD  flags       = 0;
                hr = captureClient->GetBuffer(&data, &framesAvail, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;

                bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;

                if (!silent && data && framesAvail > 0) {
                    for (UINT32 i = 0; i < framesAvail; ++i) {
                        float l = 0.f, r = 0.f;
                        convertFrame(data, mixFmt, i, l, r);
                        proc->processStereo(l, r);
                        ring->push(l, r);
                    }
                } else {
                    // Push silence so output stream stays in sync
                    for (UINT32 i = 0; i < framesAvail; ++i) {
                        float l = 0.f, r = 0.f;
                        proc->processStereo(l, r);
                        ring->push(l, r);
                    }
                }

                captureClient->ReleaseBuffer(framesAvail);
                captureClient->GetNextPacketSize(&packetSize);
            }
        }

        // ── Cleanup ───────────────────────────────────────────────────────────
        audioClient->Stop();
        CloseHandle(hEvent);
        CoTaskMemFree(mixFmt);
        return true;
    }
};

// ── WASAPICapture public API ──────────────────────────────────────────────────

WASAPICapture::WASAPICapture(AudioProcessor* proc,
                             StereoRingBuffer& ringBuf,
                             QObject* parent)
    : QObject(parent), m_proc(proc), m_ring(ringBuf) {}

WASAPICapture::~WASAPICapture() {
    close();
}

bool WASAPICapture::open(const std::string& deviceId, AudioDeviceType type,
                         double requestedSampleRate, QString& errorOut) {
    close();
    m_ring.reset();

    m_thread = new QThread(this);
    m_worker = new CaptureWorker();
    m_worker->parent          = this;
    m_worker->deviceId        = deviceId;
    m_worker->deviceType      = type;
    m_worker->requestedRate   = requestedSampleRate;
    m_worker->pRunning        = &m_running;
    m_worker->pActualRate     = &m_actualRate;
    m_worker->proc            = m_proc;
    m_worker->ring            = &m_ring;

    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &CaptureWorker::run);
    connect(m_worker, &CaptureWorker::errorOccurred, this, &WASAPICapture::errorOccurred);
    connect(m_worker, &CaptureWorker::errorOccurred, this, [this](const QString& msg){
        Q_UNUSED(msg);
        m_running.store(false);
    });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();

    // Give the worker a moment to initialise and set m_running.
    // We check with a brief spin (max 2 seconds).
    for (int i = 0; i < 200 && !m_running.load(); ++i) {
        QThread::msleep(10);
        // Check if thread reported an error (set running=false immediately)
        // A real error will have emitted errorOccurred from the worker thread.
    }

    if (!m_running.load()) {
        // Thread either failed or hasn't started yet; close cleanly.
        m_thread->quit();
        m_thread->wait(3000);
        errorOut = "Failed to start WASAPI capture (check device selection and permissions).";
        return false;
    }

    return true;
}

void WASAPICapture::close() {
    m_running.store(false);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(3000);
        // m_worker is deleted via deleteLater connected to finished
        m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    }
}

// Required for Q_OBJECT in a .cpp-defined class
#include "WASAPICapture.moc"

#else
// ──────────────────────────────────────────────────────────────────────────────
// Non-Windows stub
// ──────────────────────────────────────────────────────────────────────────────

std::vector<AudioDeviceInfo> WASAPICapture::enumerateInputSources() {
    return {}; // PortAudio fallback used on non-Windows
}

WASAPICapture::WASAPICapture(AudioProcessor* proc,
                             StereoRingBuffer& ringBuf,
                             QObject* parent)
    : QObject(parent), m_proc(proc), m_ring(ringBuf) {}

WASAPICapture::~WASAPICapture() {}

bool WASAPICapture::open(const std::string&, AudioDeviceType,
                         double, QString& errorOut) {
    errorOut = "WASAPICapture not available on this platform.";
    return false;
}

void WASAPICapture::close() {}

#endif // _WIN32
