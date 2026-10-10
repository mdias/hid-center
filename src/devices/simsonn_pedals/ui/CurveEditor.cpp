// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CurveEditor.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace hidcenter::simsonn_pedals {
namespace {

constexpr double kInputMin = 0.0;
constexpr double kInputMax = 100.0;
constexpr double kOutputMin = -100.0;
constexpr double kOutputMax = 100.0;

/// Room around the plot: value labels down the left, input labels below, a thin
/// frame gap everywhere else.
constexpr int kAxisLabelWidth = 40;
constexpr int kAxisLabelHeight = 18;
constexpr int kPlotMargin = 8;
static_assert(kAxisLabelWidth + kPlotMargin == 48 && kPlotMargin + kAxisLabelHeight == 26,
              "CurveEditor::kFrameWidth/kFrameHeight must match the graph's margins");

/// How close the mouse must be to a control point to grab it. Deliberately
/// generous: the points are small targets and there are eleven of them, so a
/// tighter radius would make dragging feel fiddly.
constexpr int kGrabRadius = 14;

/// Radius of the marker a control point is drawn with.
constexpr int kMarkerRadius = 4;

/// The live position marks: the area under the curve up to where the pedal is,
/// and the cross through the point the pedal currently maps to. The fill is
/// faint on purpose — it is behind the curve and the control points, and must
/// not compete with them for attention while a point is being dragged.
constexpr int kLiveFillAlpha = 48;
constexpr int kLiveCrossAlpha = 170;
constexpr qreal kLiveCrossWidth = 1.0;

/// How finely the spline between two points is sampled. More than the pixels a
/// segment can ever span, so the polyline is indistinguishable from the curve.
constexpr int kSplineSamplesPerSpan = 24;

QRectF plotRect(const QRect &widgetRect)
{
    return QRectF(kAxisLabelWidth, kPlotMargin,
                  widgetRect.width() - kAxisLabelWidth - kPlotMargin,
                  widgetRect.height() - kPlotMargin - kAxisLabelHeight);
}

qreal toX(const QRectF &plot, double input)
{
    return plot.left() + (input - kInputMin) / (kInputMax - kInputMin) * plot.width();
}

qreal toY(const QRectF &plot, double output)
{
    return plot.bottom() - (output - kOutputMin) / (kOutputMax - kOutputMin) * plot.height();
}

/// Inverse of toY(), clamped so a point dragged past the edge sticks to the edge.
double fromY(const QRectF &plot, qreal y)
{
    const double value = kOutputMin + (plot.bottom() - y) / plot.height()
                         * (kOutputMax - kOutputMin);
    return qBound(kOutputMin, value, kOutputMax);
}

double pointInputAt(int index)
{
    return kInputMin
           + (kInputMax - kInputMin) * index / (CurveEditor::kPointCount - 1);
}

/// The Catmull-Rom spline through `points`, sampled into a polyline.
///
/// Catmull-Rom rather than Bezier because it passes exactly through every
/// control point without tangents to set up, which is exactly what a
/// point-through-curve editor needs. End segments are clamped to their
/// neighbours, so the ends do not overshoot.
QPolygonF splineThrough(const QVector<QPointF> &points, int samplesPerSpan)
{
    QPolygonF out;
    out.reserve(samplesPerSpan * (points.size() - 1) + 1);
    for (int i = 0; i + 1 < points.size(); ++i) {
        const QPointF p0 = points.value(i - 1, points[i]);
        const QPointF p1 = points[i];
        const QPointF p2 = points[i + 1];
        const QPointF p3 = points.value(i + 2, points[i + 1]);
        for (int s = 0; s < samplesPerSpan; ++s) {
            const double t = s / double(samplesPerSpan);
            const double t2 = t * t;
            const double t3 = t2 * t;
            out << QPointF(
                0.5 * ((2.0 * p1.x()) + (-p0.x() + p2.x()) * t
                       + (2.0 * p0.x() - 5.0 * p1.x() + 4.0 * p2.x() - p3.x()) * t2
                       + (-p0.x() + 3.0 * p1.x() - 3.0 * p2.x() + p3.x()) * t3),
                0.5 * ((2.0 * p1.y()) + (-p0.y() + p2.y()) * t
                       + (2.0 * p0.y() - 5.0 * p1.y() + 4.0 * p2.y() - p3.y()) * t2
                       + (-p0.y() + 3.0 * p1.y() - 3.0 * p2.y() + p3.y()) * t3));
        }
    }
    out << points.last();
    return out;
}

/// Where `polyline` is at `x`, by interpolating between the two samples that
/// bracket it.
///
/// The curve is a function of the input, so there is exactly one such height.
/// Interpolating the drawn polyline rather than re-evaluating the spline is
/// deliberate: it guarantees the cross lands *on* the line the user can see,
/// even if the sampling were ever coarsened.
qreal polylineYAt(const QPolygonF &polyline, qreal x)
{
    if (polyline.isEmpty())
        return 0.0;
    if (x <= polyline.first().x())
        return polyline.first().y();
    for (int i = 0; i + 1 < polyline.size(); ++i) {
        const QPointF a = polyline[i];
        const QPointF b = polyline[i + 1];
        if (x > b.x())
            continue;
        const qreal span = b.x() - a.x();
        if (span <= 0.0)
            return b.y();
        return a.y() + (b.y() - a.y()) * (x - a.x()) / span;
    }
    return polyline.last().y();
}

/// The curve from the start of the plot up to `x`, closed down the bottom edge:
/// the travel the pedal has already covered.
QPolygonF areaUnder(const QPolygonF &polyline, const QRectF &plot, qreal x)
{
    QPolygonF area;
    for (const QPointF &point : polyline) {
        if (point.x() > x)
            break;
        area << point;
    }
    // The cut is almost never on a sample, so the last point is the exact
    // height at x — otherwise the fill's edge would jitter between samples as
    // the pedal moves.
    area << QPointF(x, polylineYAt(polyline, x));
    area << QPointF(x, plot.bottom());
    area << QPointF(plot.left(), plot.bottom());
    return area;
}

} // namespace

