#include "PillTabBar.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QFont>
#include <QEasingCurve>
#include <QResizeEvent>

namespace {
constexpr int kSidePadding   = 20; // left/right inset of the whole bar
constexpr int kTabHPadding   = 18; // horizontal padding inside each pill
constexpr int kTabVPadding   = 9;  // vertical padding inside each pill
constexpr int kTabSpacing    = 10; // gap between tabs
constexpr int kAnimMs        = 220;
}

PillTabBar::PillTabBar(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);

    QFont f = font();
    f.setPointSize(10);
    f.setWeight(QFont::DemiBold);
    setFont(f);

    m_pillAnim = new QPropertyAnimation(this, "pillRect", this);
    m_pillAnim->setDuration(kAnimMs);
    m_pillAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_hoverAnim = new QPropertyAnimation(this, "hoverAlpha", this);
    m_hoverAnim->setDuration(140);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
}

int PillTabBar::addTab(const QString& text) {
    m_tabs.push_back({text, QRectF()});
    relayout();
    if (m_currentIndex < 0) {
        m_currentIndex = 0;
        m_pillRect = targetPillRect(0);
    }
    update();
    return m_tabs.size() - 1;
}

void PillTabBar::setCurrentIndex(int index, bool animate) {
    if (index < 0 || index >= m_tabs.size() || index == m_currentIndex) return;
    m_currentIndex = index;
    QRectF target = targetPillRect(index);
    if (animate && isVisible()) {
        m_pillAnim->stop();
        m_pillAnim->setStartValue(m_pillRect.isValid() ? m_pillRect : target);
        m_pillAnim->setEndValue(target);
        m_pillAnim->start();
    } else {
        setPillRect(target);
    }
    emit currentChanged(index);
    update();
}

void PillTabBar::relayout() {
    QFontMetrics fm(font());
    qreal x = kSidePadding;
    const int h = height() > 0 ? height() : sizeHint().height();
    for (auto& tab : m_tabs) {
        int textW = fm.horizontalAdvance(tab.text);
        qreal w = textW + kTabHPadding * 2;
        qreal tabH = fm.height() + kTabVPadding * 2;
        qreal y = (h - tabH) / 2.0;
        tab.rect = QRectF(x, y, w, tabH);
        x += w + kTabSpacing;
    }
}

QRectF PillTabBar::targetPillRect(int index) const {
    if (index < 0 || index >= m_tabs.size()) return QRectF();
    return m_tabs[index].rect;
}

QSize PillTabBar::sizeHint() const {
    QFontMetrics fm(font());
    return QSize(400, fm.height() + kTabVPadding * 2 + 20);
}

QSize PillTabBar::minimumSizeHint() const {
    return sizeHint();
}

void PillTabBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayout();
    if (m_currentIndex >= 0) {
        m_pillAnim->stop();
        setPillRect(targetPillRect(m_currentIndex));
    }
}

int PillTabBar::tabAt(const QPoint& pos) const {
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].rect.contains(pos)) return i;
    }
    return -1;
}

void PillTabBar::setHoverIndex(int index) {
    if (index == m_hoverIndex) return;
    m_hoverIndex = index;
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hoverAlpha);
    m_hoverAnim->setEndValue(index >= 0 && index != m_currentIndex ? 1.0 : 0.0);
    m_hoverAnim->start();
}

void PillTabBar::mouseMoveEvent(QMouseEvent* event) {
    setHoverIndex(tabAt(event->pos()));
}

void PillTabBar::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    setHoverIndex(-1);
}

void PillTabBar::mousePressEvent(QMouseEvent* event) {
    int idx = tabAt(event->pos());
    if (idx >= 0) setCurrentIndex(idx);
}

void PillTabBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background + bottom border. Painted manually rather than via QSS
    // because this widget fully owns its own paintEvent (no base-class
    // stylesheet rendering happens for a plain QWidget without
    // WA_StyledBackground).
    p.fillRect(rect(), QColor(0x0a, 0x0a, 0x0a));
    p.fillRect(QRectF(0, height() - 1, width(), 1), QColor(0x23, 0x23, 0x23));

    // Sliding active pill — solid white, drawn first so text paints on top.
    if (m_pillRect.isValid()) {
        QPainterPath path;
        path.addRoundedRect(m_pillRect, m_pillRect.height() / 2.0, m_pillRect.height() / 2.0);
        p.fillPath(path, QColor(255, 255, 255));
    }

    // Faint hover pill for the currently-hovered *inactive* tab.
    if (m_hoverIndex >= 0 && m_hoverIndex != m_currentIndex && m_hoverAlpha > 0.001) {
        const QRectF& r = m_tabs[m_hoverIndex].rect;
        QPainterPath path;
        path.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
        QColor tint(255, 255, 255, int(22 * m_hoverAlpha));
        p.fillPath(path, tint);
    }

    for (int i = 0; i < m_tabs.size(); ++i) {
        const auto& tab = m_tabs[i];
        QColor textColor;
        if (i == m_currentIndex) {
            textColor = QColor(0, 0, 0); // black text on the white pill
        } else if (i == m_hoverIndex) {
            // Ease gray -> near-white as hoverAlpha animates in.
            QColor from(122, 122, 122);
            QColor to(230, 230, 230);
            textColor = QColor(
                from.red()   + int((to.red()   - from.red())   * m_hoverAlpha),
                from.green() + int((to.green() - from.green()) * m_hoverAlpha),
                from.blue()  + int((to.blue()  - from.blue())  * m_hoverAlpha)
            );
        } else {
            textColor = QColor(122, 122, 122); // gray, inactive
        }

        QFont f = font();
        f.setWeight(i == m_currentIndex ? QFont::DemiBold : QFont::Medium);
        p.setFont(f);
        p.setPen(textColor);
        p.drawText(tab.rect, Qt::AlignCenter, tab.text);
    }
}
