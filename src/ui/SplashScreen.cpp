#include "SplashScreen.h"
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QEasingCurve>

namespace {
constexpr int kSize        = 220;  // widget size — margin around the logo so
                                    // scale/shadow never clips at the edge
constexpr int kLogoSize    = 132;  // drawn logo size at 100% scale
constexpr int kIntroMs     = 320;  // fade+scale in duration
constexpr int kOutroMs     = 260;  // fade out duration
} // namespace

SplashScreen::SplashScreen(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFixedSize(kSize, kSize);

    m_logo = QPixmap(":/icons/icon128.png");
}

void SplashScreen::playIntro() {
    // Center on whichever screen currently has the cursor — matches where
    // the user actually launched the app from on multi-monitor setups.
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        move(avail.center().x() - width() / 2,
             avail.center().y() - height() / 2);
    }

    show();
    raise();

    auto* fade = new QPropertyAnimation(this, "logoOpacity", this);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setDuration(kIntroMs);
    fade->setEasingCurve(QEasingCurve::OutCubic);

    auto* scale = new QPropertyAnimation(this, "logoScale", this);
    scale->setStartValue(0.90);
    scale->setEndValue(1.0);
    scale->setDuration(kIntroMs);
    // OutBack overshoots very slightly past 100% before settling, which
    // reads as a soft, modern "pop" rather than a mechanical linear grow.
    scale->setEasingCurve(QEasingCurve::OutBack);

    fade->start(QAbstractAnimation::DeleteWhenStopped);
    scale->start(QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::playOutro(std::function<void()> onFinished) {
    auto* fade = new QPropertyAnimation(this, "logoOpacity", this);
    fade->setStartValue(m_opacity);
    fade->setEndValue(0.0);
    fade->setDuration(kOutroMs);
    fade->setEasingCurve(QEasingCurve::InCubic);

    connect(fade, &QPropertyAnimation::finished, this, [this, onFinished]() {
        if (onFinished) onFinished();
        close(); // WA_DeleteOnClose handles cleanup
    });

    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::paintEvent(QPaintEvent*) {
    if (m_logo.isNull()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(m_opacity);

    const QPointF center(width() / 2.0, height() / 2.0);
    p.translate(center);
    p.scale(m_scale, m_scale);
    p.translate(-center);

    const QRectF target(center.x() - kLogoSize / 2.0,
                         center.y() - kLogoSize / 2.0,
                         kLogoSize, kLogoSize);
    p.drawPixmap(target, m_logo, QRectF(m_logo.rect()));
}
