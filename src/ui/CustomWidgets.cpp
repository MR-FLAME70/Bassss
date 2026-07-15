#include "CustomWidgets.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QEasingCurve>
#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// ToggleSwitch
// ──────────────────────────────────────────────────────────────────────────────
namespace {
// QColor's constructor isn't constexpr, so these are plain const globals.
const QColor kAccentGreen      (0x22, 0xc5, 0x5e); // functional "engaged" accent, reused from status color
const QColor kAccentGreenLight (0x4a, 0xde, 0x80);
const QColor kOffTrack         (0x1a, 0x1a, 0x1a);
const QColor kOffBorder        (0x33, 0x33, 0x33);
const QColor kOffThumb         (0x5a, 0x5a, 0x5a);  // dim, deliberately "unlit"
}

ToggleSwitch::ToggleSwitch(QWidget* parent) : QWidget(parent) {
    setFixedSize(44, 24);
    setCursor(Qt::PointingHandCursor);

    m_posAnim = new QPropertyAnimation(this, "thumbPos", this);
    m_posAnim->setDuration(200);
    m_posAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_progAnim = new QPropertyAnimation(this, "onProgress", this);
    m_progAnim->setDuration(200);
    m_progAnim->setEasingCurve(QEasingCurve::OutCubic);

    // Soft green bloom around the whole switch when engaged. Kept at zero
    // alpha (invisible) while off so the "glow" reads as something that
    // switches on, not a permanent halo.
    m_glow = new QGraphicsDropShadowEffect(this);
    m_glow->setColor(QColor(kAccentGreen.red(), kAccentGreen.green(), kAccentGreen.blue(), 0));
    m_glow->setBlurRadius(16);
    m_glow->setOffset(0, 0);
    setGraphicsEffect(m_glow);
}

void ToggleSwitch::setChecked(bool on) {
    if (m_checked == on) return;
    m_checked = on;

    m_posAnim->stop();
    m_posAnim->setStartValue(m_thumbPos);
    m_posAnim->setEndValue(on ? 1.0 : 0.0);
    m_posAnim->start();

    m_progAnim->stop();
    m_progAnim->setStartValue(m_onProgress);
    m_progAnim->setEndValue(on ? 1.0 : 0.0);
    m_progAnim->start();

    emit toggled(on);
}

void ToggleSwitch::setOnProgress(qreal t) {
    m_onProgress = t;
    // Subtle by design — max alpha ~90 keeps it a soft bloom, not a halo.
    int alpha = int(90 * t);
    m_glow->setColor(QColor(kAccentGreen.red(), kAccentGreen.green(), kAccentGreen.blue(), alpha));
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal t = m_onProgress;
    auto lerp = [t](int a, int b) { return a + int((b - a) * t); };

    // Track: crossfades from flat unlit dark to a filled green gradient as
    // onProgress goes 0 -> 1, so ON/OFF read as distinctly different states
    // rather than a shade of the same gray.
    QColor borderColor(
        lerp(kOffBorder.red(),   kAccentGreenLight.red()),
        lerp(kOffBorder.green(), kAccentGreenLight.green()),
        lerp(kOffBorder.blue(),  kAccentGreenLight.blue()));

    QRectF trackRect(0.5, 4.5, 43, 15);
    p.setPen(QPen(borderColor, 1));
    // Flat unlit base, then a green gradient fades in on top of it (alpha
    // rises with onProgress) — draws as a clean crossfade between the two
    // distinct looks rather than a muddy gray-to-green blend.
    p.setBrush(kOffTrack);
    p.drawRoundedRect(trackRect, 7.5, 7.5);
    if (t > 0.001) {
        QColor top = kAccentGreenLight; top.setAlphaF(t);
        QColor bot = kAccentGreen;      bot.setAlphaF(t);
        QLinearGradient fadeGrad(trackRect.topLeft(), trackRect.bottomLeft());
        fadeGrad.setColorAt(0.0, top);
        fadeGrad.setColorAt(1.0, bot);
        p.setBrush(fadeGrad);
        p.drawRoundedRect(trackRect, 7.5, 7.5);
    }

    // Thumb position: 4..22 px, matches the original travel range.
    qreal thumbX = 4.0 + (22.0 - 4.0) * m_thumbPos;

    // Thumb — subtle drop shadow for depth, face crossfades dim-gray -> white.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(QRectF(thumbX, 3, 20, 20));

    QColor thumbColor(
        lerp(kOffThumb.red(),   255),
        lerp(kOffThumb.green(), 255),
        lerp(kOffThumb.blue(),  255));
    p.setBrush(thumbColor);
    p.drawEllipse(QRectF(thumbX, 2, 20, 20));
}

