// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRectF>
#include <QWidget>

#include <array>

#include "core/telemetry/TelemetryHub.h"

namespace hidcenter {

/// The four corners of the car, as a sim dash draws them: each wheel three tyre
/// strips across the contact patch with a brake bar beneath, laid out in a 2×2
/// grid in the positions the wheels actually occupy.
///
/// The colours are the point rather than decoration, as they are on RpmBar. A
/// column of sixteen numbers says nothing at a glance; blue through green to red
/// says which corner is cold and which is going off, before any of the numbers
/// have been read.
///
/// **The tyre scale is the car's own.** It centres on this compound's optimal
/// temperature, which arrives on the frame per wheel, so green means "right for
/// this tyre" in a GT3 and in a Hypercar alike rather than "near some fixed
/// number". Wheels reporting no optimum fall back to kTelemetryOptimalTyreTempC.
/// Brakes have no optimum in the telemetry, so they get a fixed band — the same
/// ramp, so one colour vocabulary covers the whole cell.
///
/// When editable, dragging sets a value: up and down within a tyre strip, along
/// the brake bar, each following the element's long axis. That is how the parts
/// downstream of telemetry get exercised with no sim running.
class WheelTempDisplay : public QWidget
{
    Q_OBJECT

public:
    explicit WheelTempDisplay(QWidget *parent = nullptr);

    std::array<TelemetryWheel, TelemetryWheelCount> wheels() const { return m_wheels; }

    /// Emits valueChanged() only on a change, so feeding this back its own
    /// signal cannot loop.
    void setWheels(const std::array<TelemetryWheel, TelemetryWheelCount> &wheels);

    /// Whether the mouse may set the values. Read-only is the resting state:
    /// this is a readout, and only test mode makes it an input.
    bool isEditable() const { return m_editable; }
    void setEditable(bool editable);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void valueChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    /// One draggable rectangle: which corner, and which reading within it.
    struct Element
    {
        QRectF box;
        int wheel = -1;

        /// 0..2 for the tyre bands across the patch, or 3 for the brake bar
        /// beneath them.
        int band = -1;
    };

    /// Recomputed on every paint and kept for hit-testing, so the rectangles the
    /// mouse is tested against are by construction the ones on screen.
    void layoutElements();

    /// The element under a point, or an Element with wheel < 0 for none.
    Element elementAt(const QPointF &point) const;

    /// Set one reading from a position inside its own rectangle, and announce it
    /// if it actually moved.
    void setFromDrag(const Element &element, const QPointF &point);

    /// Resize cursor matching the axis of whatever is under the point: vertical
    /// for a tyre strip, horizontal for the brake bar, plain elsewhere.
    void updateCursor(const QPointF &point);

    std::array<TelemetryWheel, TelemetryWheelCount> m_wheels;
    QList<Element> m_elements;

    /// What a press grabbed, held for the length of the drag. Without it a drag
    /// that wanders out of a narrow strip would start writing to its neighbour.
    Element m_dragging;

    /// Where the press landed and the reading it started from. Tyres move
    /// relative to these, so a drag nudges the value rather than jumping to the
    /// mouse.
    QPointF m_dragOrigin;
    int m_dragOriginValue = 0;

    bool m_editable = false;
};

} // namespace hidcenter
