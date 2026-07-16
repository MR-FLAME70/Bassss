#include "AudioCapture.h"
#include "AudioProcessor.h"
#include <cstring>
#include <algorithm>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#define BASSNUKER_HAS_SSE_DENORMAL_CONTROL 1
#endif

// ──────────────────────────────────────────────────────────────────────────────
// Denormal (subnormal) float protection for the audio thread.
//
// IIR filters and feedback loops (biquads, the FDN reverb's damping/allpass
// state, compressor/limiter envelopes) settle toward exact zero
// asymptotically. Once their state drops into the denormal range
// (~1e-38 and smaller), some x86 CPUs execute every subsequent float op on
// that value 10-100x slower in microcode — inaudible on its own, but a
// direct, well-documented cause of periodic CPU spikes/crackling that
// appear specifically during quiet passages or silence (exactly when it's
// least expected). Setting FTZ (flush-to-zero) + DAZ (denormals-are-zero)
// once per audio thread eliminates the entire class of bug at the hardware
// level instead of requiring every DSP module to add its own epsilon hacks.
// ──────────────────────────────────────────────────────────────────────────────
static void enableDenormalFlushToZero() {
#ifdef BASSNUKER_HAS_SSE_DENORMAL_CONTROL
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

// ──────────────────────────────────────────────────────────────────────────────
// AudioCapture — PortAudio initialisation + output stream management.
//
// Windows signal flow (fixed):
//   [WASAPI CaptureWorker thread]
//       WASAPI loopback/mic → convertFrame → raw float frames → StereoRingBuffer
//   [PortAudio high-priority audio thread]
//       StereoRingBuffer → pop raw frame → AudioProcessor::processStereo → output
//
// "Both" mode adds a second WASAPI capture for the microphone in parallel:
//   [WASAPI CaptureWorker — loopback]  → m_ring
//   [WASAPI CaptureWorker — mic]       → m_ring2
//   [PortAudio audio thread]  pops from both rings, sums frames, runs DSP.
//
// The DSP MUST run in the PortAudio callback, not in the CaptureWorker.
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
                        QString& errorOut,
                        const std::string& micDeviceId) {
    if (m_open.load()) close();
    if (sampleRate <= 0.0) sampleRate = 48000.0;

#ifdef _WIN32
    // ── Windows: WASAPI input (raw frames only) + PortAudio output (DSP here) ─

    // Open primary source (loopback or mic-only).
    if (!m_wasapi->open(inputDeviceId, inputType, sampleRate, errorOut)) {
        if (!errorOut.isEmpty())
            emit errorOccurred(errorOut);
        return false;
    }
    m_sampleRate = m_wasapi->actualSampleRate();
    m_proc->setSampleRate(m_sampleRate);

    // ── "Both" mode: additionally open the microphone as a second source ──────
    // micDeviceId is non-empty only when audioSourceMode == "both".
    // We open the mic at the same sample rate the loopback negotiated.
    // A failure here is treated as non-fatal — we fall back to loopback-only
    // and emit a warning so the user knows the mic didn't open.
    m_mixMode.store(false);
    if (!micDeviceId.empty()) {
        // Allocate the secondary capture object on demand (only when needed).
        if (!m_wasapiMic) {
            m_wasapiMic = new WASAPICapture(m_ring2, this);
            connect(m_wasapiMic, &WASAPICapture::errorOccurred,
                    this,        &AudioCapture::errorOccurred);
        }
        QString micErr;
        if (m_wasapiMic->open(micDeviceId, AudioDeviceType::Microphone,
                               m_sampleRate, micErr)) {
            m_mixMode.store(true);
        } else {
            // Non-fatal: warn but continue with loopback-only.
            emit errorOccurred(
                QString("Mic could not be opened (loopback-only fallback): %1").arg(micErr));
        }
    }

    // ── Pre-fill: wait until the ring has kTargetLatencyFrames worth of data ──
    // The WASAPI capture thread starts pushing frames immediately after open().
    // Without this wait, the PortAudio output callback fires the moment
    // Pa_StartStream() is called — which is before enough frames have
    // accumulated — and the first N callbacks pop zeros (underrun). This
    // manifests as a brief freeze at startup. We block here (main thread is
    // fine; startup is not time-critical) until the ring has enough data to
    // sustain smooth output from the very first callback.
    //
    // Timeout of 3 s covers the worst-case WASAPI initialization delay.
    {
        constexpr int kWaitMs  = 3000;
        constexpr int kStepMs  = 5;
        int waited = 0;
        while (m_ring.available() < StereoRingBuffer::kTargetLatencyFrames
               && waited < kWaitMs) {
            QThread::msleep(kStepMs);
            waited += kStepMs;
        }
        // If "both" mode, also pre-fill the mic ring.
        if (m_mixMode.load()) {
            waited = 0;
            while (m_ring2.available() < StereoRingBuffer::kTargetLatencyFrames
                   && waited < kWaitMs) {
                QThread::msleep(kStepMs);
                waited += kStepMs;
            }
        }
    }

    if (!openOutputOnly(outputDeviceId, m_sampleRate, bufferSize, errorOut)) {
        emit errorOccurred(errorOut);
        m_wasapi->close();
        if (m_wasapiMic && m_mixMode.load()) m_wasapiMic->close();
        m_mixMode.store(false);
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
    // Close secondary mic capture if it was active in "both" mode.
    if (m_wasapiMic) m_wasapiMic->close();
    m_mixMode.store(false);
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
// float frames into the ring buffer(s); we pop them, optionally mix the
// secondary mic ring (m_ring2) when m_mixMode is true, run processStereo(),
// and write the result to the PortAudio output buffer.
//
// "Both" mode mixing strategy:
//   We pop one frame from each ring and sum them directly.  The existing
//   compressor / limiter in the DSP chain handles any transient peaks that
//   result from additive mixing; using a 0.5× pre-attenuator instead would
//   make the loopback or mic feel "quieter" than when used alone, which is
//   confusing.  Users can adjust the master Volume slider if needed.
int AudioCapture::outCallback(const void*, void* outputBuffer,
                               unsigned long frameCount,
                               const PaStreamCallbackTimeInfo*,
                               PaStreamCallbackFlags,
                               void* userData) {
    auto* self = static_cast<AudioCapture*>(userData);
    auto* out  = static_cast<float*>(outputBuffer);

    // PortAudio invokes this callback on the same dedicated audio thread for
    // the life of the stream, so a function-local static is exactly once
    // per thread, with no extra per-block cost afterward.
    static thread_local bool denormalGuardEnabled = (enableDenormalFlushToZero(), true);
    (void)denormalGuardEnabled;

    // Actively pin queued latency before draining this block. Cheap (O(1)):
    // only ever does work when backlog has actually built up (startup gap or
    // clock drift between the WASAPI capture clock and this device's output
    // clock). This is what keeps end-to-end latency bounded to ~10-40ms
    // instead of silently growing and staying wherever it happened to drift.
    self->m_ring.trimToTargetLatency();
    const bool mixMode = self->m_mixMode.load();
    if (mixMode) self->m_ring2.trimToTargetLatency();

    for (unsigned long i = 0; i < frameCount; ++i) {
        float l = 0.f, r = 0.f;
        self->m_ring.pop(l, r);          // primary: loopback (or mic if mic-only)

        if (mixMode) {
            // "Both" mode: sum mic frames into the loopback frames.
            // The DSP limiter downstream prevents clipping from the additive mix.
            float ml = 0.f, mr = 0.f;
            self->m_ring2.pop(ml, mr);   // secondary: microphone
            l += ml;
            r += mr;
        }

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

    static thread_local bool denormalGuardEnabled = (enableDenormalFlushToZero(), true);
    (void)denormalGuardEnabled;

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