void ToggleSwitch::mousePressEvent(QMouseEvent*) {
    setChecked(!m_checked);
}

// ──────────────────────────────────────────────────────────────────────────────
// DarkSlider
// ──────────────────────────────────────────────────────────────────────────────
DarkSlider::DarkSlider(Qt::Orientation o, QWidget* parent) : QSlider(o, parent) {
    // Recessed-channel fader look: the groove sits visibly darker/bordered
    // than the surrounding card so it reads clearly against the background,
    // the filled portion uses a brighter silver so progress is unambiguous
    // at a glance, and the handle stays crisp white with a dark rim.
    // Thin, slightly-lit borders (vs. the previous near-black #050505) so
    // each fader reads as a distinct bordered control against the card
    // background — the FabFilter/iZotope "recessed strip" look — without
    // adding visual weight.
    setStyleSheet(R"(
        QSlider::groove:horizontal {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                        stop:0 #0a0a0a, stop:1 #1a1a1a);
            height: 5px; border-radius: 2px;
            border: 1px solid #2a2a2a;
        }
        QSlider::groove:vertical {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                        stop:0 #0a0a0a, stop:1 #1a1a1a);
            width: 5px; border-radius: 2px;
            border: 1px solid #2a2a2a;
        }
        QSlider::handle:horizontal {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                        stop:0 #f5f5f5, stop:1 #d4d4d4);
            width: 15px; height: 15px;
            border: 1px solid #000000; border-radius: 8px; margin: -6px 0;
        }
        QSlider::handle:vertical {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                        stop:0 #f5f5f5, stop:1 #d4d4d4);
            width: 15px; height: 15px;
            border: 1px solid #000000; border-radius: 8px; margin: 0 -6px;
        }
        QSlider::handle:hover    { background: #ffffff; border: 1px solid #000000; }
        QSlider::handle:pressed  { background: #c0c0c0; border: 1px solid #000000; }
        QSlider::sub-page:horizontal {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                        stop:0 #cfcfcf, stop:1 #9a9a9a);
            border: 1px solid #2a2a2a; border-radius: 2px;
        }
        QSlider::add-page:vertical {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                        stop:0 #cfcfcf, stop:1 #9a9a9a);
            border: 1px solid #2a2a2a; border-radius: 2px;
        }
        QSlider::add-page:horizontal {
            background: #101010; border: 1px solid #2a2a2a; border-radius: 2px;
        }
        QSlider::sub-page:vertical {
            background: #101010; border: 1px solid #2a2a2a; border-radius: 2px;
        }
    )");
    setRange(0, 10000);
}

void DarkSlider::setRangeF(double lo, double hi, double step) {
    flo = lo; fhi = hi; fstep = step;
    setRange(0, (int)std::round((hi-lo)/step));
}

void DarkSlider::setValueF(double v) {
    int iv = (int)std::round((v-flo)/fstep);
    setValue(iv);
}

double DarkSlider::valueF() const {
    return flo + value() * fstep;
}

void DarkSlider::paintEvent(QPaintEvent* e) {
    QSlider::paintEvent(e);
}

// ──────────────────────────────────────────────────────────────────────────────
// DarkKnob
// ──────────────────────────────────────────────────────────────────────────────
DarkKnob::DarkKnob(QWidget* parent) : QWidget(parent) {
    setFixedSize(60,60);
    setCursor(Qt::SizeVerCursor);
}

void DarkKnob::setValue(double v) {
    val = std::max(vlo, std::min(vhi, v));
    update();
    emit valueChanged(val);
}

void DarkKnob::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF rect(4,4,52,52);
    // Track
    p.setPen(QPen(QColor(0x2a,0x2a,0x2a), 5));
    p.setBrush(Qt::NoBrush);
    p.drawArc(rect, 225*16, -270*16);

    // Value arc
    double norm = (vhi>vlo) ? (val-vlo)/(vhi-vlo) : 0.0;
    p.setPen(QPen(QColor(0xe8,0xe8,0xe8), 5));
    p.drawArc(rect, 225*16, (int)(-norm*270.0*16));

    // Centre
    p.setBrush(QColor(0x17,0x17,0x17));
    p.setPen(QPen(QColor(0x2a,0x2a,0x2a), 1));
    p.drawEllipse(rect.adjusted(8,8,-8,-8));

    // Indicator line
    double angle = (225.0 - norm*270.0) * M_PI/180.0;
    double cx = rect.center().x(), cy = rect.center().y(), r = 18.0;
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(QPointF(cx, cy),
               QPointF(cx + r*std::cos(angle), cy - r*std::sin(angle)));
}

void DarkKnob::mousePressEvent(QMouseEvent* e) {
    dragStart    = e->pos();
    dragStartVal = val;
}

void DarkKnob::mouseMoveEvent(QMouseEvent* e) {
    double dy = dragStart.y() - e->pos().y();
    double range = vhi - vlo;
    setValue(dragStartVal + dy * range / 200.0);
}

// ──────────────────────────────────────────────────────────────────────────────
// VUMeter
// ──────────────────────────────────────────────────────────────────────────────
VUMeter::VUMeter(QWidget* parent) : QWidget(parent) {
    setFixedHeight(20);
    setMinimumWidth(100);
}

void VUMeter::setLevels(float rmsL, float rmsR, float peak, bool clip) {
    m_rmsL=rmsL; m_rmsR=rmsR; m_peak=peak; m_clip=clip;
    update();
}

void VUMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    int w = width(), h = height();

    p.fillRect(0,0,w,h, QColor(0x11,0x11,0x11));

    auto drawBar = [&](float level, int y, int barH) {
        // Clamp to [0,1]
        float l = std::max(0.f, std::min(1.f, level));
        int   filled = (int)(l * w);
        // Gradient: green → yellow → red
        QLinearGradient grad(0,0,w,0);
        grad.setColorAt(0.0, QColor(0x22,0xc5,0x5e));
        grad.setColorAt(0.7, QColor(0xf5,0x9e,0x0b));
        grad.setColorAt(1.0, QColor(0xef,0x44,0x44));
        p.fillRect(0, y, filled, barH, grad);
        p.fillRect(filled, y, w-filled, barH, QColor(0x22,0x22,0x22));
    };

    drawBar(m_rmsL, 1,       h/2-2);
    drawBar(m_rmsR, h/2+1,   h/2-2);

    // Peak hold tick
    if (m_peak > 0.f) {
        int px = (int)(m_peak * w);
        p.fillRect(std::min(px, w-2), 1, 2, h-2,
                   m_clip ? QColor(0xef,0x44,0x44) : Qt::white);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// SpectrumWidget
// ──────────────────────────────────────────────────────────────────────────────
SpectrumWidget::SpectrumWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(60);
}

void SpectrumWidget::setBins(const std::vector<float>& bins) {
    m_bins = bins;
    update();
}

void SpectrumWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    int w=width(), h=height();
    p.fillRect(0,0,w,h,QColor(0x0d,0x0d,0x0d));

    if (m_bins.empty()) return;

    // Logarithmic frequency mapping
    int    n = (int)m_bins.size();
    float  maxV = 0.f;
    for (float v : m_bins) maxV = std::max(maxV, v);
    if (maxV < 1e-8f) maxV = 1.f;

    QLinearGradient grad(0,0,0,h);
    grad.setColorAt(0.0, QColor(0x8b,0x5c,0xf6));
    grad.setColorAt(1.0, QColor(0x6d,0x28,0xd9,0x80));
    p.setBrush(grad);
    p.setPen(Qt::NoPen);

    QPainterPath path;
    path.moveTo(0, h);
    for (int x = 0; x < w; ++x) {
        float frac = (float)x / w;
        int binIdx = (int)(frac * n);
        binIdx = std::min(binIdx, n-1);
        float norm = std::log1p(m_bins[binIdx] / maxV * 9.f) / std::log(10.f);
        float yPix = h - norm * h;
        if (x==0) path.moveTo(0, yPix);
        else       path.lineTo(x, yPix);
    }
    path.lineTo(w, h);
    path.closeSubpath();
    p.drawPath(path);
}

