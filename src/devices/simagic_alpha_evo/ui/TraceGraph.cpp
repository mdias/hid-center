// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TraceGraph.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Room down the left for the value labels. Wide enough for "+1.0" in the
/// smaller font the axis is drawn in.
constexpr int kValueLabelWidth = 36;

/// Room along the bottom for the time labels, which sit under the plot rather
/// than inside it — a label inside a trace that reaches full scale is unreadable
/// exactly when the trace is most interesting.
constexpr int kTimeLabelHeight = 16;

/// Breathing room between the frame and the widget's edge.
constexpr int kPlotMargin = 4;

/// Where the horizontal grid lines go, as a fraction of full scale. Zero is drawn
/// solid and the rest dashed, so the centre line reads as the reference and the
/// others as measurement.
constexpr std::array<double, 3> kValueGridLines = {0.0, 0.5, -0.5};

/// Axis text is drawn a point smaller than the widget's font: it is reference,
/// not content, and at full size it competes with the trace.
constexpr int kAxisFontDelta = -1;

/// Candidate spacings for the time grid, in seconds, in the 1-2-5 sequence that
/// keeps labels on round numbers at every zoom.
constexpr std::array<double, 12> kTimeGridSteps = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                                   1.0,  2.0,  5.0,  10.0, 20.0, 30.0};

/// Aim for a grid line about this far apart, in pixels. The step actually used is
/// the first of kTimeGridSteps that clears it.
constexpr int kTimeGridTargetPx = 110;

/// How much history has to be held before the rate the time axis is laid out
/// from is taken. Long enough that the frame-to-frame jitter in the column
/// timestamps is a rounding error on the measurement.
constexpr qint64 kAxisRateMinHistoryMs = 1000;

/// The rate the time axis is laid out from until one has been measured. The
/// wheelbase reports at about a kilohertz, and the window size is known before
/// the first sample, so this lets the axis be there from the start.
constexpr qreal kNominalSampleRateHz = 1000.0;

/// A measured rate within this fraction of the nominal one is taken to *be* the
/// nominal one, so the grid does not shift for a measurement that only differs
/// by timer jitter.
constexpr qreal kNominalRateTolerance = 0.05;

} // namespace

// --- The pure parts ---------------------------------------------------------

const std::array<TraceZoomLevel, kTraceZoomLevelCount> &traceZoomLevels()
{
    // Finest first. Above the default a sample is stretched across several
    // pixels; below it several samples share one and are drawn as an envelope.
    static const std::array<TraceZoomLevel, kTraceZoomLevelCount> levels = {{
        {1, 8},
        {1, 4},
        {1, 2},
        {1, 1},  // kTraceDefaultZoomIndex: one sample, one pixel.
        {2, 1},
        {4, 1},
        {8, 1},
        {16, 1},
        {32, 1},
    }};
    return levels;
}

TraceSpan traceSegment(const TraceColumn &column, float previousLast)
{
    return {std::min(column.min, previousLast), std::max(column.max, previousLast)};
}

// --- Construction -----------------------------------------------------------

TraceGraph::TraceGraph(QWidget *parent)
    : QWidget(parent)
{
    m_intake.resize(kTraceIntakeCapacity);
    m_clock.start();

    // The timer lives on this thread with the widget, unlike the module's worker
    // timers: there is no worker here, because whatever feeds this widget is
    // already on the GUI thread.
    m_timer = new QTimer(this);
    m_timer->setInterval(1000 / kTraceRepaintHz);
    connect(m_timer, &QTimer::timeout, this, &TraceGraph::tick);

    rebuildColumns();
}

QSize TraceGraph::sizeHint() const
{
    return {480, 160};
}

QSize TraceGraph::minimumSizeHint() const
{
    return {240, 90};
}

// --- Taking samples ---------------------------------------------------------

void TraceGraph::addSample(qreal value)
{
    if (!m_running)
        return;

    const float clamped = float(std::clamp(value, qreal(-1.0), qreal(1.0)));

    m_intake[m_intakeHead] = clamped;
    m_intakeHead = (m_intakeHead + 1) % kTraceIntakeCapacity;

    if (m_intakeCount < kTraceIntakeCapacity) {
        ++m_intakeCount;
    } else {
        // Full, so that write landed on the oldest sample still waiting. Losing
        // the oldest is the right end to lose from — this is a scope, and what
        // just happened matters more than what happened four seconds ago — but it
        // is still loss, and it is counted rather than passed over.
        ++m_dropped;
    }
}

