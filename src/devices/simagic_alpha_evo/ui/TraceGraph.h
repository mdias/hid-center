// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QVector>
#include <QWidget>

#include <array>

class QTimer;

namespace hidcenter::simagic_alpha_evo {

/// How often the trace is redrawn, and so how often anything the samples cause is
/// paid for.
///
/// Samples arrive at about a kilohertz. A frame per sample would be a repaint
/// every millisecond of a widget as wide as the window, which is the entire
/// reason this widget buffers rather than drawing what it is handed.
///
/// 60 rather than 30 because at the default zoom the trace moves a pixel per
/// sample — a thousand pixels a second — and at 30 Hz that advances in visible
/// jumps of thirty-odd pixels. This is the one number to change if the trace ever
/// costs more than it is worth.
inline constexpr int kTraceRepaintHz = 60;

/// How many samples the intake ring holds between two frames.
///
/// Sized for a stall rather than for the seventeen a frame normally brings: a
/// frame that is late because the GUI thread was busy elsewhere must not lose
/// samples that had already arrived. At a kilohertz this is four seconds of
/// slack. Overruns are counted rather than passed over in silence — see
/// TraceGraph::droppedSamples().
inline constexpr int kTraceIntakeCapacity = 4096;

/// One pixel column of the trace: what the samples that landed in it did.
///
/// `last` is here so a column can be joined to the one before it. Without it a
/// column whose samples never moved is a single dot, and at 1:1 zoom — where
/// every column holds exactly one sample — the trace would be a dotted line
/// rather than the connected one it should be. See traceSegment().
struct TraceColumn
{
    float min = 0.0f;
    float max = 0.0f;
    float last = 0.0f;

    /// Milliseconds on the widget's own clock when this column closed. The time
    /// axis is derived from these rather than from an assumed sample rate, so a
    /// base that reports at 500 Hz labels itself correctly and one that stutters
    /// is not quietly misdrawn.
    qint64 closedAtMs = 0;
};

/// The vertical extent one column draws, in sample units (-1.0 … +1.0).
struct TraceSpan
{
    float lo = 0.0f;
    float hi = 0.0f;
};

/// The extent to draw for one column, given the final value of the column before
/// it.
///
/// The union of the column's own range with that previous value. **One rule
/// covers both regimes.** Zoomed out it is the min/max envelope, so a
/// single-sample spike still draws to full height rather than being averaged into
/// invisibility. At 1:1, where min == max == last, it degenerates to a segment
/// running from the previous sample to this one — which is a connected line.
///
/// Free and pure, so the join can be checked without a widget.
TraceSpan traceSegment(const TraceColumn &column, float previousLast);

/// One rung of the zoom ladder.
///
/// Exactly one of the two is greater than 1 at any level, so a level is never
/// both stretched and aggregated: either a sample owns several pixels, or several
/// samples share one.
struct TraceZoomLevel
{
    int samplesPerColumn;  ///< Samples folded into one column.
    int columnWidthPx;     ///< How wide that column is drawn.
};

inline constexpr int kTraceZoomLevelCount = 9;

/// 1:1 — one sample, one pixel — which is the ladder's natural middle and about a
/// second of history across a window-width graph.
inline constexpr int kTraceDefaultZoomIndex = 3;

/// The ladder, coarsest detail last. The only place the zoom mapping lives.
const std::array<TraceZoomLevel, kTraceZoomLevelCount> &traceZoomLevels();

/// A scrolling scope: a signal in -1.0 … +1.0 against time, newest at the right,
/// with +1.0 at the bottom and -1.0 at the top.
///
/// **Nothing that arrives at sample rate touches the UI.** addSample() writes into
/// an intake ring and returns; a timer at kTraceRepaintHz drains that ring, folds
/// what it drained into columns, and repaints once. So the rate samples arrive at
/// has no bearing on how often anything is drawn, which is what makes a kilohertz
/// affordable. A caller may hit addSample() as fast as it likes.
///
/// **Running means wanted**, and stopping throws away what was held. The widget
/// has no opinion about whether it is on screen; deciding that is the job of
/// whatever put it there.
///
/// **The cost of a frame is bounded by the widget's width in pixels**, never by
/// the sample rate, because what is stored is one entry per pixel column and not
/// one per sample. That is also why nothing has to be kept: a column is the
/// min, max and final value of whatever landed in it, and the ring is exactly as
/// long as there are columns to show.
///
/// Time runs right to left with now pinned at the right edge, so the grid lines
/// are *static* and only the trace moves. They are laid out from a nominal rate that is replaced
/// once by a measured one, not the live one, and are redone when the zoom or
/// window size changes. They are drawn whether or not anything is running. Scrolling is sample-driven: when the
/// samples stop the trace freezes rather than sliding away, because the axis is
/// really "samples ago" and no more of them have happened.
///
/// Painted in logical coordinates, as everything in this application is. On a 2×
/// display that makes one sample column two physical pixels, so the trace is
/// drawn at half the resolution the panel could manage; the zoom ladder is the
/// answer to that, and matching the rest of the codebase is worth more than the
/// pixels.
///
/// The widget knows nothing about wheelbases. It is a display of a number between
/// -1 and 1, and whatever feeds it decides what that number means.
class TraceGraph : public QWidget
{
    Q_OBJECT

