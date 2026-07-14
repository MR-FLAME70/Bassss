#pragma once
#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QComboBox>
#include "CustomWidgets.h"
#include "../settings.h"

class AudioProcessor;

// ──────────────────────────────────────────────────────────────────────────────
// FileTab — the "File Export" tab.
// Provides offline audio-file processing using the same DSP chain as the live
// mode. User opens an audio file, the entire chain runs in one pass, and the
// output is saved as a WAV. Matches converter.html/converter.js from the ext.
// ──────────────────────────────────────────────────────────────────────────────
class FileTab : public QWidget {
    Q_OBJECT
public:
    explicit FileTab(AudioProcessor* proc, QWidget* parent = nullptr);
    void refreshFromSettings(const AppSettings& s);

private slots:
    void onBrowseInput();
    void onBrowseOutput();
    void onProcess();

private:
    AudioProcessor* m_proc;
    AppSettings     m_settings;

    QLabel*       lblInputPath;
    QLabel*       lblOutputPath;
    QPushButton*  btnBrowseIn;
    QPushButton*  btnBrowseOut;
    QPushButton*  btnProcess;
    QProgressBar* progressBar;
    QLabel*       lblStatus;
    QComboBox*    comboFormat;   // WAV / WAV-float32 only (no ffmpeg dep)

    QString m_inputPath;
    QString m_outputPath;

    void buildUI();
    bool processFile();
};
