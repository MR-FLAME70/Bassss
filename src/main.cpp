#include <QApplication>
#include <QIcon>
#include <QFont>
#include <QFontDatabase>
#include "mainwindow.h"

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

    MainWindow w;
    w.setWindowTitle("Bass Nuker");
    w.show();

    return app.exec();
}
