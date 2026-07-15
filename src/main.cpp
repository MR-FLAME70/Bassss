#include <QApplication>
#include <QIcon>
#include <QFont>
#include <QFontDatabase>
#include <QElapsedTimer>
#include <QTimer>
#include <algorithm>
#include <memory>
#include "mainwindow.h"
#include "ui/SplashScreen.h"

// ──────────────────────────────────────────────────────────────────────────────
// Startup sequence
//
//   1. Show the splash screen immediately (logo fades + scales in, 90%→100%).
//   2. On the next event-loop iteration (so the splash has actually been
//      painted), construct MainWindow. This is where all the real init work
//      happens — device enumeration, AudioProcessor/AudioCapture setup, the
//      full UI tree — while the splash is on screen and animating, so init
//      is never delayed waiting on the splash and the splash never blocks on
//      init either.
//   3. Once MainWindow is built, wait out whatever's left of the ~900ms
//      splash hold time (skipped entirely if init already took longer), then
//      cross-fade: splash fades out while MainWindow fades + scales in
//      (97%→100%) at the same time. Nothing ever "pops" into view.
// ──────────────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // High-DPI support
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Round);

    QApplication app(argc, argv);
    app.setApplicationName("BassNuker");
    app.setApplicationDisplayName("Bass Nuker");
    app.setApplicationVersion("6.9.0");
    app.setOrganizationName("BassNuker");
    app.setOrganizationDomain("com.bassnuker");

    // Set window icon
    app.setWindowIcon(QIcon(":/icons/icon128.png"));

    // Use Segoe UI if available (Windows), otherwise fall back to a clean
    // modern sans-serif so the UI reads consistently across platforms.
    QFont font("Segoe UI", 11);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(font);

    constexpr int kMinSplashMs = 900; // within the requested ~800-1200ms range

    auto* splash = new SplashScreen();
    splash->playIntro();

    auto splashTimer = std::make_shared<QElapsedTimer>();
    splashTimer->start();

    // Defer MainWindow construction to the next event-loop turn so the
    // splash's first frame is actually flushed to the screen before any
    // heavy init work runs on the same (single, UI) thread.
    QTimer::singleShot(0, [&app, splash, splashTimer]() {
        auto* w = new MainWindow();
        w->setAttribute(Qt::WA_DeleteOnClose, true);
        w->setWindowTitle("Bass Nuker");
        QObject::connect(&app, &QApplication::aboutToQuit, w, [w]() { w->close(); });

        const qint64 elapsed   = splashTimer->elapsed();
        const qint64 remaining = std::max<qint64>(0, kMinSplashMs - elapsed);

        QTimer::singleShot(remaining, [splash, w]() {
            splash->playOutro([w]() { w->playIntroAnimation(); });
        });
    });

    return app.exec();
}
