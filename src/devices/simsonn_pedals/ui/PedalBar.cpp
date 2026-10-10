// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

#include "../Pedals.h"

namespace hidcenter::simsonn_pedals {
namespace {

/// The trough, and the room left above and below it for the thumbs to stand
/// proud of it. The thumbs overhang on purpose: a marker drawn inside the trough
/// competes with the fill, and at the floor of the range the two would coincide.
constexpr int kTroughHeight = 24;
constexpr int kThumbOverhang = 3;
constexpr qreal kTroughRadius = 3.0;

/// The thumbs themselves: a stem the height of the trough plus its overhang,
/// wide enough to see against a filled bar. The wider one is what a thumb
/// becomes under the cursor or while it is being dragged — a 3 px stem is enough
/// to read but not enough to look like something you can take hold of.
constexpr qreal kThumbWidth = 3.0;
constexpr qreal kThumbActiveWidth = 5.0;
constexpr qreal kThumbRadius = 1.5;

/// How close to a thumb's centre the cursor has to be to grab it. Generous
/// against the stem's width because the stem is not the target: the endpoint is,
/// and nobody can be asked to hit a 3 px column on a bar this wide.
constexpr qreal kGrabTolerance = 7.0;

/// How much of the accent the calibrated band and the live fill each take. The
/// band is a backdrop and has to stay behind the number; the fill is the thing
/// being read and does not.
constexpr int kBandAlpha = 40;
constexpr int kFillAlpha = 150;

/// Wide enough that the number inside never has to be elided.
constexpr int kPreferredWidth = 260;

} // namespace

PedalBar::PedalBar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PedalBar::setRange(int minimum, int maximum)
{
    // An empty range would divide by zero in toX(), and says nothing anyway.
    if (maximum <= minimum || (m_minimum == minimum && m_maximum == maximum))
        return;
    m_minimum = minimum;
    m_maximum = maximum;
    update();
}

void PedalBar::setValue(const std::optional<int> &value)
{
    if (m_value == value)
        return;
    m_value = value;
    update();
}

void PedalBar::setCalibration(const std::optional<std::pair<int, int>> &calibration)
{
    if (m_calibration == calibration)
        return;
    m_calibration = calibration;
    update();
}

void PedalBar::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    update();
}

void PedalBar::setEditable(bool editable)
{
    if (m_editable == editable)
        return;
    m_editable = editable;
    // Only while it can be dragged: tracking the mouse over a bar that cannot
    // be touched would be a paint every time the cursor crosses it.
    setMouseTracking(editable);
    if (!editable) {
        m_dragging = Thumb::None;
        setHovered(Thumb::None);
    }
    update();
}

QSize PedalBar::sizeHint() const
{
    return {kPreferredWidth, kTroughHeight + 2 * kThumbOverhang};
}

QSize PedalBar::minimumSizeHint() const
{
    return {fontMetrics().horizontalAdvance(m_text) + 2 * kPreferredWidth / 10,
            kTroughHeight + 2 * kThumbOverhang};
}

// --- Geometry ---------------------------------------------------------------

QRectF PedalBar::troughRect() const
{
    return QRectF(rect()).adjusted(0.5, kThumbOverhang + 0.5, -0.5, -kThumbOverhang - 0.5);
}

qreal PedalBar::toX(const QRectF &trough, int value) const
{
    const qreal fraction =
        qBound(0.0, double(value - m_minimum) / double(m_maximum - m_minimum), 1.0);
    return trough.left() + fraction * trough.width();
}

qreal PedalBar::thumbCentre(const QRectF &trough, int value, qreal width) const
{
    return qBound(qreal(rect().left()) + width / 2.0, toX(trough, value),
                  qreal(rect().right()) - width / 2.0);
}

int PedalBar::fromX(const QRectF &trough, qreal x) const
{
    if (trough.width() <= 0.0)
        return m_minimum;
    const qreal fraction = qBound(0.0, (x - trough.left()) / trough.width(), 1.0);
    return m_minimum + qRound(fraction * double(m_maximum - m_minimum));
}

std::pair<int, int> PedalBar::ordered() const
{
    if (!m_calibration)
        return {m_minimum, m_maximum};
    return {qMin(m_calibration->first, m_calibration->second),
            qMax(m_calibration->first, m_calibration->second)};
}

// --- Dragging ---------------------------------------------------------------

PedalBar::Thumb PedalBar::thumbAt(const QPointF &position) const
{
    if (!m_editable || !m_calibration)
        return Thumb::None;

    // Only the horizontal distance: a thumb spans the bar's whole height, so
    // anywhere in the widget is on it.
    const QRectF trough = troughRect();
    const std::pair<int, int> current = ordered();
    const qreal toLow =
        qAbs(position.x() - thumbCentre(trough, current.first, kThumbActiveWidth));
    const qreal toHigh =
        qAbs(position.x() - thumbCentre(trough, current.second, kThumbActiveWidth));

    if (qMin(toLow, toHigh) > kGrabTolerance)
        return Thumb::None;
    // The nearer one where the two are close enough to overlap, and the low one
    // on a tie — arbitrary, but the same every time.
    return toLow <= toHigh ? Thumb::Low : Thumb::High;
}