void TraceGraph::tick()
{
    const int drained = m_intakeCount;
    if (drained == 0)
        return;  // Nothing arrived, so nothing to draw and no repaint to pay for.

    int read = (m_intakeHead - drained + kTraceIntakeCapacity) % kTraceIntakeCapacity;
    for (int i = 0; i < drained; ++i) {
        foldSample(m_intake[read]);
        read = (read + 1) % kTraceIntakeCapacity;
    }
    m_intakeCount = 0;

    latchAxisRate();

    // Running means wanted. Whether that is tied to a tab being the one on
    // screen is the page's business, not this widget's — the same call made for
    // the size policy.
    update();
}

void TraceGraph::latchAxisRate()
{
    if (m_axisRateMeasured || m_columnCount < 2)
        return;

    const qint64 historyMs = columnFromEnd(0)->closedAtMs - columnFromEnd(m_columnCount - 1)->closedAtMs;
    if (historyMs >= kAxisRateMinHistoryMs || m_columnCount == int(m_columns.size()))
        setMeasuredAxisRate(sampleRateHz());
}

void TraceGraph::setMeasuredAxisRate(qreal measured)
{
    if (measured <= 0.0)
        return;

    m_axisRateMeasured = true;

    const bool nearNominal =
        std::fabs(measured - kNominalSampleRateHz) <= kNominalSampleRateHz * kNominalRateTolerance;
    const qreal rate = nearNominal ? kNominalSampleRateHz : measured;
    if (rate != m_axisRateHz) {
        m_axisRateHz = rate;
        update();
    }
}

void TraceGraph::foldSample(float value)
{
    if (m_buildingCount == 0) {
        m_building.min = value;
        m_building.max = value;
    } else {
        m_building.min = std::min(m_building.min, value);
        m_building.max = std::max(m_building.max, value);
    }
    m_building.last = value;
    ++m_buildingCount;

    if (m_buildingCount < traceZoomLevels()[size_t(m_zoomIndex)].samplesPerColumn)
        return;

    m_building.closedAtMs = m_clock.elapsed();
    if (!m_columns.isEmpty()) {
        m_columns[m_columnHead] = m_building;
        m_columnHead = (m_columnHead + 1) % m_columns.size();
        m_columnCount = std::min(m_columnCount + 1, int(m_columns.size()));
    }

    m_building = {};
    m_buildingCount = 0;
}

// --- State ------------------------------------------------------------------

void TraceGraph::setRunning(bool running)
{
    if (m_running == running)
        return;

    m_running = running;
    clear();

    if (running)
        m_timer->start();
    else
        m_timer->stop();

    update();
}

void TraceGraph::clear()
{
    m_intakeHead = 0;
    m_intakeCount = 0;
    m_dropped = 0;

    m_columnHead = 0;
    m_columnCount = 0;
    m_building = {};
    m_buildingCount = 0;
    m_axisRateHz = kNominalSampleRateHz;
    m_axisRateMeasured = false;

    update();
}

void TraceGraph::setZoomIndex(int index)
{
    const int clamped = std::clamp(index, 0, kTraceZoomLevelCount - 1);
    if (clamped == m_zoomIndex)
        return;

    m_zoomIndex = clamped;

    // The columns held were folded under the old level, so they mean something
    // different now and there is no honest way to reinterpret them.
    rebuildColumns();
    m_intakeCount = 0;
    m_dropped = 0;  // It described the trace that has just been thrown away.

    Q_EMIT zoomChanged(m_zoomIndex);
    update();
}

void TraceGraph::zoomIn()
{
    setZoomIndex(m_zoomIndex - 1);
}

void TraceGraph::zoomOut()
{
    setZoomIndex(m_zoomIndex + 1);
}

// --- Geometry ---------------------------------------------------------------

QRect TraceGraph::plotRect() const
{
    const int left = kPlotMargin + kValueLabelWidth;
    const int top = kPlotMargin;
    const int right = width() - kPlotMargin;
    const int bottom = height() - kPlotMargin - kTimeLabelHeight;

    if (right <= left || bottom <= top)
        return {};

    return QRect(QPoint(left, top), QPoint(right, bottom));
}

int TraceGraph::columnCapacity() const
{
    const QRect plot = plotRect();
    if (plot.isEmpty())
        return 0;

    return std::max(0, plot.width() / traceZoomLevels()[size_t(m_zoomIndex)].columnWidthPx);
}

