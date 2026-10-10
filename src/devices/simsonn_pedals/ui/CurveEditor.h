// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QVector>
#include <QWidget>

#include <optional>

namespace hidcenter::simsonn_pedals {

/// An editable response curve mapping pedal input (0..100, the x axis) to
/// pedal output (-100..100, the y axis).
///
/// The curve is defined by kPointCount control points at fixed, evenly spaced
/// input positions. Dragging a point moves it vertically only, and the drawn
/// curve is the smooth spline that passes through every point. Buttons on the
/// left of the graph load standard preset curves.
///
/// The graph can also show where the pedal is right now — see setInput() — so
/// the curve being edited and the travel it is being edited for are read in one
/// place.
class CurveEditor : public QWidget
{
    Q_OBJECT

public:
    /// Eleven, because that is how many the device stores: points 0 to 10,
    /// signed bytes in -100..100 at evenly spaced inputs. A graph with a
    /// different number of handles from the hardware could not show what the
    /// hardware holds.
    static constexpr int kPointCount = 11;

    /// Standard curves the preset buttons load.
    enum class Preset {
        Linear,      ///< -100 to +100 across the full travel.
        Exponential, ///< Slow at first, steeper towards the end.
        Progressive, ///< Steep at first, gentle towards the end.
        Regressive,  ///< Faint response most of the way, steep at the very end.
        S_Curve,     ///< Gentle at both ends, steeper through the middle.
    };

    explicit CurveEditor(QWidget *parent = nullptr);

    /// The current point values (-100..100), in increasing input order.
    QVector<double> values() const { return m_values; }

    /// One point's current value, for the graph's drawing and hit-testing.
    double valueAt(int index) const { return m_values.at(index); }

    /// Load one of the built-in curves into the control points.
    void applyPreset(Preset preset);

    /// Put a whole curve into the control points — the device's own, typically.
    ///
    /// **Silent**: no changed(). A curve read off the hardware is not an edit,
    /// and announcing it as one would have the application report a change
    /// nobody made. A vector of the wrong length is ignored rather than
    /// part-applied: half a curve is not a curve.
    void setValues(const QVector<double> &values);

    /// Where the pedal is, as a percentage of its travel (0..100), or nothing
    /// when nothing is reading it — an absent value draws no live marks at all,
    /// which is the honest thing to show when the device is not being read.
    ///
    /// Values outside the range are clamped rather than refused: a device whose
    /// calibration lets it report slightly past its endpoints should peg the
    /// marker at the edge, not make it vanish.
    void setInput(const std::optional<double> &input);
    const std::optional<double> &input() const { return m_input; }

    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

Q_SIGNALS:
    /// Emitted whenever the points change, by a drag or by a preset.
    void changed();

private:
    void movePoint(int index, double value);

    /// Height that makes the plot square for a graph `graphWidth` wide.
    int heightForGraphWidth(int graphWidth) const;

    /// Smallest edge of the square plot area inside the graph.
    static constexpr int kFrameWidth = 48;   ///< Axis labels left of, and margin right of, the plot.
    static constexpr int kFrameHeight = 26;  ///< Margin above, and axis labels below, the plot.
    static constexpr int kMinimumPlot = 200;
    static constexpr int kSpacing = 8;

    class Graph;  ///< The painted graph; defined in the .cpp.

    QVector<double> m_values;  ///< size kPointCount
    std::optional<double> m_input;  ///< Live pedal position, 0..100.
    Graph *m_graph = nullptr;
    QWidget *m_presets = nullptr;
};

} // namespace hidcenter::simsonn_pedals