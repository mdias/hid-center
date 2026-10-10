// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RangeSlider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

namespace hidcenter::simsonn_pedals {
namespace {

/// The trough, and the room above and below it for the thumbs to stand proud of
/// it. Matched to PedalBar deliberately: the two are read one under the other on
/// the same device and a difference of a pixel or two would look like a mistake.
constexpr int kTroughHeight = 24;
constexpr int kThumbOverhang = 3;
constexpr qreal kTroughRadius = 3.0;

constexpr qreal kThumbWidth = 3.0;
constexpr qreal kThumbActiveWidth = 5.0;
constexpr qreal kThumbRadius = 1.5;

/// How close to a thumb's centre the cursor has to be to grab it. Generous
/// against the stem's width, because the stem is not the target — the value is.
constexpr qreal kGrabTolerance = 7.0;

/// How much of the accent the band between the thumbs takes. Faint: it is a
/// backdrop for the numbers drawn over it.
constexpr int kBandAlpha = 60;

/// Wide enough that the numbers inside never have to be elided.
constexpr int kPreferredWidth = 200;

} // namespace

RangeSlider::RangeSlider(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void RangeSlider::setScale(int minimum, int maximum)
{
    // An empty scale would divide by zero in toX(), and says nothing anyway.
    if (maximum <= minimum || (m_minimum == minimum && m_maximum == maximum))
        return;
    m_minimum = minimum;
    m_maximum = maximum;
    setValues(m_low, m_high);
    update();
}

void RangeSlider::setValues(int low, int high)
{
    // The gap first and the scale second, so a pair that cannot be honoured ends
    // up inside the scale rather than outside it.
    const int separation = qMin(m_separation, m_maximum - m_minimum);
    high = qMax(high, low + separation);
    low = qBound(m_minimum, low, m_maximum - separation);
    high = qBound(low + separation, high, m_maximum);

    if (m_low == low && m_high == high)
        return;
    m_low = low;
    m_high = high;
    update();
}

void RangeSlider::setMinimumSeparation(int separation)
{
    m_separation = qMax(0, separation);
    setValues(m_low, m_high);
}

void RangeSlider::setSuffix(const QString &suffix)
{
    if (m_suffix == suffix)
        return;
    m_suffix = suffix;
    update();
}

QSize RangeSlider::sizeHint() const
{
    return {kPreferredWidth, kTroughHeight + 2 * kThumbOverhang};
}

QSize RangeSlider::minimumSizeHint() const
{
    return {fontMetrics().horizontalAdvance(text()) + kPreferredWidth / 4,
            kTroughHeight + 2 * kThumbOverhang};
}

QString RangeSlider::text() const
{
    return tr("%1%3  –  %2%3").arg(m_low).arg(m_high).arg(m_suffix);
}

// --- Geometry ---------------------------------------------------------------

QRectF RangeSlider::troughRect() const
{
    return QRectF(rect()).adjusted(0.5, kThumbOverhang + 0.5, -0.5, -kThumbOverhang - 0.5);
}

qreal RangeSlider::toX(const QRectF &trough, int value) const
{
    const qreal fraction =
        qBound(0.0, double(value - m_minimum) / double(m_maximum - m_minimum), 1.0);
    return trough.left() + fraction * trough.width();
}

qreal RangeSlider::thumbCentre(const QRectF &trough, int value, qreal width) const
{
    return qBound(qreal(rect().left()) + width / 2.0, toX(trough, value),
                  qreal(rect().right()) - width / 2.0);
}

int RangeSlider::fromX(const QRectF &trough, qreal x) const
{
    if (trough.width() <= 0.0)
        return m_minimum;
    const qreal fraction = qBound(0.0, (x - trough.left()) / trough.width(), 1.0);
    return m_minimum + qRound(fraction * double(m_maximum - m_minimum));
}

// --- Dragging ---------------------------------------------------------------

RangeSlider::Thumb RangeSlider::thumbAt(const QPointF &position) const
{
    // Only the horizontal distance: a thumb spans the whole height, so anywhere
    // in the widget is on it.
    const QRectF trough = troughRect();
    const qreal toLow = qAbs(position.x() - thumbCentre(trough, m_low, kThumbActiveWidth));
    const qreal toHigh = qAbs(position.x() - thumbCentre(trough, m_high, kThumbActiveWidth));

    if (qMin(toLow, toHigh) > kGrabTolerance)
        return Thumb::None;
    // The nearer one where the two are close enough to overlap, and the low one
    // on a tie — arbitrary, but the same every time.
    return toLow <= toHigh ? Thumb::Low : Thumb::High;
}

void RangeSlider::dragTo(qreal x)
{
    if (m_dragging == Thumb::None)
        return;

    const int value = fromX(troughRect(), x + m_grabOffset);
    const int low = m_low;
    const int high = m_high;

    if (m_dragging == Thumb::Low)
        setValues(qMin(value, m_high - m_separation), m_high);
    else
        setValues(m_low, qMax(value, m_low + m_separation));

    if (m_low != low || m_high != high)
        Q_EMIT valuesChanged(m_low, m_high);
}

void RangeSlider::setHovered(Thumb thumb)
{
    if (m_hovered == thumb)
        return;
    m_hovered = thumb;
    if (thumb == Thumb::None)
        unsetCursor();
    else
        setCursor(Qt::SizeHorCursor);
    update();
}

void RangeSlider::mousePressEvent(QMouseEvent *event)
{
    const Thumb thumb =
        event->button() == Qt::LeftButton ? thumbAt(event->position()) : Thumb::None;
    if (thumb == Thumb::None) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_grabOffset =
        thumbCentre(troughRect(), thumb == Thumb::Low ? m_low : m_high, kThumbActiveWidth)
        - event->position().x();
    m_dragging = thumb;
    setHovered(thumb);
    update();
    event->accept();
}

void RangeSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging != Thumb::None) {
        dragTo(event->position().x());
        event->accept();
        return;
    }
    setHovered(thumbAt(event->position()));
    QWidget::mouseMoveEvent(event);
}

void RangeSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging == Thumb::None) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = Thumb::None;
    setHovered(thumbAt(event->position()));
    update();
    event->accept();
}

void RangeSlider::leaveEvent(QEvent *event)
{
    setHovered(Thumb::None);
    QWidget::leaveEvent(event);
}

// --- Painting ---------------------------------------------------------------

void RangeSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF trough = troughRect();
    const QColor accent = palette().color(QPalette::Accent);

    QPainterPath troughPath;
    troughPath.addRoundedRect(trough, kTroughRadius, kTroughRadius);

    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawPath(troughPath);

    // The part of the travel that is actually used, between the thumbs. Clipped
    // to the trough so a band that reaches an end does not square off its
    // rounded corner.
    p.save();
    p.setClipPath(troughPath);
    QColor band = accent;
    band.setAlpha(kBandAlpha);
    const qreal low = toX(trough, m_low);
    const qreal high = toX(trough, m_high);
    p.fillRect(QRectF(low, trough.top(), high - low, trough.height()), band);
    p.restore();

    p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(troughPath);

    // The thumbs last, over the band and the trough's own border. The one under
    // the cursor or in hand is wider and in the accent, so what a drag is about
    // to move is never in doubt.
    const std::pair<Thumb, int> thumbs[] = {{Thumb::Low, m_low}, {Thumb::High, m_high}};
    p.setPen(Qt::NoPen);
    for (const auto &[which, value] : thumbs) {
        const bool active =
            m_dragging == which || (m_dragging == Thumb::None && m_hovered == which);
        const qreal width = active ? kThumbActiveWidth : kThumbWidth;
        p.setBrush(active ? accent : palette().color(QPalette::Text));
        p.drawRoundedRect(QRectF(thumbCentre(trough, value, width) - width / 2.0,
                                 trough.top() - kThumbOverhang, width,
                                 trough.height() + 2 * kThumbOverhang),
                          kThumbRadius, kThumbRadius);
    }

    p.setPen(palette().color(QPalette::Text));
    p.drawText(trough, Qt::AlignCenter, text());
}

} // namespace hidcenter::simsonn_pedals