void TraceGraph::rebuildColumns()
{
    const int capacity = columnCapacity();

    m_columns.assign(capacity, TraceColumn{});
    m_columnHead = 0;
    m_columnCount = 0;
    m_building = {};
    m_buildingCount = 0;
    m_axisRateHz = kNominalSampleRateHz;
    m_axisRateMeasured = false;

    m_bars.reserve(capacity);
    m_polyline.reserve(capacity);
}

const TraceColumn *TraceGraph::columnFromEnd(int age) const
{
    if (age < 0 || age >= m_columnCount)
        return nullptr;

    const int size = int(m_columns.size());
    const int index = (m_columnHead - 1 - age + size * 2) % size;
    return &m_columns[index];
}

void TraceGraph::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // The column-to-pixel mapping just changed, and every column held was folded
    // under the old one.
    rebuildColumns();
}

// --- What the axis says -----------------------------------------------------

qreal TraceGraph::sampleRateHz() const
{
    if (m_columnCount < 2)
        return 0.0;

    const TraceColumn *newest = columnFromEnd(0);
    const TraceColumn *oldest = columnFromEnd(m_columnCount - 1);
    const qint64 elapsedMs = newest->closedAtMs - oldest->closedAtMs;
    if (elapsedMs <= 0)
        return 0.0;

    const qreal samples =
        qreal(m_columnCount - 1) * traceZoomLevels()[size_t(m_zoomIndex)].samplesPerColumn;
    return samples * 1000.0 / qreal(elapsedMs);
}

TraceSpan TraceGraph::visibleExtent() const
{
    if (m_columnCount == 0)
        return {};

    TraceSpan extent{1.0f, -1.0f};
    for (int age = 0; age < m_columnCount; ++age) {
        const TraceColumn *column = columnFromEnd(age);
        extent.lo = std::min(extent.lo, column->min);
        extent.hi = std::max(extent.hi, column->max);
    }
    return extent;
}

qreal TraceGraph::spanSeconds() const
{
    const qreal rate = sampleRateHz();
    if (rate <= 0.0)
        return 0.0;

    const int capacity = columnCapacity();
    if (capacity <= 0)
        return 0.0;

    return qreal(capacity) * traceZoomLevels()[size_t(m_zoomIndex)].samplesPerColumn / rate;
}

// --- Painting ---------------------------------------------------------------

