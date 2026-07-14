#include "FileTab.h"
#include "../audio/AudioProcessor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QApplication>
#include <fstream>
#include <cstring>
#include <vector>
#include <string>

FileTab::FileTab(AudioProcessor* proc, QWidget* parent)
    : QWidget(parent), m_proc(proc) {
    m_settings = globalSettings();
    buildUI();
}

void FileTab::buildUI() {
    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(16);
    lay->setContentsMargins(20,20,20,20);

    // Title
    lay->addWidget(makeLabel("File Export", 15, true));
    lay->addWidget(makeDimLabel(
        "Process an audio file through the same DSP chain as live mode.\n"
        "Input: 32-bit float WAV stereo   Output: 32-bit float WAV stereo"));

    // Input
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        cl->addWidget(makeLabel("Input File", 12, true));
        auto* row = new QHBoxLayout();
        lblInputPath = new QLabel("(none selected)");
        lblInputPath->setStyleSheet("color:#888; font-size:11px; background:#111; "
                                    "border:1px solid #222; border-radius:4px; padding:4px 8px;");
        btnBrowseIn  = new QPushButton("Browse…");
        btnBrowseIn->setStyleSheet(
            "QPushButton { background:#1a1a1a; color:#8b5cf6; border:1px solid #8b5cf6;"
            "border-radius:6px; padding:6px 14px; font-size:12px; }"
            "QPushButton:hover { background:#2a2a2a; }");
        row->addWidget(lblInputPath, 1);
        row->addWidget(btnBrowseIn);
        cl->addLayout(row);
        lay->addWidget(card);
    }

    // Output
    {
        auto* card = new DarkCard();
        auto* cl   = new QVBoxLayout(card);
        cl->addWidget(makeLabel("Output File", 12, true));
        auto* row = new QHBoxLayout();
        lblOutputPath = new QLabel("(none selected)");
        lblOutputPath->setStyleSheet("color:#888; font-size:11px; background:#111; "
                                     "border:1px solid #222; border-radius:4px; padding:4px 8px;");
        btnBrowseOut = new QPushButton("Browse…");
        btnBrowseOut->setStyleSheet(
            "QPushButton { background:#1a1a1a; color:#8b5cf6; border:1px solid #8b5cf6;"
            "border-radius:6px; padding:6px 14px; font-size:12px; }"
            "QPushButton:hover { background:#2a2a2a; }");
        row->addWidget(lblOutputPath, 1);
        row->addWidget(btnBrowseOut);
        cl->addLayout(row);
        lay->addWidget(card);
    }

    // Format note
    comboFormat = new QComboBox();
    comboFormat->addItem("32-bit float WAV (lossless)");
    comboFormat->setStyleSheet("QComboBox { background:#1a1a1a; color:#fff; border:1px solid #333;"
                                "border-radius:6px; padding:4px 8px; font-size:12px; }"
                                "QComboBox QAbstractItemView { background:#1a1a1a; color:#fff; "
                                "border:1px solid #333; selection-background-color:#8b5cf6; }");
    lay->addWidget(comboFormat);

    // Process
    btnProcess = new QPushButton("⚡ Process File");
    btnProcess->setStyleSheet(
        "QPushButton { background:#8b5cf6; color:#fff; border:none; border-radius:8px;"
        "padding:10px 24px; font-size:13px; font-weight:bold; }"
        "QPushButton:hover { background:#7c3aed; }"
        "QPushButton:disabled { background:#333; color:#666; }");
    lay->addWidget(btnProcess);

    progressBar = new QProgressBar();
    progressBar->setRange(0,100);
    progressBar->setValue(0);
    progressBar->setStyleSheet(
        "QProgressBar { background:#111; border:1px solid #222; border-radius:4px; color:#fff; }"
        "QProgressBar::chunk { background:#8b5cf6; border-radius:4px; }");
    lay->addWidget(progressBar);

    lblStatus = makeLabel("Ready", 11, false, "#888");
    lay->addWidget(lblStatus);

    lay->addStretch();

    // Connections
    connect(btnBrowseIn,  &QPushButton::clicked, this, &FileTab::onBrowseInput);
    connect(btnBrowseOut, &QPushButton::clicked, this, &FileTab::onBrowseOutput);
    connect(btnProcess,   &QPushButton::clicked, this, &FileTab::onProcess);
}

void FileTab::onBrowseInput() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Audio File",
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        "WAV Files (*.wav);;All Files (*)");
    if (path.isEmpty()) return;
    m_inputPath = path;
    lblInputPath->setText(path);
    // Auto-fill output
    if (m_outputPath.isEmpty()) {
        QString out = path;
        out.replace(QRegularExpression("\\.wav$", QRegularExpression::CaseInsensitiveOption),
                    "_processed.wav");
        if (out == path) out += "_processed.wav";
        m_outputPath = out;
        lblOutputPath->setText(out);
    }
}

void FileTab::onBrowseOutput() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Processed File",
        m_outputPath.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
            : m_outputPath,
        "WAV Files (*.wav);;All Files (*)");
    if (path.isEmpty()) return;
    m_outputPath = path;
    lblOutputPath->setText(path);
}

