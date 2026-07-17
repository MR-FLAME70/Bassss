#include "AudioCapture.h"
#include "AudioProcessor.h"
#include "AudioDiagnostics.h"
#include <cstring>
#include <cstdio>   // std::remove
#include <algorithm>
#include <QDebug>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#include <pmmintrin.h>
#define BASSNUKER_HAS_SSE_DENORMAL_CONTROL 1
#endif

// ──────────────────────────────────────────────────────────────────────────────
// Denormal (subnormal) float protection for the audio thread.
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
// Windows signal flow:
//   [WASAPI CaptureWorker thread]
//       WASAPI loopback/mic → convertFrame → raw float frames → StereoRingBuffer
//   [PortAudio high-priority audio thread]
//       StereoRingBuffer → pop raw frame → AudioProcessor::processStereo → output
//
// "Both" mode adds a second WASAPI capture for the microphone in parallel:
//   [WASAPI CaptureWorker — loopback]  → m_ring
//   [WASAPI CaptureWorker — mic]       → m_ring2
//   [PortAudio audio thread]
//       pops from both rings, applies mic gain to mic frames ONLY,
//       sums frames, runs DSP.
//
// The mic gain is applied here (in outCallback) when in "both" mode so that
// the Mic Volume slider scales only the microphone signal and not the loopback.
// In mic-only mode the gain is applied inside AudioProcessor::processStereo
// (the mic IS the primary ring signal there). In playback mode the gain is
// not applied at all (no mic in the signal).
// ──────────────────────────────────────────────────────────────────────────────

AudioCapture::AudioCapture(AudioProcessor* proc, QObject* parent)
    : QObject(parent), m_proc(proc) {
    Pa_Initialize();

#ifdef _WIN32
    m_wasapi = new WASAPICapture(m_ring, this);
    connect(m_wasapi, &WASAPICapture::errorOccurred,
            this,     &AudioCapture::errorOccurred);
#endif

    // Flush the diagnostic ring to disk once per second (UI thread).
    m_diagTimer = new QTimer(this);
    m_diagTimer->setInterval(1000);
    connect(m_diagTimer, &QTimer::timeout, this, &AudioCapture::flushDiagnostics);
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
    if (!m_wasapi->open(inputDeviceId, inputType, sampleRate, errorOut)) {
        if (!errorOut.isEmpty())
            emit errorOccurred(errorOut);
        return false;
    }
    m_sampleRate = m_wasapi->actualSampleRate();
    m_proc->setSampleRate(m_sampleRate);

    m_mixMode.store(false);
    if (!micDeviceId.empty()) {
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
            emit errorOccurred(
                QString("Mic could not be opened (loopback-only fallback): %1").arg(micErr));
        }
    }

    {
        constexpr int kWaitMs  = 3000;
        constexpr int kStepMs  = 5;
        int waited = 0;
        while (m_ring.available() < StereoRingBuffer::kTargetLatencyFrames
               && waited < kWaitMs) {
            QThread::msleep(kStepMs);
            waited += kStepMs;
        }
        if (m_mixMode.load()) {
            waited = 0;
            while (m_ring2.available() < StereoRingBuffer::kTargetLatencyFrames
                   && waited < kWaitMs) {
                QThread::msleep(kStepMs);
                waited += kStepMs;
            }
        }

        m_ring.resetToTargetLatency();
        if (m_mixMode.load()) m_ring2.resetToTargetLatency();
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
    m_diagTimer->stop();

#ifdef _WIN32
    if (m_wasapi) m_wasapi->close();
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

    // Final flush — captures any events that occurred since the last timer tick.
    flushDiagnostics();
    qDebug() << "[AudioDiag] Final log written to"
             << QString::fromStdString(diagLogPath());
}

// ──────────────────────────────────────────────────────────────────────────────
// Diagnostics helpers
// ──────────────────────────────────────────────────────────────────────────────
std::string AudioCapture::diagLogPath() {
    const char* tmp = nullptr;
#ifdef _WIN32
    tmp = getenv("TEMP");
    if (!tmp || !*tmp) tmp = getenv("TMP");
#endif
    std::string dir = (tmp && *tmp) ? tmp : ".";
    return dir + "/bassnuker_audio_diag.csv";
}

void AudioCapture::flushDiagnostics() {
    AudioDiagnostics::instance().flushToLog(diagLogPath());
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

    // ── Initialise diagnostics before the stream fires its first callback ─────
    {
        std::string logPath = diagLogPath();
        std::remove(logPath.c_str()); // delete stale log from a previous run
        AudioDiagnostics::instance().reset(sampleRate, bufferSize);
        qDebug() << "[AudioDiag] Logging to" << QString::fromStdString(logPath)
                 << "  budget=" << (int)(1e6 * bufferSize / sampleRate) << "us"
                 << "  sr=" << (int)sampleRate
                 << "  framesPerCb=" << bufferSize;
    }

    err = Pa_StartStream(m_outStream);
    if (err != paNoError) {
        Pa_CloseStream(m_outStream); m_outStream = nullptr;
        errorOut = QString("Pa_StartStream (out): %1").arg(Pa_GetErrorText(err));
        return false;
    }

    m_diagTimer->start();
    return true;
}

