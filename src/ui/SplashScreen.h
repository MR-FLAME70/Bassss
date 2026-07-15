#pragma once
#include <QWidget>
#include <QPixmap>
#include <functional>

// ──────────────────────────────────────────────────────────────────────────────
// SplashScreen — centered, borderless, fully transparent splash shown while
// MainWindow is constructed (device enumeration, DSP graph setup, UI build).
//
// Only the app logo is visible — no background panel, no text, no chrome.
// Both animated properties (logoOpacity, logoScale) are painted directly via
// QPainter transforms in paintEvent() rather than through a QGraphicsOpacityEffect
// or by re-scaling/re-rendering the pixmap every frame: painter.setOpacity()
// and painter.scale() are cheap per-frame operations on an already-rasterized
// pixmap, and the resulting frames are composited by the platform's window
// compositor (DWM on Windows) — the same GPU-backed alpha-blit path a native
// translucent window uses for any other content. That combination is the
// practical ceiling for "GPU-accelerated" animation in a QWidget app without
// pulling in a full QOpenGLWidget/QGraphicsView stack for a ~250ms splash.
// ──────────────────────────────────────────────────────────────────────────────
class SplashScreen : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal logoOpacity READ logoOpacity WRITE setLogoOpacity)
    Q_PROPERTY(qreal logoScale   READ logoScale   WRITE setLogoScale)

public:
    explicit SplashScreen(QWidget* parent = nullptr);

    qreal logoOpacity() const { return m_opacity; }
    void  setLogoOpacity(qreal o) { m_opacity = o; update(); }

    qreal logoScale() const { return m_scale; }
    void  setLogoScale(qreal s) { m_scale = s; update(); }

    // Centers on the screen containing the cursor, then plays the fade+scale
    // in animation (opacity 0→1, scale 90%→100%).
    void playIntro();

    // Plays the fade-out (opacity 1→0, no further scale change) and calls
    // `onFinished` once the animation completes, then closes/deletes itself.
    void playOutro(std::function<void()> onFinished);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap m_logo;
    qreal   m_opacity = 0.0;
    qreal   m_scale   = 0.90;
};