    /// The state as properties as well as accessors, for the same reason LedRing
    /// has them: it is what lets anything holding only a QWidget* — Designer, a
    /// stylesheet, a test — reach the zoom without linking against this class.
    Q_PROPERTY(int zoomIndex READ zoomIndex WRITE setZoomIndex NOTIFY zoomChanged)
    Q_PROPERTY(qreal spanSeconds READ spanSeconds NOTIFY zoomChanged)
    Q_PROPERTY(bool running READ isRunning WRITE setRunning)
    Q_PROPERTY(int columnsHeld READ columnsHeld)
    Q_PROPERTY(qint64 droppedSamples READ droppedSamples)

public:
    explicit TraceGraph(QWidget *parent = nullptr);

    /// Take one sample, in -1.0 … +1.0. **Buffers only.**
    ///
    /// This is the one function here a caller may reach a thousand times a
    /// second, so it is a bounds check and a store into the intake ring and
    /// nothing else. It folds no columns, emits no signal and — the whole point —
    /// never calls update(). Values outside the range are clamped rather than
    /// refused: a signal that saturates should draw as saturated.
    void addSample(qreal value);

    /// Whether the clock is running. Stopping it also clears, because a trace
    /// nobody is feeding is a picture of the past pretending to be the present.
    bool isRunning() const { return m_running; }
    void setRunning(bool running);

    /// Throw away everything held and start again from an empty graph.
    void clear();

    int zoomIndex() const { return m_zoomIndex; }

    /// Clamped to the ladder. Changing it clears, because the columns held mean
    /// something different at a different level and there is no honest way to
    /// reinterpret them.
    void setZoomIndex(int index);

    void zoomIn();
    void zoomOut();

    /// True while there is a rung left in that direction, which is what the
    /// page's buttons disable themselves on.
    bool canZoomIn() const { return m_zoomIndex > 0; }
    bool canZoomOut() const { return m_zoomIndex < kTraceZoomLevelCount - 1; }

    /// Seconds across the full width at the current zoom, or 0 before enough
    /// samples have arrived to measure a rate. Derived from sampleRateHz(), so it
    /// describes this device rather than an assumed kilohertz.
    qreal spanSeconds() const;

    /// The measured rate samples are arriving at, or 0 when not yet known.
    ///
    /// Taken from the timestamps of the oldest and newest columns actually held,
    /// so it needs no cooperation from whatever is feeding the widget and cannot
    /// disagree with what is on screen.
    qreal sampleRateHz() const;

    /// How many columns of history are on screen. Rises to the widget's width in
    /// columns and stops there, because that is all there is room to draw.
    int columnsHeld() const { return m_columnCount; }

    /// The lowest and highest the signal reached anywhere on screen — the peak of
    /// what is being shown, which is the number a force trace is usually being
    /// watched for. {0, 0} while nothing is held.
    TraceSpan visibleExtent() const;

    /// Samples the intake ring had to drop because nothing drained it in time.
    /// Should stay 0; if it does not, the graph is lying about the signal and the
    /// page says so. Silent loss would look exactly like a device gone quiet.
    qint64 droppedSamples() const { return m_dropped; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The zoom level, and therefore the span, changed.
    void zoomChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private Q_SLOTS:
    /// Drain the intake, fold it into columns, and repaint at most once. The only
    /// thing in this class that touches the UI.
    void tick();

private:
    /// Where the trace is drawn, inside the axis labels and the frame. Everything
    /// drawn is derived from this one rectangle.
    QRect plotRect() const;

    /// How many columns fit across plotRect() at the current zoom.
    int columnCapacity() const;

    /// Resize the column ring to match the widget and the zoom, discarding what
    /// it held. Called from both the resize and the zoom paths, which are the two
    /// ways the mapping from column to pixel can change.
    void rebuildColumns();

    /// Take the rate the time axis is laid out from, once there is enough history
    /// to measure it well. A no-op once taken; cleared with the columns.
    void latchAxisRate();
    void setMeasuredAxisRate(qreal measured);

    /// Fold one drained sample into the column being built, closing it and
    /// pushing it when it is full.
    void foldSample(float value);

    /// The column `age` places back from the newest, or nullptr past the end.
    const TraceColumn *columnFromEnd(int age) const;

    // --- The intake -----------------------------------------------------------
    // Written by addSample() at sample rate, drained by tick(). A plain ring:
    // fixed capacity, no allocation once built.
    QVector<float> m_intake;
    int m_intakeHead = 0;  ///< Next slot to write.
    int m_intakeCount = 0;
    qint64 m_dropped = 0;

    // --- The columns ----------------------------------------------------------
    QVector<TraceColumn> m_columns;  ///< Ring, columnCapacity() long.
    int m_columnHead = 0;            ///< Next slot to write.
    int m_columnCount = 0;

    /// The column being accumulated, not yet in the ring.
    TraceColumn m_building;
    int m_buildingCount = 0;

    // Scratch for painting, kept between frames so a frame allocates nothing.
    // This is not a cached picture — it is reused storage for the one call that
    // draws the trace, refilled from the columns every time.
    QVector<QRect> m_bars;
    QPolygonF m_polyline;

    /// The rate the time grid is drawn from: nominal until a measured one has
    /// been taken, then fixed for the life of the columns so the grid does not
    /// move as the trace draws.
    qreal m_axisRateHz = 1000.0;  ///< kNominalSampleRateHz.
    bool m_axisRateMeasured = false;

    QTimer *m_timer = nullptr;

    /// The clock columns are stamped from. Monotonic and started once, so a
    /// system clock change cannot make the time axis run backwards.
    QElapsedTimer m_clock;

    int m_zoomIndex = kTraceDefaultZoomIndex;
    bool m_running = false;
};

} // namespace hidcenter::simagic_alpha_evo
