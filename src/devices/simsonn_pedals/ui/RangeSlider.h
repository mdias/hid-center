// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

namespace hidcenter::simsonn_pedals {

/// A slider with two thumbs: a low value and a high value on one scale.
///
/// Qt has no such thing, and the pair of spin boxes it takes to stand in for one
/// hides the only thing that matters about these two numbers — that they are two
/// ends of the same range, and that what lies between them is the part that gets
/// used. A slider says that in one glance and cannot be set to a low above its
/// high.
///
/// Used for a pedal's deadzones, which is why it reads as percentages: the low
/// thumb is where the pedal starts to register and the high thumb is where it
/// reaches full output. The widget itself knows nothing about pedals, though —
/// it is a range on a scale, and PedalBar is the one that knows about raw counts
/// and calibration.
///
/// Painted rather than styled, from the palette, for the same reason PedalBar
/// and CurveEditor are: it follows the application's theme instead of pinning
/// colours of its own.
class RangeSlider : public QWidget
{
    Q_OBJECT

public:
    explicit RangeSlider(QWidget *parent = nullptr);

    /// The scale the trough spans. Defaults to 0..100.
    void setScale(int minimum, int maximum);

    int low() const { return m_low; }
    int high() const { return m_high; }

    /// Put the thumbs somewhere, clamped into the scale and kept apart.
    ///
    /// **Silent**: no valuesChanged(). This is how a value arrives from
    /// somewhere else — the device's own settings, say — and echoing that back
    /// as an edit would make every load look like something the user did.
    void setValues(int low, int high);

    /// The smallest gap allowed between the thumbs, in scale units. One by
    /// default, so the two can never coincide: a range with nothing in it would
    /// be a pedal with no travel at all.
    void setMinimumSeparation(int separation);

    /// The unit drawn after each number. A percent sign by default.
    void setSuffix(const QString &suffix);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// A thumb was dragged. Emitted continuously through the drag, not once at
    /// the end, so whatever is watching can follow the value as it moves.
    void valuesChanged(int low, int high);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class Thumb { None, Low, High };

    QRectF troughRect() const;
    qreal toX(const QRectF &trough, int value) const;

    /// Where a thumb of that width actually sits: pulled inside the widget, so
    /// one at either extreme of the scale is still drawn, and still grabbable,
    /// in full.
    qreal thumbCentre(const QRectF &trough, int value, qreal width) const;
    int fromX(const QRectF &trough, qreal x) const;

    /// Which thumb is within grabbing distance, the nearer one when both are.
    Thumb thumbAt(const QPointF &position) const;

    void dragTo(qreal x);
    void setHovered(Thumb thumb);
    QString text() const;

    int m_minimum = 0;
    int m_maximum = 100;
    int m_low = 0;
    int m_high = 100;
    int m_separation = 1;
    QString m_suffix = QStringLiteral(" %");

    Thumb m_hovered = Thumb::None;
    Thumb m_dragging = Thumb::None;

    /// How far the grab was from the thumb's centre, so the thumb does not jump
    /// out from under the cursor on the first press.
    qreal m_grabOffset = 0.0;
};

} // namespace hidcenter::simsonn_pedals
