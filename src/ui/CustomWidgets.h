#pragma once
#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QDial>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
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

// ── Toggle switch (CSS custom toggle) ─────────────────────────────────────────
class ToggleSwitch : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool on);
    QSize sizeHint() const override { return {44,24}; }
    QSize minimumSizeHint() const override { return {44,24}; }

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    bool   m_checked = false;
    QTimer m_anim;
    float  m_thumbX  = 4.f; // animated thumb position
    float  m_targetX = 4.f;
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

// ── Section group box with collapse support ────────────────────────────────────
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);
    QWidget* content() { return m_content; }
    void setCollapsed(bool c);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    QString  m_title;
    QWidget* m_content;
    bool     m_collapsed = false;
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