// ──────────────────────────────────────────────────────────────────────────────
// CollapsibleSection
// ──────────────────────────────────────────────────────────────────────────────
CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QWidget(parent), m_title(title)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16,14,16,14);
    outer->setSpacing(10);

    // ── Header: title + Enable/Disable switch. Always visible. ────────────────
    m_headerLayout = new QHBoxLayout();
    auto* titleLbl = makeLabel(title, 12, true);
    m_toggle = new ToggleSwitch();
    m_headerLayout->addWidget(titleLbl);
    m_headerLayout->addStretch();
    m_headerLayout->addWidget(m_toggle);
    outer->addLayout(m_headerLayout);

    // ── Body: everything the caller adds via content() — hidden when off. ─────
    m_content = new QWidget(this);
    m_content->setMaximumHeight(0);
    m_content->setVisible(false);
    outer->addWidget(m_content);

    m_anim = new QPropertyAnimation(m_content, "maximumHeight", this);
    m_anim->setDuration(220);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QPropertyAnimation::finished, this, [this]{
        if (m_expanded) m_content->setMaximumHeight(QWIDGETSIZE_MAX);
        else             m_content->setVisible(false);
    });

    connect(m_toggle, &ToggleSwitch::toggled, this, [this](bool on){
        setExpanded(on, true);
        emit toggled(on);
    });
}

