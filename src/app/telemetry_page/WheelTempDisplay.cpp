// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/telemetry_page/WheelTempDisplay.h"

#include <QMouseEvent>
#include <QPainter>

namespace hidcenter {
namespace {

/// The brake bar's place in Element::band, one past the three tyre bands.
constexpr int kBrakeBand = 3;

constexpr int kCellGap = 10;
constexpr int kStripGap = 2;
constexpr int kBrakeGap = 3;
constexpr qreal kRadius = 2.0;

/// Gutter between a corner's frame and the coloured elements inside it. Wider
/// than RpmBar's, because here the fills reach the frame on all four sides and a
/// three-pixel gap leaves the outline unreadable against them.
constexpr qreal kInner = 5.0;

/// Height of the brake bar. Enough for its own text, and no more: the tyres are
/// what this widget is mostly for, and they should get the room.
constexpr int kBrakeHeight = 17;

/// How far either side of optimal the tyre scale reaches.
///
/// Not symmetric, and deliberately. A tyre that is thirty degrees cold is merely
/// slow; one that is thirty degrees hot is going off. The shorter road to red is
/// the true one.
constexpr int kBelowOptimal = 35;
constexpr int kAboveOptimal = 25;

/// Full scale for a brake disc, which the telemetry gives no optimum for. Cold
/// enough to be blue in the pit lane, hot enough that ordinary heavy braking
/// does not simply peg it red.
constexpr int kBrakeMinC = 0;
constexpr int kBrakeMaxC = 700;

/// How many strip-heights of mouse travel sweep a tyre's whole scale. Absolute
/// mapping made a few pixels worth several degrees.
constexpr qreal kTyreDragTravel = 3.0;

struct Scale
{
    int min = 0;
    int max = 0;
};

Scale tyreScale(const TelemetryWheel &wheel)
{
    // Zero means the sim did not say, not a tyre that wants to be frozen.
    const int optimal = wheel.optimalTyreTemp > 0 ? wheel.optimalTyreTemp
                                                  : kTelemetryOptimalTyreTempC;
    return {optimal - kBelowOptimal, optimal + kAboveOptimal};
}

Scale scaleFor(const TelemetryWheel &wheel, int band)
{
    return band == kBrakeBand ? Scale{kBrakeMinC, kBrakeMaxC} : tyreScale(wheel);
}

/// Where a temperature sits on its scale, 0..1.
qreal positionOf(int celsius, const Scale &scale)
{
    const int span = scale.max - scale.min;
    if (span <= 0)
        return 0.0;
    return qBound(0.0, qreal(celsius - scale.min) / span, 1.0);
}

int valueAt(qreal position, const Scale &scale)
{
    return scale.min + int(qRound(qBound(0.0, position, 1.0) * (scale.max - scale.min)));
}

/// Cold to hot, as one sweep of hue: blue through cyan and green to amber and
/// red. A sweep rather than the three fixed steps RpmBar uses, because a tyre
/// crossing from "nearly right" to "right" is a real, gradual thing, where revs
/// crossing into the last fifth is a threshold.
QColor temperatureColour(qreal position)
{
    constexpr qreal kColdHue = 220.0 / 360.0;
    const qreal hue = kColdHue * (1.0 - qBound(0.0, position, 1.0));
    return QColor::fromHsvF(hue, 0.72, 0.90);
}

/// Whichever of black or white can be read on top of a fill.
QColor textOn(const QColor &fill)
{
    return fill.lightnessF() > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

QString cornerName(int wheel)
{
    switch (wheel) {
    case TelemetryFrontLeft:
        return WheelTempDisplay::tr("FL");
    case TelemetryFrontRight:
        return WheelTempDisplay::tr("FR");
    case TelemetryRearLeft:
        return WheelTempDisplay::tr("RL");
    default:
        return WheelTempDisplay::tr("RR");
    }
}

} // namespace

WheelTempDisplay::WheelTempDisplay(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize WheelTempDisplay::sizeHint() const
{
    return {340, 190};
}

QSize WheelTempDisplay::minimumSizeHint() const
{
    // Below about this the tyre strips are too narrow to hold their numbers, and
    // a coloured block with no number in it is half a readout.
    return {260, 150};
}

void WheelTempDisplay::setWheels(const std::array<TelemetryWheel, TelemetryWheelCount> &wheels)
{
    if (wheels == m_wheels)
        return;

    m_wheels = wheels;
    update();
    Q_EMIT valueChanged();
}

void WheelTempDisplay::setEditable(bool editable)
{
    if (m_editable == editable)
        return;

    m_editable = editable;
    // Tracked so the cursor can follow the element under it without a button down.
    setMouseTracking(m_editable);
    unsetCursor();
    // Not setEnabled(false) for the read-only case: this is a live readout, and
    // greying it out would cost exactly the legibility it exists for. The frame
    // around each corner is what changes instead; see paintEvent().
    update();
}

void WheelTempDisplay::layoutElements()
{
    m_elements.clear();

    const int labelHeight = fontMetrics().height();
    const qreal cellWidth = (width() - kCellGap) / 2.0;
    const qreal cellHeight = (height() - kCellGap) / 2.0;
    if (cellWidth <= 0 || cellHeight <= labelHeight)
        return;

    for (int wheel = 0; wheel < TelemetryWheelCount; ++wheel) {
        // Front row first, left before right — which is the order the frame
        // carries them in, so the grid is the enum laid out on the page.
        const int column = wheel % 2;
        const int row = wheel / 2;

        const QRectF group(column * (cellWidth + kCellGap),
                           row * (cellHeight + kCellGap) + labelHeight, cellWidth,
                           cellHeight - labelHeight);

        const QRectF inside = group.adjusted(kInner, kInner, -kInner, -kInner);
        const qreal tyreHeight = inside.height() - kBrakeHeight - kBrakeGap;
        if (inside.width() <= 0 || tyreHeight <= 0)
            return;

        const qreal stripWidth = (inside.width() - 2 * kStripGap) / 3.0;
        for (int band = 0; band < 3; ++band) {
            m_elements.append({QRectF(inside.left() + band * (stripWidth + kStripGap),
                                      inside.top(), stripWidth, tyreHeight),
                               wheel, band});
        }

        m_elements.append({QRectF(inside.left(), inside.bottom() - kBrakeHeight, inside.width(),
                                  kBrakeHeight),
                           wheel, kBrakeBand});
    }
}

WheelTempDisplay::Element WheelTempDisplay::elementAt(const QPointF &point) const
{
    for (const Element &element : m_elements) {
        if (element.box.contains(point))
            return element;
    }
    return {};
}

void WheelTempDisplay::setFromDrag(const Element &element, const QPointF &point)
{
    if (element.wheel < 0)
        return;

    TelemetryWheel &wheel = m_wheels[element.wheel];
    const Scale scale = scaleFor(wheel, element.band);

    // Along each element's long axis: up the tyre strip is hotter, and right
    // along the brake bar is hotter. Anything else would have the mouse moving
    // one way and the fill another.
    qreal position = 0.0;
    if (element.band == kBrakeBand) {
        if (element.box.width() <= 0)
            return;
        position = (point.x() - element.box.left()) / element.box.width();
    } else {
        if (element.box.height() <= 0)
            return;
        // Relative to where the drag began, and slowed: see kTyreDragTravel.
        // Unclamped origin: a tyre already below the scale's floor must not snap up.
        position = qreal(m_dragOriginValue - scale.min) / (scale.max - scale.min)
                   + (m_dragOrigin.y() - point.y()) / (element.box.height() * kTyreDragTravel);
    }

    // The colour scale bottoms out well above zero, but a tyre may be set right
    // down to 0 °C; that just reads as fully cold.
    const int value = element.band == kBrakeBand
                          ? valueAt(position, scale)
                          : qMax(0, scale.min + int(qRound(qMin(position, 1.0)
                                                           * (scale.max - scale.min))));
    int &target = element.band == kBrakeBand ? wheel.brakeTemp : wheel.tyreTemp[element.band];
    if (target == value)
        return;

    target = value;
    update();
    Q_EMIT valueChanged();
}

void WheelTempDisplay::updateCursor(const QPointF &point)
{
    const Element element = elementAt(point);
    if (element.wheel < 0)
        unsetCursor();
    else
        setCursor(element.band == kBrakeBand ? Qt::SizeHorCursor : Qt::SizeVerCursor);
}

void WheelTempDisplay::mousePressEvent(QMouseEvent *event)
{
    if (!m_editable) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_dragging = elementAt(event->position());
    if (m_dragging.wheel < 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    const TelemetryWheel &wheel = m_wheels[m_dragging.wheel];
    m_dragOrigin = event->position();
    m_dragOriginValue = m_dragging.band == kBrakeBand ? wheel.brakeTemp
                                                      : wheel.tyreTemp[m_dragging.band];
    // A tyre press alone changes nothing; the brake bar still jumps to the click.
    if (m_dragging.band == kBrakeBand)
        setFromDrag(m_dragging, event->position());
    event->accept();
}

void WheelTempDisplay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_editable || m_dragging.wheel < 0 || !(event->buttons() & Qt::LeftButton)) {
        if (m_editable)
            updateCursor(event->position());
        QWidget::mouseMoveEvent(event);
        return;
    }

    // Deliberately not re-hit-testing: the drag belongs to whatever was pressed,
    // however far outside it the mouse has since wandered.
    setFromDrag(m_dragging, event->position());
    event->accept();
}

void WheelTempDisplay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    layoutElements();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont valueFont = font();
    valueFont.setPointSizeF(valueFont.pointSizeF() * 0.9);