// --- The graph --------------------------------------------------------------

class CurveEditor::Graph : public QWidget
{
public:
    Graph(CurveEditor *editor, QWidget *parent)
        : QWidget(parent)
        , m_editor(editor)
    {
        setMouseTracking(true);
        
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF plot = plotRect(rect());

        // The grid: dashed every twenty-five units, so each grid cell is 25 x 25
        // input/output units. The zero line is drawn solid and darker - it is
        // the reference the eye must find fastest, the way the other graphs in
        // the application treat their centre lines.
        QPen gridPen(palette().color(QPalette::Mid));
        gridPen.setStyle(Qt::DashLine);
        gridPen.setWidthF(0.8);
        p.setPen(gridPen);
        for (int value = -100; value <= 100; value += 25) {
            const double y = toY(plot, value);
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int value = 0; value <= 100; value += 25) {
            const double x = toX(plot, value);
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        QPen zeroPen(palette().color(QPalette::Dark));
        zeroPen.setWidthF(1.0);
        p.setPen(zeroPen);
        p.drawLine(QPointF(plot.left(), toY(plot, 0.0)),
                   QPointF(plot.right(), toY(plot, 0.0)));
        p.drawRect(plot);

        // Axis labels: reference text, one point smaller than the UI font.
        QFont font = p.font();
        font.setPointSizeF(font.pointSizeF() - 1.0);
        p.setFont(font);
        p.setPen(palette().color(QPalette::Text));
        for (int value = -100; value <= 100; value += 50) {
            p.drawText(QRectF(0, toY(plot, value) - 7, kAxisLabelWidth - 4, 14),
                       Qt::AlignRight | Qt::AlignVCenter, QString::number(value));
        }
        for (int value = 0; value <= 100; value += 50) {
            p.drawText(
                QRectF(toX(plot, value) - 20, plot.bottom() + 2, 40, kAxisLabelHeight - 2),
                Qt::AlignHCenter, QString::number(value));
        }

        // The response curve, through every control point.
        QVector<QPointF> points;
        points.reserve(kPointCount);
        for (int i = 0; i < kPointCount; ++i)
            points << QPointF(toX(plot, pointInputAt(i)), toY(plot, m_editor->valueAt(i)));

        const QPolygonF curve = splineThrough(points, kSplineSamplesPerSpan);

        // Where the pedal is, drawn under the curve so the curve stays the
        // clearest thing on the graph: the travel covered so far as a filled
        // area, and a cross through the output that travel currently produces.
        if (m_editor->input())
            paintLiveInput(p, plot, curve, *m_editor->input());

        p.setPen(QPen(palette().color(QPalette::Accent), 2.0));
        p.drawPolyline(curve);

        // The control points themselves. The hovered one is filled so the eye
        // can tell at a glance which handle the cursor has on it.
        for (int i = 0; i < kPointCount; ++i) {
            const QPointF point = points[i];
            if (i == m_hoverIndex)
                p.setBrush(palette().color(QPalette::Accent));
            else
                p.setBrush(palette().color(QPalette::Base));
            p.setPen(QPen(palette().color(QPalette::Accent), 1.5));
            p.drawEllipse(point, kMarkerRadius, kMarkerRadius);
        }
    }

    /// The filled travel and the cross, plus the numbers they stand for.
    void paintLiveInput(QPainter &p, const QRectF &plot, const QPolygonF &curve,
                        double input) const
    {
        const qreal x = toX(plot, qBound(kInputMin, input, kInputMax));
        const qreal y = polylineYAt(curve, x);

        const QColor accent = palette().color(QPalette::Accent);

        QColor fill = accent;
        fill.setAlpha(kLiveFillAlpha);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPolygon(areaUnder(curve, plot, x));

        // Both lines span the whole plot, so each one can be read off against
        // the axis it belongs to without following the curve by eye.
        QColor crossColour = accent;
        crossColour.setAlpha(kLiveCrossAlpha);
        QPen crossPen(crossColour);
        crossPen.setWidthF(kLiveCrossWidth);
        p.setPen(crossPen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));

        // The numbers, in the corner the curve is least likely to occupy: the
        // cross says roughly where, this says exactly where.
        p.setPen(accent);
        p.drawText(plot.adjusted(4, 2, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                   tr("%1 %  \u2192  %2")
                       .arg(input, 0, 'f', 1)
                       .arg(fromY(plot, y), 0, 'f', 1));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const QRectF plot = plotRect(rect());
        m_dragIndex = hitPoint(plot, event->position());
        if (m_dragIndex >= 0) {
            // Vertical only: the input position is fixed, so only the height of
            // the click under the cursor takes effect.
            m_editor->movePoint(m_dragIndex, fromY(plot, event->position().y()));
            setCursor(Qt::SizeVerCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QRectF plot = plotRect(rect());
        if (m_dragIndex >= 0) {
            m_editor->movePoint(m_dragIndex, fromY(plot, event->position().y()));
        } else {
            // The hovered point is what earns the filled marker, so it is
            // recomputed on every move and redrawn only when it actually
            // changes — a repaint per pixel would be wasted work for the
            // stretches of movement that hit nothing.
            const int hover = hitPoint(plot, event->position());
            if (hover != m_hoverIndex) {
                m_hoverIndex = hover;
                update();
            }
            // Offer the vertical-resize cursor when a point is close enough to
            // grab, so the mouse advertises what a press would do.
            setCursor(hover >= 0 ? Qt::SizeVerCursor : Qt::ArrowCursor);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dragIndex >= 0) {
            m_dragIndex = -1;
            setCursor(Qt::ArrowCursor);
        }
    }

    void leaveEvent(QEvent *event) override
    {
        Q_UNUSED(event);
        // The cursor left the widget, so no handle is hovered and its highlight
        // must not survive to the next visit.
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
    }

private:
    /// The index of the nearest control point, or -1 when none is within the
    /// grab radius.
    int hitPoint(const QRectF &plot, const QPointF &position) const
    {
        int best = -1;
        double bestDistance = kGrabRadius;
        for (int i = 0; i < kPointCount; ++i) {
            const QPointF point(toX(plot, pointInputAt(i)), toY(plot, m_editor->valueAt(i)));
            const double distance = std::hypot(point.x() - position.x(),
                                               point.y() - position.y());
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    CurveEditor *m_editor;
    int m_dragIndex = -1;
    int m_hoverIndex = -1;  ///< The point under the cursor, or -1.
};

// --- The editor -------------------------------------------------------------

CurveEditor::CurveEditor(QWidget *parent)
    : QWidget(parent)
{
    // Linear until told otherwise: the most neutral possible starting point.
    m_values.resize(kPointCount);
    for (int i = 0; i < kPointCount; ++i)
        m_values[i] = kOutputMin + (kOutputMax - kOutputMin) * i / (kPointCount - 1);

    // No layout: the graph must be square whatever room it is given, which a box
    // layout cannot express. resizeEvent() places both children by hand.
    m_presets = new QWidget(this);
    auto *presets = new QVBoxLayout(m_presets);
    presets->setContentsMargins(0, 0, 0, 0);
    presets->setSpacing(4);
    auto *header = new QLabel(tr("Presets"), m_presets);
    presets->addWidget(header);

    auto addButton = [this, presets](const QString &text, Preset preset) {
        auto *button = new QPushButton(text, m_presets);
        presets->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, preset] {
            applyPreset(preset);
        });
    };
    addButton(tr("Linear"), Preset::Linear);
    addButton(tr("Exponential"), Preset::Exponential);
    addButton(tr("S-Curve"), Preset::S_Curve);
    addButton(tr("Progressive"), Preset::Progressive);
    addButton(tr("Regressive"), Preset::Regressive);
    presets->addStretch(1);

    m_graph = new Graph(this, this);
    // The values live in this widget, the pixels in the graph: nothing else
    // repaints it, so every change must be echoed back as a redraw or the
    // dragged handle would sit still while the curve silently moved underneath.
    connect(this, &CurveEditor::changed, m_graph, [this] { m_graph->update(); });

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

int CurveEditor::heightForGraphWidth(int graphWidth) const
{
    // The plot is a square, so once the graph's width is known so is its height:
    // the width less the axis-label margin gives the plot's side, and the same
    // side plus the margins above and below gives the height.
    const int plotSide = qMax(kMinimumPlot, graphWidth - kFrameWidth);
    return qMax(plotSide + kFrameHeight, m_presets->sizeHint().height());
}

bool CurveEditor::hasHeightForWidth() const
{
    return true;
}

int CurveEditor::heightForWidth(int width) const
{
    return heightForGraphWidth(width - m_presets->sizeHint().width() - kSpacing);
}

QSize CurveEditor::minimumSizeHint() const
{
    const int width = m_presets->sizeHint().width() + kSpacing + kFrameWidth + kMinimumPlot;
    return {width, heightForWidth(width)};
}

QSize CurveEditor::sizeHint() const
{
    const int width = minimumSizeHint().width() + 60;
    return {width, heightForWidth(width)};
}

void CurveEditor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int presetsWidth = m_presets->sizeHint().width();

    // The graph takes all the width left of the presets and its height follows;
    // the layout gives us that same height through heightForWidth(), which is
    // what pushes whatever is below the graph down.
    const int graphWidth = qMax(kFrameWidth + kMinimumPlot, width() - presetsWidth - kSpacing);
    const int graphHeight = heightForGraphWidth(graphWidth);
    m_presets->setGeometry(0, 0, presetsWidth, graphHeight);
    m_graph->setGeometry(presetsWidth + kSpacing, 0, graphWidth, graphHeight);
}

void CurveEditor::applyPreset(Preset preset)
{
    // Each preset as a function of normalised input t in 0..1, returning
    // normalised output in -1..1.
    auto linear = [](double t) { return 2.0 * t - 1.0; };
    auto exponential = [](double t) { return 2.0 * t * t - 1.0; };
    auto sCurve = [](double t) { return 2.0 * t * t * (3.0 - 2.0 * t) - 1.0; };
    auto progressive = [](double t) { return 2.0 * std::sqrt(t) - 1.0; };
    auto regressive = [](double t) { return 2.0 * t * t * t - 1.0; };

    double (*fn)(double) = linear;
    switch (preset) {
    case Preset::Linear: fn = linear; break;
    case Preset::Exponential: fn = exponential; break;
    case Preset::S_Curve: fn = sCurve; break;
    case Preset::Progressive: fn = progressive; break;
    case Preset::Regressive: fn = regressive; break;
    }

    for (int i = 0; i < kPointCount; ++i)
        m_values[i] = kOutputMax * fn(i / double(kPointCount - 1));
    Q_EMIT changed();
}

void CurveEditor::setValues(const QVector<double> &values)
{
    if (values.size() != kPointCount)
        return;
    bool moved = false;
    for (int i = 0; i < kPointCount; ++i) {
        const double clamped = qBound(kOutputMin, values.at(i), kOutputMax);
        moved = moved || m_values[i] != clamped;
        m_values[i] = clamped;
    }
    if (moved)
        m_graph->update();
}

void CurveEditor::setInput(const std::optional<double> &input)
{
    if (m_input == input)
        return;
    m_input = input;
    // Not a change to the curve, so no changed() here: nothing has been edited
    // and nothing would be written to the device. Only the pixels move.
    m_graph->update();
}

void CurveEditor::movePoint(int index, double value)
{
    if (index < 0 || index >= kPointCount)
        return;
    const double clamped = qBound(kOutputMin, value, kOutputMax);
    if (m_values[index] == clamped)
        return;
    m_values[index] = clamped;
    Q_EMIT changed();
}

} // namespace hidcenter::simsonn_pedals