// ── Windows output callback — runs on the PortAudio real-time audio thread ────
//
// INSTRUMENTED: every path that can cause a glitch is measured and logged:
//   • PortAudio statusFlags  → XRUN (hardware underflow / overflow)
//   • trimToTargetLatency()  → frames skipped = phase discontinuity = click
//   • pop() returning false  → ring empty = silence injected = click
//   • total callback time vs budget → over-budget = next-callback underrun
//   • sub-time of consumePendingSettings (via thread-local accumulators)
//
// All logging is lock-free: atomic increments + memcpy into a pre-allocated
// ring buffer inside AudioDiagnostics. No malloc, no mutex, no I/O here.
//
int AudioCapture::outCallback(const void*, void* outputBuffer,
                               unsigned long frameCount,
                               const PaStreamCallbackTimeInfo* /*timeInfo*/,
                               PaStreamCallbackFlags statusFlags,
                               void* userData) {
    auto* self = static_cast<AudioCapture*>(userData);
    auto* out  = static_cast<float*>(outputBuffer);
    auto& diag = AudioDiagnostics::instance();

    static thread_local bool denormalGuardEnabled = (enableDenormalFlushToZero(), true);
    (void)denormalGuardEnabled;

    // ── Callback sequence number + thread-local sub-timing reset ─────────────
    uint32_t seq = diag.callbackCount.fetch_add(1, std::memory_order_relaxed);
    diag.beginCallback(seq);
    int64_t cbStart = diag.nowNs();

    // ── 1. PortAudio XRUN detection ───────────────────────────────────────────
    // Previously this parameter was unnamed and silently discarded.
    // paOutputUnderflow means the hardware ran out of data (heard as silence/pop).
    // paInputOverflow   means the capture side overflowed (data lost upstream).
    if (statusFlags != 0)
        diag.logXrun(seq, (unsigned int)statusFlags);

    // ── 2. Ring fill snapshot (BEFORE trim) ───────────────────────────────────
    // Captured here so the log shows exactly how full the ring was when we
    // decided whether to trim.
    int fillBefore = self->m_ring.available();

    // ── 3. Gradual latency trim ───────────────────────────────────────────────
    // trimToTargetLatency() advances the read pointer by up to kTrimStepFrames
    // (256) when queued fill exceeds kMaxLatencyFrames (9600 = ~200 ms).
    // The skipped frames are NEVER processed — they create a phase/amplitude
    // discontinuity = click. Now returns the number of frames actually skipped.
    int trimmed = self->m_ring.trimToTargetLatency();
    if (trimmed > 0)
        diag.logTrim(seq, trimmed, fillBefore);

    // Log push-drops accumulated by the capture thread since the last callback.
    // push() increments pushDropCount atomically when the ring is full; we
    // exchange it to 0 here (audio thread) to drain it non-destructively.
    {
        uint32_t drops = self->m_ring.pushDropCount.exchange(0, std::memory_order_relaxed);
        if (drops > 0)
            diag.logPushDrop((int)drops, fillBefore);
    }

    const bool mixMode = self->m_mixMode.load();
    if (mixMode) {
        int fill2    = self->m_ring2.available();
        int trimmed2 = self->m_ring2.trimToTargetLatency();
        if (trimmed2 > 0)
            diag.logTrim(seq, trimmed2, fill2);
        uint32_t drops2 = self->m_ring2.pushDropCount.exchange(0, std::memory_order_relaxed);
        if (drops2 > 0)
            diag.logPushDrop((int)drops2, fill2);
    }

    // Read mic gain once per block (atomic load, cheap).
    const float micGainVal = mixMode ? self->m_proc->getMicGainAtomic() : 1.f;

    // ── 4. Per-frame DSP loop — timed as a whole ──────────────────────────────
    // processStereo() calls consumePendingSettings() every 128 samples, which
    // accumulates its own sub-timings into thread-local vars via diag.accXxx().
    int underruns = 0;
    int64_t dspStart = diag.nowNs();

    for (unsigned long i = 0; i < frameCount; ++i) {
        float l = 0.f, r = 0.f;

        // pop() returns false (and outputs silence) when the ring is empty.
        // Silence injection = amplitude step = click.
        if (!self->m_ring.pop(l, r)) {
            ++underruns;
            if (underruns == 1) // log first occurrence; avoid flooding the ring
                diag.logUnderrun(seq, (int)i, self->m_ring.available());
        }

        if (mixMode) {
            // "Both" mode: scale mic frames by mic gain BEFORE summing with
            // loopback so the Mic Volume slider controls only the mic signal.
            float ml = 0.f, mr = 0.f;
            self->m_ring2.pop(ml, mr);
            l += ml * micGainVal;
            r += mr * micGainVal;
        }

        self->m_proc->processStereo(l, r);
        out[i*2]   = l;
        out[i*2+1] = r;
    }

    // ── 5. Callback summary ───────────────────────────────────────────────────
    int totalUs = (int)((diag.nowNs() - cbStart) / 1000);
    (void)dspStart; // dspUs ≈ totalUs minus the tiny overhead above the loop
    diag.endCallback(seq, totalUs, fillBefore, underruns);

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
