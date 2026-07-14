#include "AudioCapture.h"
#include "AudioProcessor.h"
#include <QMetaObject>
#include <cstring>
#include <algorithm>
#include <cmath>

// ──────────────────────────────────────────────────────────────────────────────
// AudioCapture — PortAudio initialisation + output stream management.
//
// Input is handled by WASAPICapture on Windows (see WASAPICapture.cpp).
// On non-Windows a standard PortAudio full-duplex stream is used instead.
// ──────────────────────────────────────────────────────────────────────────────

AudioCapture::AudioCapture(AudioProcessor* proc, QObject* parent)
    : QObject(parent), m_proc(proc) {
    Pa_Initialize();

#ifdef _WIN32
    m_wasapi = new WASAPICapture(proc, m_ring, this);
    connect(m_wasapi, &WASAPICapture::errorOccurred,
            this,     &AudioCapture::errorOccurred);
#endif
}

AudioCapture::~AudioCapture() {
    close();
    Pa_Terminate();
}

// ──────────────────────────────────────────────────────────────────────────────
// Enumerate input sources
// ──────────────────────────────────────────────────────────────────────────────
std::vector<AudioDeviceInfo> AudioCapture::enumerateInputSources() {
#ifdef _WIN32
    return WASAPICapture::enumerateInputSources();
#else
    // Non-Windows: expose PortAudio input devices as "Microphone" type
    std::vector<AudioDeviceInfo> result;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxInputChannels == 0) continue;
        AudioDeviceInfo d;
        d.id                = std::to_string(i);
        d.name              = info->name ? info->name : "";
        d.type              = AudioDeviceType::Microphone;
        d.defaultSampleRate = info->defaultSampleRate;
        d.channels          = std::min(2, info->maxInputChannels);
        result.push_back(d);
    }
    return result;
#endif
}

// ──────────────────────────────────────────────────────────────────────────────
// Enumerate output devices (always PortAudio)
// ──────────────────────────────────────────────────────────────────────────────
std::vector<AudioCapture::OutputDeviceInfo> AudioCapture::enumerateOutputDevices() {
    std::vector<OutputDeviceInfo> result;
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxOutputChannels == 0) continue;
        OutputDeviceInfo d;
        d.paIndex           = i;
        std::string name    = info->name ? info->name : "";
        d.id                = std::to_string(i) + ":" + name;
        d.name              = name;
        d.defaultSampleRate = info->defaultSampleRate;
        result.push_back(d);
    }
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// Open
// ──────────────────────────────────────────────────────────────────────────────
bool AudioCapture::open(const std::string& inputDeviceId,
                        AudioDeviceType   inputType,
                        const std::string& outputDeviceId,
                        double sampleRate, int bufferSize,
                        QString& errorOut) {
    if (m_open.load()) close();

    if (sampleRate <= 0.0) sampleRate = 48000.0;

#ifdef _WIN32
    // ── Windows: WASAPI input + PortAudio output ──────────────────────────────

    // 1. Start WASAPI capture first so we know the actual sample rate.
    if (!m_wasapi->open(inputDeviceId, inputType, sampleRate, errorOut)) {
        // Emit so the UI receives a signal-driven error even if the caller
        // also checks the return value (avoids needing two separate error paths).
        if (!errorOut.isEmpty())
            emit errorOccurred(errorOut);
        return false;
    }
    m_sampleRate = m_wasapi->actualSampleRate();
    m_proc->setSampleRate(m_sampleRate);

    // 2. Open PortAudio output-only stream.
    if (!openOutputOnly(outputDeviceId, m_sampleRate, bufferSize, errorOut)) {
        emit errorOccurred(errorOut);
        m_wasapi->close();
        return false;
    }

    m_open.store(true);
    return true;

#else
    // ── Non-Windows: standard PortAudio full-duplex ───────────────────────────

    // Parse output device index from id (format "<paIndex>:<name>")
    int outDev = Pa_GetDefaultOutputDevice();
    if (!outputDeviceId.empty()) {
        try { outDev = std::stoi(outputDeviceId); } catch (...) {}
    }
    if (outDev == paNoDevice) {
        errorOut = "No output device available.";
        return false;
    }

    // Parse input device index
    int inDev = Pa_GetDefaultInputDevice();
    if (!inputDeviceId.empty()) {
        try { inDev = std::stoi(inputDeviceId); } catch (...) {}
    }
    if (inDev == paNoDevice) {
        errorOut = "No input device available.";
        return false;
    }

    const PaDeviceInfo* inInfo  = Pa_GetDeviceInfo(inDev);
    const PaDeviceInfo* outInfo = Pa_GetDeviceInfo(outDev);

    PaStreamParameters inputParams{};
    inputParams.device           = inDev;
    inputParams.channelCount     = std::min(2, inInfo ? inInfo->maxInputChannels : 2);
    inputParams.sampleFormat     = paFloat32;
    inputParams.suggestedLatency = inInfo ? inInfo->defaultLowInputLatency : 0.005;

    PaStreamParameters outputParams{};
    outputParams.device           = outDev;
    outputParams.channelCount     = 2;
    outputParams.sampleFormat     = paFloat32;
    outputParams.suggestedLatency = outInfo ? outInfo->defaultLowOutputLatency : 0.005;

    if (sampleRate <= 0.0) sampleRate = 44100.0;
    m_sampleRate = sampleRate;
    m_proc->setSampleRate(m_sampleRate);

    PaError err = Pa_OpenStream(&m_stream,
                                &inputParams, &outputParams,
                                m_sampleRate, (unsigned long)bufferSize,
                                paDitherOff, &AudioCapture::paCallback, this);
    if (err != paNoError) {
        errorOut = QString("PortAudio error: %1").arg(Pa_GetErrorText(err));
        return false;
    }

    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        errorOut = QString("Pa_StartStream: %1").arg(Pa_GetErrorText(err));
        return false;
    }

    m_open.store(true);
    return true;