    // A dotted outline while read-only, solid while editable: the one visible
    // difference between "you may drag this" and "this is showing you a value",
    // and the same tell RpmBar uses.
    //
    // Faded text colour rather than QPalette::Mid, which RpmBar can afford
    // because its frame stands on the window. These frames are read against four
    // blocks of saturated colour, and on a dark theme Mid is the background.
    QColor frameColour = palette().color(QPalette::WindowText);
    frameColour.setAlphaF(0.45);
    QPen frame(frameColour);
    frame.setStyle(m_editable ? Qt::SolidLine : Qt::DotLine);

    const int labelHeight = fontMetrics().height();
    const qreal cellWidth = (width() - kCellGap) / 2.0;
    const qreal cellHeight = (height() - kCellGap) / 2.0;

    for (int wheel = 0; wheel < TelemetryWheelCount; ++wheel) {
        const int column = wheel % 2;
        const int row = wheel / 2;
        const qreal left = column * (cellWidth + kCellGap);
        const qreal top = row * (cellHeight + kCellGap);

        painter.setPen(palette().color(QPalette::WindowText));
        painter.setFont(font());
        painter.drawText(QRectF(left, top, cellWidth, labelHeight),
                         Qt::AlignLeft | Qt::AlignVCenter, cornerName(wheel));

        painter.setPen(frame);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(left + 0.5, top + labelHeight + 0.5, cellWidth - 1,
                                       cellHeight - labelHeight - 1),
                                kRadius, kRadius);
    }

    painter.setFont(valueFont);

    for (const Element &element : m_elements) {
        const TelemetryWheel &wheel = m_wheels[element.wheel];
        const bool brake = element.band == kBrakeBand;
        const int celsius = brake ? wheel.brakeTemp : wheel.tyreTemp[element.band];

        const QColor fill = temperatureColour(positionOf(celsius, scaleFor(wheel, element.band)));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(element.box, kRadius, kRadius);

        // The brake reading says what it is; the tyre bands are three of a kind
        // and a unit on each would only be noise.
        const QString text = brake ? tr("Brake %1 °C").arg(celsius) : QString::number(celsius);

        painter.setPen(textOn(fill));
        painter.drawText(element.box, Qt::AlignCenter, text);
    }
}

} // namespace hidcenter
