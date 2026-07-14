#include "CustomWidgets.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>

// ──────────────────────────────────────────────────────────────────────────────
// ToggleSwitch
// ──────────────────────────────────────────────────────────────────────────────
ToggleSwitch::ToggleSwitch(QWidget* parent) : QWidget(parent) {
    setFixedSize(44, 24);
    setCursor(Qt::PointingHandCursor);
    connect(&m_anim, &QTimer::timeout, this, [this]{
        // Ease-out: step proportionally to remaining distance for a softer,
        // more natural settle instead of a constant-speed slide.
        float remaining = m_targetX - m_thumbX;
        if (std::abs(remaining) < 0.4f) {
            m_thumbX = m_targetX; m_anim.stop();
        } else {
            m_thumbX += remaining * 0.35f;
        }
        update();
    });
}

void ToggleSwitch::setChecked(bool on) {
    if (m_checked == on) return;
    m_checked = on;
    m_targetX = on ? 22.f : 4.f;
    m_anim.start(16);
    emit toggled(on);
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor track = m_checked ? QColor(0xe8,0xe8,0xe8) : QColor(0x2a,0x2a,0x2a);
    p.setPen(QPen(QColor(m_checked ? 0xe8 : 0x3a, m_checked ? 0xe8 : 0x3a, m_checked ? 0xe8 : 0x3a), 1));
    p.setBrush(track);
    p.drawRoundedRect(QRectF(0.5, 4.5, 43, 15), 7.5, 7.5);

    // Thumb — subtle drop shadow for depth, plain white face
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0,0,0,60));
    p.drawEllipse(QRectF(m_thumbX, 3, 20, 20));
    p.setBrush(m_checked ? Qt::white : QColor(0xcf,0xcf,0xcf));
    p.drawEllipse(QRectF(m_thumbX, 2, 20, 20));
}

void ToggleSwitch::mousePressEvent(QMouseEvent*) {
    setChecked(!m_checked);
}

// ──────────────────────────────────────────────────────────────────────────────
// DarkSlider
// ──────────────────────────────────────────────────────────────────────────────
DarkSlider::DarkSlider(Qt::Orientation o, QWidget* parent) : QSlider(o, parent) {
    setStyleSheet(R"(
        QSlider::groove:horizontal {
            background: #1c1c1c; height: 4px; border-radius: 2px;
            border: 1px solid #2a2a2a;
        }
        QSlider::groove:vertical {
            background: #1c1c1c; width: 4px; border-radius: 2px;
            border: 1px solid #2a2a2a;
        }
        QSlider::handle:horizontal {
            background: #e8e8e8; width: 15px; height: 15px;
            border: 1px solid #050505; border-radius: 8px; margin: -6px 0;
        }
        QSlider::handle:vertical {
            background: #e8e8e8; width: 15px; height: 15px;
            border: 1px solid #050505; border-radius: 8px; margin: 0 -6px;
        }
        QSlider::handle:hover    { background: #ffffff; }
        QSlider::handle:pressed  { background: #cfcfcf; }
        QSlider::sub-page:horizontal { background: #9a9a9a; border-radius: 2px; }
        QSlider::add-page:vertical   { background: #9a9a9a; border-radius: 2px; }
        QSlider::add-page:horizontal { background: #1c1c1c; border-radius: 2px; }
        QSlider::sub-page:vertical   { background: #1c1c1c; border-radius: 2px; }
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
