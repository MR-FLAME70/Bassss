#pragma once
#include <QWidget>
#include <QString>
#include <QRectF>
#include <QColor>
#include <QVector>
#include <QPropertyAnimation>

// ──────────────────────────────────────────────────────────────────────────────
// PillTabBar — segmented, pill-shaped tab bar.
//
// Replaces the stock QTabBar (which can only be skinned via QSS and cannot
// animate — Qt style sheets don't support transitions). This is a fully
// custom-painted QWidget so the selection pill can *slide* between tabs and
// hover states can *fade*, both driven by QPropertyAnimation.
//
//   - Active tab:   solid white rounded pill, black bold text.
//   - Inactive tab:  transparent background, gray text.
//   - Hover:         inactive tab's text eases toward white and a faint
//                     pill fades in behind it.
//   - Selecting a tab slides the white pill from its old position to the
//     new one instead of just snapping.
// ──────────────────────────────────────────────────────────────────────────────
class PillTabBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(QRectF pillRect READ pillRect WRITE setPillRect)
    Q_PROPERTY(qreal hoverAlpha READ hoverAlpha WRITE setHoverAlpha)

public:
    explicit PillTabBar(QWidget* parent = nullptr);

    int addTab(const QString& text);
    void setCurrentIndex(int index, bool animate = true);
    int currentIndex() const { return m_currentIndex; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    QRectF pillRect() const { return m_pillRect; }
    void setPillRect(const QRectF& r) { m_pillRect = r; update(); }

    qreal hoverAlpha() const { return m_hoverAlpha; }
    void setHoverAlpha(qreal a) { m_hoverAlpha = a; update(); }

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Tab {
        QString text;
        QRectF  rect; // laid out geometry, recomputed on resize/relayout
    };

    void relayout();
    QRectF targetPillRect(int index) const;
    int tabAt(const QPoint& pos) const;
    void setHoverIndex(int index);

    QVector<Tab> m_tabs;
    int m_currentIndex = -1;
    int m_hoverIndex   = -1;

    QRectF m_pillRect;      // animated: current drawn position of the white pill
    qreal  m_hoverAlpha = 0.0; // animated: 0 = no hover tint, 1 = full hover tint

    QPropertyAnimation* m_pillAnim  = nullptr;
    QPropertyAnimation* m_hoverAnim = nullptr;
};
