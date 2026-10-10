// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClutchPaddlesView.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionProgressBar>

#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Between a label or a value and its bar.
constexpr int kGapPx = 8;

/// Around the handle's text, across and down.
constexpr int kHandlePadXPx = 8;
constexpr int kHandlePadYPx = 2;

/// The bars' own range: fine enough that a 12-bit axis moves them smoothly.
constexpr int kBarSteps = 1000;

constexpr int kRatioMax = 100;
constexpr int kRatioPageStep = 10;

QString percentText(int percent)
{
    return ClutchPaddlesView::tr("%1 %").arg(percent);
}

} // namespace

ClutchPaddlesView::ClutchPaddlesView(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ClutchPaddlesView::setValues(qreal top, qreal bottom)
{
    top = qBound(0.0, top, 1.0);
    bottom = qBound(0.0, bottom, 1.0);
    if (top == m_top && bottom == m_bottom)
        return;
    m_top = top;
    m_bottom = bottom;
    update();
}

void ClutchPaddlesView::setLabels(const QString &top, const QString &bottom)
{
    if (top == m_topLabel && bottom == m_bottomLabel)
        return;
    m_topLabel = top;
    m_bottomLabel = bottom;
    updateGeometry();
    update();
}

void ClutchPaddlesView::setRatio(int percent)
{
    percent = qBound(0, percent, kRatioMax);
    if (percent == m_ratio)
        return;
    m_ratio = percent;
    update();
}

void ClutchPaddlesView::setRatioEditable(bool editable)
{
    if (editable == m_ratioEditable)
        return;
    m_ratioEditable = editable;
    m_dragging = false;
    if (!editable)
        unsetCursor();
    update();
}

void ClutchPaddlesView::editRatio(int percent)
{
    percent = qBound(0, percent, kRatioMax);
    if (percent == m_ratio)
        return;
    m_ratio = percent;
    update();
    Q_EMIT ratioChanged(m_ratio);
}

// --- Geometry ----------------------------------------------------------------

int ClutchPaddlesView::trackLeft() const
{
    const QFontMetrics metrics = fontMetrics();
    return qMax(metrics.horizontalAdvance(m_topLabel), metrics.horizontalAdvance(m_bottomLabel))
           + kGapPx;
}

int ClutchPaddlesView::trackRight() const
{
    return width() - fontMetrics().horizontalAdvance(percentText(kRatioMax)) - kGapPx;
}

int ClutchPaddlesView::barHeight() const
{
    return fontMetrics().height();
}

int ClutchPaddlesView::handleRowHeight() const
{
    return fontMetrics().height() + 2 * kHandlePadYPx + 4;
}

QRect ClutchPaddlesView::topBar() const
{
    return QRect(trackLeft(), 0, qMax(0, trackRight() - trackLeft()), barHeight());
}

QRect ClutchPaddlesView::bottomBar() const
{
    return topBar().translated(0, barHeight() + handleRowHeight());
}

int ClutchPaddlesView::ratioX() const
{
    const QRect bar = topBar();
    return bar.left() + int(std::lround(bar.width() * m_ratio / double(kRatioMax)));
}

QRect ClutchPaddlesView::handleRect() const
{
    const QFontMetrics metrics = fontMetrics();
    const int w = metrics.horizontalAdvance(percentText(kRatioMax)) + 2 * kHandlePadXPx;
    const int h = metrics.height() + 2 * kHandlePadYPx;
    QRect rect(0, 0, w, h);
    rect.moveCenter(QPoint(ratioX(), barHeight() + handleRowHeight() / 2));
    // Kept inside the widget at either end of the range.
    if (rect.left() < 0)
        rect.moveLeft(0);
    if (rect.right() > width() - 1)
        rect.moveRight(width() - 1);
    return rect;
}

int ClutchPaddlesView::ratioAt(int x) const
{
    const QRect bar = topBar();
    if (bar.width() <= 0)
        return m_ratio;
    return qBound(0, int(std::lround((x - bar.left()) * double(kRatioMax) / bar.width())),
                  kRatioMax);
}

QSize ClutchPaddlesView::sizeHint() const
{
    return QSize(trackLeft() + 200 + fontMetrics().horizontalAdvance(percentText(kRatioMax))
                     + kGapPx,
                 2 * barHeight() + handleRowHeight());
}

QSize ClutchPaddlesView::minimumSizeHint() const
{
    const QSize hint = sizeHint();
    return QSize(hint.width() - 120, hint.height());
}

// --- Painting ----------------------------------------------------------------

void ClutchPaddlesView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const QPalette &colours = palette();

    const auto drawBar = [&](const QRect &rect, qreal value, const QString &label) {
        QStyleOptionProgressBar bar;
        bar.initFrom(this);
        bar.rect = rect;
        bar.minimum = 0;
        bar.maximum = kBarSteps;
        bar.progress = int(std::lround(value * kBarSteps));
        bar.textVisible = false;
        bar.state |= QStyle::State_Horizontal;
        // The bars are a readout, so they keep the active colours however the
        // window is focused: the hands that move them are on the wheel.
        bar.palette.setCurrentColorGroup(QPalette::Active);
        style()->drawControl(QStyle::CE_ProgressBar, &bar, &painter, this);

        painter.setPen(colours.color(QPalette::WindowText));
        painter.drawText(QRect(0, rect.top(), trackLeft() - kGapPx, rect.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, label);
        painter.drawText(QRect(rect.right() + kGapPx, rect.top(),
                               width() - rect.right() - kGapPx, rect.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         percentText(int(std::lround(value * 100))));
    };
    drawBar(topBar(), m_top, m_topLabel);
    drawBar(bottomBar(), m_bottom, m_bottomLabel);

    // The ratio: a line through both bars, and between them the handle with
    // the value on it. In the highlight while it can be dragged, and otherwise
    // like a plain button, still legible but plainly not the thing to grab.
    const bool editable = m_ratioEditable && isEnabled();
    QColor accent = colours.color(QPalette::Active, QPalette::Highlight);
    if (editable && (m_dragging || m_hovering))
        accent = accent.lighter(115);
    const QColor line = editable ? accent : colours.color(QPalette::Mid);

    const int x = ratioX();
    painter.fillRect(QRect(x - 1, topBar().top(), 2, bottomBar().bottom() - topBar().top() + 1),
                     line);

    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF handle = QRectF(handleRect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = handle.height() / 2.0;
    if (editable) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
    } else {
        painter.setPen(QPen(colours.color(QPalette::Mid), 1.0));
        painter.setBrush(colours.color(QPalette::Button));
    }
    painter.drawRoundedRect(handle, radius, radius);
    painter.setPen(editable ? colours.color(QPalette::Active, QPalette::HighlightedText)
                            : colours.color(QPalette::Disabled, QPalette::ButtonText));
    painter.drawText(handle, Qt::AlignCenter, percentText(m_ratio));

    // Focus as a ring round the handle rather than the style's focus rect,
    // which is a line under it.
    if (hasFocus() && editable) {
        painter.setPen(QPen(accent, 1.5));
        painter.setBrush(Qt::NoBrush);
        const QRectF ring = handle.adjusted(-2.5, -2.5, 2.5, 2.5);
        painter.drawRoundedRect(ring, ring.height() / 2.0, ring.height() / 2.0);
    }
}

// --- Editing -----------------------------------------------------------------

void ClutchPaddlesView::mousePressEvent(QMouseEvent *event)
{
    if (!m_ratioEditable || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Only the handle is grabbed. A click anywhere else leaves the ratio alone,
    // so a slip near the bars cannot move it.
    if (!handleRect().contains(event->position().toPoint())) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Held by the point clicked, not by its middle, so grabbing it off-centre
    // does not make it jump.
    m_dragging = true;
    m_grabOffset = ratioX() - int(event->position().x());
    update();
}

void ClutchPaddlesView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();
    if (m_dragging) {
        editRatio(ratioAt(at.x() + m_grabOffset));
        return;
    }

    const bool over = m_ratioEditable && handleRect().contains(at);
    if (over != m_hovering) {
        m_hovering = over;
        if (over)
            setCursor(Qt::SizeHorCursor);
        else
            unsetCursor();
        update();
    }
}

void ClutchPaddlesView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ClutchPaddlesView::keyPressEvent(QKeyEvent *event)
{
    if (!m_ratioEditable) {
        QWidget::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
        editRatio(m_ratio - 1);
        return;
    case Qt::Key_Right:
    case Qt::Key_Up:
        editRatio(m_ratio + 1);
        return;
    case Qt::Key_PageDown:
        editRatio(m_ratio - kRatioPageStep);
        return;
    case Qt::Key_PageUp:
        editRatio(m_ratio + kRatioPageStep);
        return;
    case Qt::Key_Home:
        editRatio(0);
        return;
    case Qt::Key_End:
        editRatio(kRatioMax);
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void ClutchPaddlesView::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_hovering) {
        m_hovering = false;
        unsetCursor();
        update();
    }
}

void ClutchPaddlesView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateGeometry();
}

} // namespace hidcenter::simagic_alpha_evo