void TraceGraph::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    const QRect plot = plotRect();
    if (plot.isEmpty())
        return;

    QPainter painter(this);

    // Deliberately no antialiasing. The grid is axis-aligned and looks better
    // without it, and on a trace this dense it buys nothing visible while being
    // the single largest cost in the raster engine. Text is unaffected: glyphs
    // are antialiased by the font engine whatever this hint says.
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QPalette &pal = palette();
    const qreal midY = plot.center().y() + 0.5;
    const qreal halfHeight = plot.height() / 2.0;
    // Positive is *down*: +1.0 at the bottom of the plot, -1.0 at the top.
    const auto yOf = [midY, halfHeight](qreal value) { return midY + value * halfHeight; };

    QFont axisFont = font();
    axisFont.setPointSize(std::max(6, axisFont.pointSize() + kAxisFontDelta));
    painter.setFont(axisFont);

    // --- The ground ---------------------------------------------------------
    painter.fillRect(plot, pal.color(QPalette::Base));

    // --- Horizontal grid and value labels -----------------------------------
    const QColor gridColour = pal.color(QPalette::Mid);
    const QColor textColour = pal.color(QPalette::Text);

    for (const double level : kValueGridLines) {
        QPen pen(gridColour);
        pen.setStyle(level == 0.0 ? Qt::SolidLine : Qt::DotLine);
        painter.setPen(pen);
        const int y = int(std::lround(yOf(level)));
        painter.drawLine(plot.left(), y, plot.right(), y);
    }

    painter.setPen(textColour);
    const QRect labelColumn(kPlotMargin, 0, kValueLabelWidth - 4, 0);
    const auto drawValueLabel = [&](qreal value, const QString &text) {
        const int y = int(std::lround(yOf(value)));
        const QRect box(labelColumn.x(), y - 8, labelColumn.width(), 16);
        painter.drawText(box, Qt::AlignRight | Qt::AlignVCenter, text);
    };
    drawValueLabel(1.0, QStringLiteral("+1.0"));
    drawValueLabel(0.0, QStringLiteral("0"));
    drawValueLabel(-1.0, QStringLiteral("-1.0"));

    // --- Vertical grid and time labels --------------------------------------
    // Anchored to now at the right edge, so these do not move: only the trace
    // does. Always drawn, from the nominal rate until a measured one replaces it
    // (latchAxisRate()), once, rather than from the live rate, which wanders by
    // a percent or so from frame to frame and would drag every line and label,
    // and flip the step between 1-2-5 candidates, as the trace drew.
    const TraceZoomLevel &level = traceZoomLevels()[size_t(m_zoomIndex)];
    const qreal rate = m_axisRateHz;
    {
        const qreal pixelsPerSecond = rate * level.columnWidthPx / level.samplesPerColumn;

        double step = kTimeGridSteps.back();
        for (const double candidate : kTimeGridSteps) {
            if (candidate * pixelsPerSecond >= kTimeGridTargetPx) {
                step = candidate;
                break;
            }
        }

        QPen pen(gridColour);
        pen.setStyle(Qt::DotLine);

        for (int i = 1;; ++i) {
            const qreal seconds = step * i;
            const int x = int(std::lround(plot.right() - seconds * pixelsPerSecond));
            if (x <= plot.left())
                break;

            painter.setPen(pen);
            painter.drawLine(x, plot.top(), x, plot.bottom());

            painter.setPen(textColour);
            const QString text = step < 1.0
                ? tr("-%1 ms").arg(std::lround(seconds * 1000.0))
                : tr("-%1 s").arg(seconds, 0, 'g', 2);
            painter.drawText(QRect(x - 40, plot.bottom() + 1, 80, kTimeLabelHeight),
                             Qt::AlignHCenter | Qt::AlignVCenter, text);
        }

        painter.setPen(textColour);
        painter.drawText(QRect(plot.right() - 40, plot.bottom() + 1, 40, kTimeLabelHeight),
                         Qt::AlignRight | Qt::AlignVCenter, tr("now"));
    }

    // --- The trace ----------------------------------------------------------
    // Two columns is the floor, because one column has no predecessor to be
    // joined to and so nothing to draw between.
    if (m_columnCount >= 2) {
        QPen tracePen(pal.color(QPalette::Highlight));
        tracePen.setWidth(1);
        // Square, so the last pixel of a stroke is drawn: with the default flat
        // cap a 1px line stops short of its end point.
        tracePen.setCapStyle(Qt::SquareCap);

        if (level.columnWidthPx == 1) {
            // One sample or more per pixel: draw each column's extent, joined to
            // the column before it. At 1:1 this degenerates to a connected line;
            // zoomed out it is the min/max envelope.
            //
            // Drawn as one-pixel-wide filled bars on whole pixels rather than as
            // lines. A line from y=10.5 to y=10.9 with a 1px pen and no
            // antialiasing covers *no* pixel — a flat-capped stroke that stays
            // inside one pixel row is dropped — and a trace whose steps are a
            // fraction of a pixel is nothing but such lines, so it came out with
            // gaps. A bar from floor(lo) to floor(hi) always covers both ends.
            painter.setPen(Qt::NoPen);
            painter.setBrush(pal.color(QPalette::Highlight));
            m_bars.clear();
            for (int age = 0; age < m_columnCount - 1; ++age) {
                const TraceColumn *column = columnFromEnd(age);
                const TraceColumn *previous = columnFromEnd(age + 1);
                const TraceSpan span = traceSegment(*column, previous->last);
                const qreal x = plot.right() - age;
                if (x < plot.left())
                    break;
                const int top = int(std::floor(yOf(span.lo)));
                const int bottom = int(std::floor(yOf(span.hi)));
                m_bars.append(QRect(int(x), top, 1, bottom - top + 1));
            }
            painter.drawRects(m_bars);
            painter.setBrush(Qt::NoBrush);
        } else {
            // A sample owns several pixels, so it is a point on a polyline rather
            // than a bar — eight disconnected ticks would not read as a signal.
            painter.setPen(tracePen);
            m_polyline.clear();
            for (int age = 0; age < m_columnCount; ++age) {
                const TraceColumn *column = columnFromEnd(age);
                const qreal x = plot.right() - qreal(age) * level.columnWidthPx;
                if (x < plot.left())
                    break;
                m_polyline.append(QPointF(x, yOf(column->last)));
            }
            painter.drawPolyline(m_polyline);
        }
    }

    // --- The frame ----------------------------------------------------------
    painter.setPen(pal.color(QPalette::Mid));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plot.adjusted(0, 0, -1, -1));
}

} // namespace hidcenter::simagic_alpha_evo
