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
        float speed = 3.f;
        if (std::abs(m_thumbX - m_targetX) < speed) {
            m_thumbX = m_targetX; m_anim.stop();
        } else {
            m_thumbX += (m_thumbX < m_targetX) ? speed : -speed;
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

    QColor track = m_checked ? QColor(0x8b,0x5c,0xf6) : QColor(0x33,0x33,0x33);
    p.setBrush(track);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 4, 44, 16, 8, 8);

    // Thumb
    p.setBrush(Qt::white);
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
            background: #222; height: 4px; border-radius: 2px;
        }
        QSlider::groove:vertical {
            background: #222; width: 4px; border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #8b5cf6; width: 16px; height: 16px;
            border-radius: 8px; margin: -6px 0;
        }
        QSlider::handle:vertical {
            background: #8b5cf6; width: 16px; height: 16px;
            border-radius: 8px; margin: 0 -6px;
        }
        QSlider::sub-page:horizontal { background: #8b5cf6; border-radius: 2px; }
        QSlider::add-page:vertical   { background: #8b5cf6; border-radius: 2px; }
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
    p.setPen(QPen(QColor(0x33,0x33,0x33), 5));
    p.setBrush(Qt::NoBrush);
    p.drawArc(rect, 225*16, -270*16);

    // Value arc
    double norm = (vhi>vlo) ? (val-vlo)/(vhi-vlo) : 0.0;
    p.setPen(QPen(QColor(0x8b,0x5c,0xf6), 5));
    p.drawArc(rect, 225*16, (int)(-norm*270.0*16));

    // Centre
    p.setBrush(QColor(0x1a,0x1a,0x1a));
    p.setPen(Qt::NoPen);
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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,24,0,0);
    layout->setSpacing(0);
    m_content = new QWidget(this);
    layout->addWidget(m_content);
    setCursor(Qt::PointingHandCursor);
}

void CollapsibleSection::setCollapsed(bool c) {
    m_collapsed = c;
    m_content->setVisible(!c);
    update();
}

void CollapsibleSection::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Header bar
    p.fillRect(0,0,width(),22, QColor(0x1a,0x1a,0x1a));
    p.setPen(QColor(0x33,0x33,0x33));
    p.drawLine(0,22, width(),22);

    // Title
    p.setPen(QColor(0xaa,0xaa,0xaa));
    p.setFont(QFont("Segoe UI", 10));
    p.drawText(QRect(8,0,width()-30,22), Qt::AlignVCenter, m_title);

    // Chevron
    p.setPen(QPen(QColor(0x88,0x88,0x88), 1.5));
    int cx = width()-14, cy = 11;
    if (m_collapsed) {
        p.drawLine(cx-4,cy-2, cx,cy+2);
        p.drawLine(cx,cy+2,   cx+4,cy-2);
    } else {
        p.drawLine(cx-4,cy+2, cx,cy-2);
        p.drawLine(cx,cy-2,   cx+4,cy+2);
    }
}

void CollapsibleSection::mousePressEvent(QMouseEvent* e) {
    if (e->y() <= 22) setCollapsed(!m_collapsed);
}

// ──────────────────────────────────────────────────────────────────────────────
// DarkCard
// ──────────────────────────────────────────────────────────────────────────────
DarkCard::DarkCard(QWidget* parent) : QWidget(parent) {
    setContentsMargins(12,12,12,12);
}

void DarkCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), 8, 8);
    p.fillPath(path, QColor(0x11,0x11,0x11));
    p.setPen(QPen(QColor(0x22,0x22,0x22), 1));
    p.drawPath(path);
}