void FileTab::onProcess() {
    if (m_inputPath.isEmpty() || m_outputPath.isEmpty()) {
        QMessageBox::warning(this, "Bass Nuker",
            "Please select an input and output file first.");
        return;
    }
    btnProcess->setEnabled(false);
    lblStatus->setText("Processing…");
    lblStatus->setStyleSheet("color: #8b5cf6;");
    progressBar->setValue(0);

    bool ok = processFile();

    btnProcess->setEnabled(true);
    progressBar->setValue(ok ? 100 : 0);
    lblStatus->setText(ok ? "Done! File saved." : "Error — check input file format.");
    lblStatus->setStyleSheet(ok ? "color: #22c55e;" : "color: #ef4444;");
}

void FileTab::refreshFromSettings(const AppSettings& s) {
    m_settings = s;
}

// ──────────────────────────────────────────────────────────────────────────────
// Offline DSP processing — reads IEEE float32 WAV, runs the DSP chain sample
// by sample (same signal chain as live mode), writes output WAV.
// ──────────────────────────────────────────────────────────────────────────────
bool FileTab::processFile() {
    // ── Read input WAV (simple 32-bit float stereo parser) ────────────────────
    std::ifstream fin(m_inputPath.toStdString(), std::ios::binary);
    if (!fin) return false;

    char fourcc[5] = {};
    fin.read(fourcc, 4);
    if (std::string(fourcc,4) != "RIFF") return false;
    uint32_t chunkSize; fin.read(reinterpret_cast<char*>(&chunkSize), 4);
    char wave[4]; fin.read(wave, 4);
    if (std::string(wave,4) != "WAVE") return false;

    uint16_t audioFormat=0, numChannels=0, bitsPerSample=0;
    uint32_t sampleRate=0, dataSize=0;
    bool foundData = false;

    while (!fin.eof() && !foundData) {
        char id[4]; fin.read(id,4);
        uint32_t sz; fin.read(reinterpret_cast<char*>(&sz),4);
        if (std::string(id,4) == "fmt ") {
            fin.read(reinterpret_cast<char*>(&audioFormat),2);
            fin.read(reinterpret_cast<char*>(&numChannels),2);
            fin.read(reinterpret_cast<char*>(&sampleRate),4);
            uint32_t byteRate; fin.read(reinterpret_cast<char*>(&byteRate),4);
            uint16_t blockAlign; fin.read(reinterpret_cast<char*>(&blockAlign),2);
            fin.read(reinterpret_cast<char*>(&bitsPerSample),2);
            // Skip remaining fmt bytes
            if (sz > 16) fin.seekg(sz-16, std::ios::cur);
        } else if (std::string(id,4) == "data") {
            dataSize = sz;
            foundData = true;
        } else {
            fin.seekg(sz, std::ios::cur);
        }
    }

    if (!foundData) return false;
    // Only IEEE float 32-bit stereo supported natively (no libsndfile dep)
    if (audioFormat != 3 || bitsPerSample != 32) {
        QMessageBox::warning(nullptr, "Bass Nuker",
            "Only 32-bit float stereo WAV input is supported.\n"
            "Convert your file first (e.g. with Audacity → Export → 32-bit float WAV).");
        return false;
    }
    if (numChannels != 2) {
        QMessageBox::warning(nullptr, "Bass Nuker",
            "Only stereo (2-channel) WAV input is supported.");
        return false;
    }

    uint32_t numSamples = dataSize / sizeof(float);
    std::vector<float> inBuf(numSamples);
    fin.read(reinterpret_cast<char*>(inBuf.data()), dataSize);

    // Configure a fresh, identical DSP chain for offline processing
    AudioProcessor offlineProc;
    offlineProc.setSampleRate(sampleRate);
    offlineProc.applySettings(m_settings);
    offlineProc.setEnabled(true);

    // Process
    std::vector<float> outBuf(numSamples);
    uint32_t frames = numSamples / 2;
    for (uint32_t i = 0; i < frames; ++i) {
        float l = inBuf[i*2];
        float r = inBuf[i*2+1];
        offlineProc.processStereo(l, r);
        outBuf[i*2]   = l;
        outBuf[i*2+1] = r;
        if (i % 44100 == 0) {
            int pct = (int)((float)i / frames * 100.f);
            progressBar->setValue(pct);
            QApplication::processEvents();
        }
    }

    // Write output WAV
    std::ofstream fout(m_outputPath.toStdString(), std::ios::binary);
    if (!fout) return false;

    uint32_t outDataSz = numSamples * sizeof(float);
    uint32_t outChunkSz = 36 + outDataSz;
    auto w4 = [&](uint32_t v){ fout.write(reinterpret_cast<const char*>(&v),4); };
    auto w2 = [&](uint16_t v){ fout.write(reinterpret_cast<const char*>(&v),2); };

    fout.write("RIFF",4); w4(outChunkSz);
    fout.write("WAVE",4);
    fout.write("fmt ",4); w4(18);
    w2(3); w2(2); w4(sampleRate);
    w4(sampleRate*2*4); w2(8); w2(32); w2(0);
    fout.write("data",4); w4(outDataSz);
    fout.write(reinterpret_cast<const char*>(outBuf.data()), outDataSz);
    return fout.good();
}
