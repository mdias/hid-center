// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

#include <optional>
#include <utility>

namespace hidcenter::simsonn_pedals {

/// A progress bar with the pedal's stored calibration marked on it.
///
/// A plain QProgressBar would do for the live value alone, but not for the two
/// things that make this readable. The first is the pair of **thumbs** at the
/// calibration endpoints, which is what turns a number into a judgement: a live
/// value short of the upper thumb is a pedal that cannot reach full output, and
/// one past it is travel the device is throwing away. The second is that the
/// trough spans the sensor's whole range, of which a pedal uses a fraction, so
/// the calibrated span has to be drawn as a band rather than left implied.
///
/// The thumbs are also **draggable**, which is the other half of the same idea:
/// once an endpoint is visible against the live value, the obvious thing to want
/// is to nudge it. So a bar is an input as well as a reading, and dragging a
/// thumb emits calibrationEdited() rather than quietly keeping the new value —
/// what an endpoint means, and what happens to it next, is the calibration's
/// business and not the bar's.
///
/// Painted rather than styled for the same reason CurveEditor is: the colours
/// come from the palette, so it follows the application's theme instead of
/// pinning a set of its own.
class PedalBar : public QWidget
{
    Q_OBJECT

public:
    explicit PedalBar(QWidget *parent = nullptr);

    /// The range the trough spans. Both ends are raw sensor counts.
    void setRange(int minimum, int maximum);

    /// Where the pedal is now, or nothing when it is not being read — which
    /// draws no fill at all, rather than a fill at the floor that would look
    /// like a real reading of zero.
    void setValue(const std::optional<int> &value);

    /// The stored calibration endpoints, low then high, or nothing when they
    /// have not been read. Values outside the range are clamped rather than
    /// refused, and a pair the wrong way round is taken as given: a device may
    /// hold anything, and hiding that would defeat the point of showing it.
    void setCalibration(const std::optional<std::pair<int, int>> &calibration);

    /// The line drawn across the middle. Usually the value in words.
    void setText(const QString &text);

    /// Whether the thumbs can be dragged. Off by default, and turned off again
    /// while a calibration is being written: a widget that still moves under the
    /// mouse when its value can no longer be acted on is lying about what it can
    /// do. A bar with no calibration set has no thumbs and so nothing to drag,
    /// whatever this says.
    void setEditable(bool editable);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// A thumb was dragged: the endpoints as they now stand, low then high,
    /// already clamped into range and kept from crossing. Emitted continuously
    /// while the mouse moves, not once on release, because the point of dragging
    /// an endpoint is watching what it does to the reading beside it.
    void calibrationEdited(int low, int high);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class Thumb { None, Low, High };

    QRectF troughRect() const;

    /// Where `value` falls across the trough.
    qreal toX(const QRectF &trough, int value) const;

    /// The same, pulled inside the widget so a thumb of that width sitting on
    /// either extreme of the range is still drawn — and still grabbable — in
    /// full. This and not toX() is where a thumb actually is.
    qreal thumbCentre(const QRectF &trough, int value, qreal width) const;

    /// The count at a position across the trough: toX() read backwards.
    int fromX(const QRectF &trough, qreal x) const;

    /// Which thumb is within grabbing distance of `position`, the nearer one
    /// when both are. None when nothing is close enough, or when the bar is not
    /// editable, or when there is no calibration to grab.
    Thumb thumbAt(const QPointF &position) const;

    /// Move the thumb being dragged to `x`, and say so if it went anywhere.
    void dragTo(qreal x);

    /// The calibration as low-then-high, whichever way round it was given.
    std::pair<int, int> ordered() const;

    void setHovered(Thumb thumb);

    int m_minimum = 0;
    int m_maximum = 100;
    std::optional<int> m_value;
    std::optional<std::pair<int, int>> m_calibration;
    QString m_text;

    bool m_editable = false;
    Thumb m_hovered = Thumb::None;
    Thumb m_dragging = Thumb::None;

    /// How far the grab was from the thumb's centre, so the thumb does not jump
    /// out from under the cursor on the first press.
    qreal m_grabOffset = 0.0;
};

} // namespace hidcenter::simsonn_pedals