void PedalBar::dragTo(qreal x)
{
    if (m_dragging == Thumb::None || !m_calibration)
        return;

    const QRectF trough = troughRect();
    const std::pair<int, int> current = ordered();
    int value = fromX(trough, x + m_grabOffset);

    // Neither thumb may cross the other, nor come so close to it that what is
    // left could not be a pedal's travel. kMovementThreshold is the same span
    // this page already calls "an axis with nothing attached to it", and a
    // calibration that narrow would have the pedal saturating a hair off its
    // rest position.
    if (m_dragging == Thumb::Low)
        value = qMin(value, current.second - kMovementThreshold);
    else
        value = qMax(value, current.first + kMovementThreshold);
    value = qBound(m_minimum, value, m_maximum);

    const std::pair<int, int> edited = m_dragging == Thumb::Low
        ? std::pair {value, current.second}
        : std::pair {current.first, value};
    if (edited == current)
        return;

    // Moved here as well as announced, so the thumb keeps up with the mouse
    // whatever the owner decides to do with the new value.
    m_calibration = edited;
    update();
    Q_EMIT calibrationEdited(edited.first, edited.second);
}

void PedalBar::setHovered(Thumb thumb)
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

void PedalBar::mousePressEvent(QMouseEvent *event)
{
    const Thumb thumb =
        event->button() == Qt::LeftButton ? thumbAt(event->position()) : Thumb::None;
    if (thumb == Thumb::None) {
        QWidget::mousePressEvent(event);
        return;
    }

    const std::pair<int, int> current = ordered();
    m_grabOffset = thumbCentre(troughRect(),
                               thumb == Thumb::Low ? current.first : current.second,
                               kThumbActiveWidth)
        - event->position().x();
    m_dragging = thumb;
    setHovered(thumb);
    update();
    event->accept();
}

void PedalBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging != Thumb::None) {
        dragTo(event->position().x());
        event->accept();
        return;
    }
    setHovered(thumbAt(event->position()));
    QWidget::mouseMoveEvent(event);
}

void PedalBar::mouseReleaseEvent(QMouseEvent *event)
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

void PedalBar::leaveEvent(QEvent *event)
{
    setHovered(Thumb::None);
    QWidget::leaveEvent(event);
}

// --- Painting ---------------------------------------------------------------

void PedalBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF trough = troughRect();
    const QColor accent = palette().color(QPalette::Accent);

    // The trough, which is also the clip for everything drawn inside it, so a
    // fill that reaches the end does not square off its own corners.
    QPainterPath troughPath;
    troughPath.addRoundedRect(trough, kTroughRadius, kTroughRadius);

    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawPath(troughPath);

    p.save();
    p.setClipPath(troughPath);

    // The calibrated span, behind everything: the part of the sensor's range the
    // device actually maps to an output.
    if (m_calibration) {
        const std::pair<int, int> current = ordered();
        const qreal low = toX(trough, current.first);
        const qreal high = toX(trough, current.second);
        QColor band = accent;
        band.setAlpha(kBandAlpha);
        p.fillRect(QRectF(low, trough.top(), high - low, trough.height()), band);
    }

    if (m_value) {
        QColor fill = accent;
        fill.setAlpha(kFillAlpha);
        p.fillRect(QRectF(trough.left(), trough.top(), toX(trough, *m_value) - trough.left(),
                          trough.height()),
                   fill);
    }

    p.restore();

    p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(troughPath);

    // The thumbs last, so they read over both the fill and the trough's own
    // border — which is what an endpoint marker has to do to stay findable. The
    // one under the cursor or in hand is drawn wider and in the accent, so that
    // what a drag is about to move is never in doubt.
    if (m_calibration) {
        const std::pair<int, int> current = ordered();
        const std::pair<Thumb, int> thumbs[] = {{Thumb::Low, current.first},
                                                {Thumb::High, current.second}};
        p.setPen(Qt::NoPen);
        for (const auto &[which, endpoint] : thumbs) {
            const bool active = m_editable
                && (m_dragging == which || (m_dragging == Thumb::None && m_hovered == which));
            const qreal width = active ? kThumbActiveWidth : kThumbWidth;
            p.setBrush(active ? accent : palette().color(QPalette::Text));
            p.drawRoundedRect(QRectF(thumbCentre(trough, endpoint, width) - width / 2.0,
                                     trough.top() - kThumbOverhang, width,
                                     trough.height() + 2 * kThumbOverhang),
                              kThumbRadius, kThumbRadius);
        }
    }

    if (!m_text.isEmpty()) {
        p.setPen(palette().color(QPalette::Text));
        p.drawText(trough, Qt::AlignCenter, m_text);
    }
}

} // namespace hidcenter::simsonn_pedals
