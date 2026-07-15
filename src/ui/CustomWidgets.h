#pragma once
#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QDial>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <QPropertyAnimation>
#include <QHBoxLayout>
#include <functional>
#include <vector>

// ──────────────────────────────────────────────────────────────────────────────
// Monochrome dark-themed custom widgets — black / white / gray only.
// Colours:
//   background:      #000 / #0a0a0a
//   card bg:         #111 / #161616 / #0d0d0d
//   accent (neutral): #e8e8e8 (selected tab, toggle on, slider thumb/fill)
//   accent hover:     #ffffff
//   text primary:    #fff
//   text secondary:  #aaa / #888
//   border:          #2a2a2a / #383838
//   status (functional only, not decorative): #22c55e ok, #ef4444 stop/clip
// ──────────────────────────────────────────────────────────────────────────────

// ── Toggle switch — professional audio-gear "engaged" style ───────────────────
// OFF: flat, unlit dark track + dim gray thumb (deliberately inert-looking).
// ON:  track fills with the app's functional accent green, thumb turns
//      bright white, and a soft green glow blooms around the whole switch —
//      the same "this module is lit up and running" language real hardware
//      effects units use, reusing the accent already defined for functional
//      status (#22c55e) rather than inventing a new color.
// Both the thumb slide and the color/glow crossfade are driven by
// QPropertyAnimation (not manual per-frame stepping) for consistent,
// frame-rate-independent easing.
class ToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    Q_PROPERTY(qreal thumbPos READ thumbPos WRITE setThumbPos)
    Q_PROPERTY(qreal onProgress READ onProgress WRITE setOnProgress)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool on);
    QSize sizeHint() const override { return {44,24}; }
    QSize minimumSizeHint() const override { return {44,24}; }

    qreal thumbPos() const { return m_thumbPos; }
    void setThumbPos(qreal t) { m_thumbPos = t; update(); }

    // 0 = fully OFF look, 1 = fully ON look. Drives track color and glow
    // strength together so they crossfade in lockstep with the thumb slide.
    qreal onProgress() const { return m_onProgress; }
    void setOnProgress(qreal t);

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    bool   m_checked   = false;
    qreal  m_thumbPos   = 0.0; // 0 = left (off), 1 = right (on)
    qreal  m_onProgress = 0.0; // 0..1 crossfade between off/on visual style

    QPropertyAnimation* m_posAnim;
    QPropertyAnimation* m_progAnim;
    class QGraphicsDropShadowEffect* m_glow;
};

// ── Vertical / horizontal slider (matching popup.css slider style) ─────────────
class DarkSlider : public QSlider {
    Q_OBJECT
public:
    explicit DarkSlider(Qt::Orientation o, QWidget* parent = nullptr);

    // Floating-point convenience
    void   setRangeF(double lo, double hi, double step = 0.01);
    void   setValueF(double v);
    double valueF() const;
    void   setDecimals(int d)  { decimals = d; }

signals:
    void valueChangedF(double v);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double flo=0, fhi=1, fstep=0.01;
    int    decimals = 2;
};

// ── Knob / circular dial ──────────────────────────────────────────────────────
class DarkKnob : public QWidget {
    Q_OBJECT
public:
    explicit DarkKnob(QWidget* parent = nullptr);
    void   setRange(double lo, double hi) { vlo=lo; vhi=hi; }
    void   setValue(double v);
    double value() const { return val; }
    QSize  sizeHint() const override { return {60,60}; }

signals:
    void valueChanged(double v);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    double val=0, vlo=0, vhi=1;
    QPointF dragStart;
    double  dragStartVal=0;
};

// ── VU Meter bar (left + right channel) ──────────────────────────────────────
class VUMeter : public QWidget {
    Q_OBJECT
public:
    explicit VUMeter(QWidget* parent = nullptr);
    void setLevels(float rmsL, float rmsR, float peak, bool clip);
    QSize sizeHint() const override { return {180,20}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float m_rmsL=0, m_rmsR=0, m_peak=0;
    bool  m_clip=false;
};

// ── Spectrum Canvas ────────────────────────────────────────────────────────────
class SpectrumWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget* parent = nullptr);
    void setBins(const std::vector<float>& bins);
    QSize sizeHint() const override { return {360,80}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<float> m_bins;
};

// ── Effect section: card with title + Enable/Disable switch that drives an
//    animated expand/collapse of its body. Disabled == only the title and the
//    toggle remain visible; every slider, dropdown, and parameter label the
//    caller adds to content() is hidden with the body, and reappears with its
//    previous value intact (values live in AppSettings, never in the hidden
//    widgets, so nothing is lost while collapsed).
// ────────────────────────────────────────────────────────────────────────────
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    // Add rows/controls here — this is the part hidden when disabled.
    QWidget* content() { return m_content; }
    // Header row (title + toggle) — append extra widgets (e.g. a note label)
    // before the toggle via headerLayout()->insertWidget(idx, w).
    QHBoxLayout* headerLayout() { return m_headerLayout; }
    ToggleSwitch* toggle() { return m_toggle; }

    bool isExpanded() const { return m_expanded; }
    // animate=false is for initial sync from settings — no visible motion.
    void setExpanded(bool expanded, bool animate = true);

signals:
    void toggled(bool on);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString              m_title;
    ToggleSwitch*         m_toggle;
    QHBoxLayout*          m_headerLayout;
    QWidget*              m_content;
    QPropertyAnimation*   m_anim;
    bool                  m_expanded = false;
};

// ── Card container ─────────────────────────────────────────────────────────────
class DarkCard : public QWidget {
    Q_OBJECT
public:
    explicit DarkCard(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
};

// ── Label styles ───────────────────────────────────────────────────────────────
inline QLabel* makeLabel(const QString& text, int fontSize = 12, bool bold = false,
                         const QString& color = "#ffffff") {
    auto* lbl = new QLabel(text);
    QString style = QString("color: %1; font-size: %2px;%3")
        .arg(color).arg(fontSize).arg(bold ? " font-weight: bold;" : "");
    lbl->setStyleSheet(style);
    return lbl;
}

inline QLabel* makeDimLabel(const QString& text, int sz = 11) {
    return makeLabel(text, sz, false, "#aaaaaa");
}
