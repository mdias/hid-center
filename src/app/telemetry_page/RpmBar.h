// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "core/telemetry/TelemetryHub.h"

namespace hidcenter {

/// The engine-speed bar every wheel rim has across its top: a row of segments
/// filling green through amber to red as the revs climb.
///
/// The colours are the point rather than decoration. A single-colour bar says
/// only "how full"; this shape is read at a glance for the one thing that
/// matters while driving, which is how close the top is.
///
/// Full scale is the car's rev limiter, so the colours mean the same thing in
/// every car: red is the last fifth of *this* engine's range, not of some fixed
/// number. See setMaximum().
///
/// When editable, clicking or dragging sets the value, which is how the parts
/// downstream of telemetry are exercised with no sim running.
class RpmBar : public QWidget
{
    Q_OBJECT

public:
    explicit RpmBar(QWidget *parent = nullptr);

    int value() const { return m_value; }

    /// Clamped to 0..maximum(). Emits valueChanged() only on a change, so
    /// feeding it back its own signal cannot loop.
    void setValue(int rpm);

    int maximum() const { return m_maximum; }

    /// Full scale — normally the car's rev limiter, which changes with the car.
    /// Zero or less falls back to kTelemetryRpmMax rather than leaving a bar
    /// that cannot be filled.
    ///
    /// A maximum that drops below the current value clamps it and emits
    /// valueChanged(), exactly as setValue() would: quietly moving the value
    /// while claiming only the scale changed is how the two get out of step.
    void setMaximum(int rpm);

    /// Whether the mouse may set the value. Read-only is the resting state: the
    /// bar is a readout, and only test mode makes it an input.
    bool isEditable() const { return m_editable; }
    void setEditable(bool editable);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void valueChanged(int rpm);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    /// Value at a horizontal position, clamped to the range.
    int valueAt(int x) const;

    int m_value = 0;
    int m_maximum = kTelemetryRpmMax;
    bool m_editable = false;
};

} // namespace hidcenter
