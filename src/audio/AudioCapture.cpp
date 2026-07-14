#include "AudioCapture.h"
#include "AudioProcessor.h"
#include <cstring>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// AudioCapture — PortAudio initialisation + output stream management.
//
// Windows signal flow (fixed):
//   [WASAPI CaptureWorker thread]
//       WASAPI loopback/mic → convertFrame → raw float frames → StereoRingBuffer
//   [PortAudio high-priority audio thread]
//       StereoRingBuffer → pop raw frame → AudioProcessor::processStereo → output
//
// The DSP MUST run in the PortAudio callback, not in the CaptureWorker.
// Moving it to the audio thread:
//   • Eliminates missed WASAPI events (the burst handler completes instantly)
//   • Uses the OS-scheduled real-time audio thread for all DSP
//   • Keeps PortAudio latency well-defined (bufferSize / sampleRate seconds)
// ──────────────────────────────────────────────────────────────────────────────

AudioCapture::AudioCapture(AudioProcessor* proc, QObject* parent)
    : QObject(parent), m_proc(proc) {
    Pa_Initialize();

#ifdef _WIN32
    // WASAPICapture no longer takes an AudioProcessor* — it only owns the ring.
    m_wasapi = new WASAPICapture(m_ring, this);
    connect(m_wasapi, &WASAPICapture::errorOccurred,
            this,     &AudioCapture::errorOccurred);
#endif
}

AudioCapture::~AudioCapture() {
    close();
    Pa_Terminate();
}

// ──────────────────────────────────────────────────────────────────────────────
// Device enumeration
// ──────────────────────────────────────────────────────────────────────────────
std::vector<AudioDeviceInfo> AudioCapture::enumerateInputSources() {
#ifdef _WIN32
    return WASAPICapture::enumerateInputSources();
#else
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
    // ── Windows: WASAPI input (raw frames only) + PortAudio output (DSP here) ─
    if (!m_wasapi->open(inputDeviceId, inputType, sampleRate, errorOut)) {
        if (!errorOut.isEmpty())
            emit errorOccurred(errorOut);
        return false;
    }
    m_sampleRate = m_wasapi->actualSampleRate();
    m_proc->setSampleRate(m_sampleRate);

    if (!openOutputOnly(outputDeviceId, m_sampleRate, bufferSize, errorOut)) {
        emit errorOccurred(errorOut);
        m_wasapi->close();
        return false;
    }

    m_open.store(true);
    return true;

#else
    // ── Non-Windows: PortAudio full-duplex ────────────────────────────────────
    int outDev = Pa_GetDefaultOutputDevice();
    if (!outputDeviceId.empty()) {
        try { outDev = std::stoi(outputDeviceId); } catch (...) {}
    }
    if (outDev == paNoDevice) { errorOut = "No output device available."; return false; }

    int inDev = Pa_GetDefaultInputDevice();
    if (!inputDeviceId.empty()) {
        try { inDev = std::stoi(inputDeviceId); } catch (...) {}
    }
    if (inDev == paNoDevice) { errorOut = "No input device available."; return false; }

    const PaDeviceInfo* inInfo  = Pa_GetDeviceInfo(inDev);
    const PaDeviceInfo* outInfo = Pa_GetDeviceInfo(outDev);

    PaStreamParameters inp{};
    inp.device           = inDev;
    inp.channelCount     = std::min(2, inInfo ? inInfo->maxInputChannels : 2);
    inp.sampleFormat     = paFloat32;
    inp.suggestedLatency = inInfo ? inInfo->defaultLowInputLatency : 0.005;

    PaStreamParameters outp{};
    outp.device           = outDev;
    outp.channelCount     = 2;
    outp.sampleFormat     = paFloat32;
    outp.suggestedLatency = outInfo ? outInfo->defaultLowOutputLatency : 0.005;

    m_sampleRate = sampleRate;
    m_proc->setSampleRate(m_sampleRate);

    PaError err = Pa_OpenStream(&m_stream, &inp, &outp, m_sampleRate,
                                (unsigned long)bufferSize, paDitherOff,
                                &AudioCapture::paCallback, this);
    if (err != paNoError) {
        errorOut = QString("PortAudio: %1").arg(Pa_GetErrorText(err));
        return false;
    }
    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        Pa_CloseStream(m_stream); m_stream = nullptr;
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
    int outDev = Pa_GetDefaultOutputDevice();
    if (!outputDeviceId.empty()) {
        try {
            size_t c = outputDeviceId.find(':');
            outDev = std::stoi(c != std::string::npos
                               ? outputDeviceId.substr(0, c) : outputDeviceId);
        } catch (...) {}
    }
    if (outDev == paNoDevice) { errorOut = "No output device available."; return false; }

    const PaDeviceInfo* outInfo = Pa_GetDeviceInfo(outDev);

    PaStreamParameters outp{};
    outp.device           = outDev;
    outp.channelCount     = 2;
    outp.sampleFormat     = paFloat32;
    outp.suggestedLatency = outInfo ? outInfo->defaultLowOutputLatency : 0.005;

    PaError err = Pa_OpenStream(&m_outStream, nullptr, &outp, sampleRate,
                                (unsigned long)bufferSize, paDitherOff,
                                &AudioCapture::outCallback, this);
    if (err != paNoError) {
        errorOut = QString("PortAudio output: %1").arg(Pa_GetErrorText(err));
        return false;
    }
    err = Pa_StartStream(m_outStream);
    if (err != paNoError) {
        Pa_CloseStream(m_outStream); m_outStream = nullptr;
        errorOut = QString("Pa_StartStream (out): %1").arg(Pa_GetErrorText(err));
        return false;
    }
    return true;
}

// ── Windows output callback — runs on the PortAudio real-time audio thread ────
//
// The full DSP chain runs HERE.  The CaptureWorker pushes raw (unprocessed)
// float frames into the ring buffer; we pop them, run processStereo(), and
// write the result to the PortAudio output buffer.
//
// Why here and not in the CaptureWorker?
//   WASAPI delivers data in bursts (~480 frames every 10 ms at 48 kHz).
//   Running expensive DSP (FDN reverb + EQ + …) inside the burst handler
//   causes the thread to miss the next WASAPI event, which leads to dropped
//   capture frames and audible stutter.  The PortAudio audio thread fires at
//   a fixed period (bufferSize / sampleRate seconds) under the real-time
//   OS scheduler — the correct home for sample-accurate DSP.
int AudioCapture::outCallback(const void*, void* outputBuffer,
                               unsigned long frameCount,
                               const PaStreamCallbackTimeInfo*,
                               PaStreamCallbackFlags,
                               void* userData) {
    auto* self = static_cast<AudioCapture*>(userData);
    auto* out  = static_cast<float*>(outputBuffer);

    for (unsigned long i = 0; i < frameCount; ++i) {
        float l = 0.f, r = 0.f;
        self->m_ring.pop(l, r);          // raw input (or 0 on underrun)
        self->m_proc->processStereo(l, r); // full DSP chain
        out[i*2]   = l;
        out[i*2+1] = r;
    }
    return paContinue;
}

#else

// ──────────────────────────────────────────────────────────────────────────────
// Non-Windows: PortAudio full-duplex callback
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
        float l = in[i*2], r = in[i*2+1];
        self->m_proc->processStereo(l, r);
        out[i*2] = l; out[i*2+1] = r;
    }
    return paContinue;
}
#endif