#endif
}

// ──────────────────────────────────────────────────────────────────────────────
// Close
// ──────────────────────────────────────────────────────────────────────────────
void AudioCapture::close() {
#ifdef _WIN32
    if (m_wasapi) m_wasapi->close();
    if (m_outStream) {
        Pa_StopStream(m_outStream);
        Pa_CloseStream(m_outStream);
        m_outStream = nullptr;
    }
#else
    if (m_stream) {
        Pa_StopStream(m_stream);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    }
#endif
    m_open.store(false);
}

// ──────────────────────────────────────────────────────────────────────────────
// Windows: open PortAudio output-only stream
// ──────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
bool AudioCapture::openOutputOnly(const std::string& outputDeviceId,
                                  double sampleRate, int bufferSize,
                                  QString& errorOut) {
    // Resolve output device PA index
    int outDev = Pa_GetDefaultOutputDevice();
    if (!outputDeviceId.empty()) {
        // id format: "<paIndex>:<name>"
        try {
            size_t colonPos = outputDeviceId.find(':');
            std::string idxStr = (colonPos != std::string::npos)
                                     ? outputDeviceId.substr(0, colonPos)
                                     : outputDeviceId;
            outDev = std::stoi(idxStr);
        } catch (...) {}
    }
    if (outDev == paNoDevice) {
        errorOut = "No output device available.";
        return false;
    }

    const PaDeviceInfo* outInfo = Pa_GetDeviceInfo(outDev);

    PaStreamParameters outputParams{};
    outputParams.device           = outDev;
    outputParams.channelCount     = 2;
    outputParams.sampleFormat     = paFloat32;
    // Request lowest possible output latency for minimum delay
    outputParams.suggestedLatency = outInfo ? outInfo->defaultLowOutputLatency : 0.005;

    PaError err = Pa_OpenStream(&m_outStream,
                                nullptr, &outputParams,
                                sampleRate, (unsigned long)bufferSize,
                                paDitherOff, &AudioCapture::outCallback, this);
    if (err != paNoError) {
        errorOut = QString("PortAudio output error: %1").arg(Pa_GetErrorText(err));
        return false;
    }

    err = Pa_StartStream(m_outStream);
    if (err != paNoError) {
        Pa_CloseStream(m_outStream);
        m_outStream = nullptr;
        errorOut = QString("Pa_StartStream (output): %1").arg(Pa_GetErrorText(err));
        return false;
    }
    return true;
}

// PortAudio output-only callback — pulls processed frames from ring buffer.
int AudioCapture::outCallback(const void*, void* outputBuffer,
                              unsigned long frameCount,
                              const PaStreamCallbackTimeInfo*,
                              PaStreamCallbackFlags,
                              void* userData) {
    auto* self = static_cast<AudioCapture*>(userData);
    auto* out  = static_cast<float*>(outputBuffer);

    for (unsigned long i = 0; i < frameCount; ++i) {
        float l = 0.f, r = 0.f;
        self->m_ring.pop(l, r);    // returns silence if ring is empty
        out[i*2]   = l;
        out[i*2+1] = r;
    }
    return paContinue;
}

#else

// ──────────────────────────────────────────────────────────────────────────────
// Non-Windows: standard PortAudio full-duplex callback
// ──────────────────────────────────────────────────────────────────────────────
int AudioCapture::paCallback(const void* inputBuffer, void* outputBuffer,
                             unsigned long frameCount,
                             const PaStreamCallbackTimeInfo*,
                             PaStreamCallbackFlags,
                             void* userData) {
    auto* self = static_cast<AudioCapture*>(userData);
    auto* in   = static_cast<const float*>(inputBuffer);
    auto* out  = static_cast<float*>(outputBuffer);

    if (!in) {
        std::memset(out, 0, frameCount * 2 * sizeof(float));
        return paContinue;
    }

    for (unsigned long i = 0; i < frameCount; ++i) {
        float l = in[i*2];
        float r = in[i*2+1];
        self->m_proc->processStereo(l, r);
        out[i*2]   = l;
        out[i*2+1] = r;
    }
    return paContinue;
}
#endif
