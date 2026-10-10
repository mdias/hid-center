// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/telemetry_page/RpmBar.h"

#include "core/telemetry/TelemetryHub.h"

#include <QMouseEvent>
#include <QPainter>

namespace hidcenter {
namespace {

/// Segments across the bar. Coarse enough that each is a clear step rather than
/// a pixel, which is what makes the thing readable in peripheral vision.
constexpr int kSegments = 20;

constexpr int kSegmentGap = 2;
constexpr qreal kRadius = 2.0;

/// Where the colour changes, as a fraction of full scale. The last fifth being
/// red is the convention every rim follows.
constexpr qreal kAmberFrom = 0.6;
constexpr qreal kRedFrom = 0.8;

/// Unlit segments. Enough to show where the bar ends without competing with the
/// lit ones.
constexpr qreal kUnlitOpacity = 0.18;

QColor colourForSegment(int segment)
{
    const qreal position = qreal(segment) / qreal(kSegments);
    if (position >= kRedFrom)
        return {0xd6, 0x3a, 0x2f};
    if (position >= kAmberFrom)
        return {0xf0, 0x8c, 0x00};
    return {0x37, 0xb2, 0x4d};
}

} // namespace

RpmBar::RpmBar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize RpmBar::sizeHint() const
{
    return {320, 28};
}

QSize RpmBar::minimumSizeHint() const
{
    // Below about this, segments collapse into each other and the bar stops
    // saying anything a plain number would not.
    return {160, 20};
}

void RpmBar::setValue(int rpm)
{
    const int clamped = qBound(0, rpm, m_maximum);
    if (clamped == m_value)
        return;

    m_value = clamped;
    update();
    Q_EMIT valueChanged(m_value);
}

void RpmBar::setMaximum(int rpm)
{
    const int scale = rpm > 0 ? rpm : kTelemetryRpmMax;
    if (scale == m_maximum)
        return;

    m_maximum = scale;

    // Repaint even when the value survives the change: every segment now stands
    // for a different number of revs, so the same value fills a different amount
    // of bar.
    update();

    // Then let setValue() do the clamping and the signalling, so a value falling
    // out of range is announced by the one path that announces value changes.
    setValue(m_value);
}

void RpmBar::setEditable(bool editable)
{
    if (m_editable == editable)
        return;

    m_editable = editable;
    setCursor(m_editable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    // A read-only bar must not look disabled — it is a live readout, and greying
    // it out would cost exactly the legibility it exists for. The frame is what
    // changes instead; see paintEvent().
    update();
}

int RpmBar::valueAt(int x) const
{
    if (width() <= 0)
        return 0;
    return qBound(0, int(qreal(x) * m_maximum / qreal(width())), m_maximum);
}

void RpmBar::mousePressEvent(QMouseEvent *event)
{
    if (!m_editable) {
        QWidget::mousePressEvent(event);
        return;
    }

    setValue(valueAt(int(event->position().x())));
    event->accept();
}

void RpmBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_editable || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    setValue(valueAt(int(event->position().x())));
    event->accept();
}

void RpmBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // A dotted outline while read-only, solid while editable: the one visible
    // difference between "you may drag this" and "this is showing you a value".
    QPen frame(palette().color(QPalette::Mid));
    frame.setStyle(m_editable ? Qt::SolidLine : Qt::DotLine);
    painter.setPen(frame);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    const qreal inner = 3.0;
    const qreal usable = width() - 2 * inner;
    if (usable <= 0)
        return;

    const qreal segmentWidth = (usable - kSegmentGap * (kSegments - 1)) / kSegments;
    if (segmentWidth <= 0)
        return;

    // Round up, so any value above zero lights at least one segment. A bar that
    // reads empty while the engine is turning would be a lie.
    const int lit = m_value == 0
        ? 0
        : qMin(kSegments, int((qreal(m_value) * kSegments + m_maximum - 1) / m_maximum));

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < kSegments; ++i) {
        QColor colour = colourForSegment(i);
        if (i >= lit)
            colour.setAlphaF(kUnlitOpacity);

        const QRectF box(inner + i * (segmentWidth + kSegmentGap), inner, segmentWidth,
                         height() - 2 * inner);
        painter.setBrush(colour);
        painter.drawRoundedRect(box, kRadius, kRadius);
    }
}

} // namespace hidcenter