void CollapsibleSection::setExpanded(bool expanded, bool animate) {
    m_expanded = expanded;
    if (m_toggle->isChecked() != expanded) m_toggle->setChecked(expanded);
    m_anim->stop();

    if (expanded) {
        m_content->setVisible(true);
        int target = m_content->layout() ? m_content->layout()->sizeHint().height()
                                          : m_content->sizeHint().height();
        if (animate) {
            m_anim->setEasingCurve(QEasingCurve::OutCubic);
            m_anim->setStartValue(m_content->maximumHeight() > 0 ? m_content->maximumHeight() : 0);
            m_anim->setEndValue(target);
            m_anim->start();
        } else {
            m_content->setMaximumHeight(QWIDGETSIZE_MAX);
        }
    } else {
        int start = m_content->maximumHeight();
        if (start <= 0 || start == QWIDGETSIZE_MAX)
            start = m_content->layout() ? m_content->layout()->sizeHint().height()
                                         : m_content->sizeHint().height();
        if (animate) {
            m_anim->setEasingCurve(QEasingCurve::InCubic);
            m_anim->setStartValue(start);
            m_anim->setEndValue(0);
            m_anim->start();
        } else {
            m_content->setMaximumHeight(0);
            m_content->setVisible(false);
        }
    }
    update();
}

void CollapsibleSection::paintEvent(QPaintEvent*) {
    // Same rounded-card chrome as DarkCard, so sections still read as cards.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 10, 10);
    p.fillPath(path, QColor(0x12,0x12,0x12));
    p.setPen(QPen(QColor(0x28,0x28,0x28), 1));
    p.drawPath(path);
}

// ──────────────────────────────────────────────────────────────────────────────
// DarkCard
// ──────────────────────────────────────────────────────────────────────────────
DarkCard::DarkCard(QWidget* parent) : QWidget(parent) {
    setContentsMargins(16,16,16,16);
}

void DarkCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 10, 10);
    p.fillPath(path, QColor(0x12,0x12,0x12));
    p.setPen(QPen(QColor(0x28,0x28,0x28), 1));
    p.drawPath(path);
}
