// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LedRing.h"

#include <QColorDialog>
#include <QConicalGradient>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Breathing room between the widget's edge and the rim, so the hover marker
/// outside the band has somewhere to sit.
constexpr qreal kMargin = 8.0;

/// Band thickness as a fraction of the outer radius. Thick enough to read a
/// colour off, thin enough to still look like a strip rather than a disc with a
/// dot punched out.
constexpr qreal kBandRatio = 0.30;

/// The hover marker: a thin arc just outside the rim rather than a tint over the
/// band. Tinting would alter the very colour the user is trying to judge.
constexpr qreal kHoverGap = 3.0;
constexpr qreal kHoverWidth = 2.5;

/// Boundaries between zones, drawn across the band. Not decoration: a smooth
/// fade gives no clue where one click target ends and the next begins, so
/// without these the zones are invisible.
constexpr qreal kTickWidth = 2.0;
constexpr qreal kTickAlpha = 0.75;

/// Shown when a ring is asked to hold nothing at all. RpmBar's green, so a bare
/// LedRing reads against both a light and a dark palette.
const QColor kFallbackColour(0x37, 0xb2, 0x4d);

/// How many stops a ring starts on. Three is enough to show that the zones
/// divide up and that the fade wraps, which one or two would not.
constexpr int kInitialRingColours = 3;

/// The stops a fresh ring starts with: an even sweep of hue, so it reads as a
/// fade the moment it appears rather than as a row of identical wedges the user
/// has to click before the widget explains itself. These also fill the tail
/// beyond the current count, so raising it never reveals a black gap.
QList<QColor> defaultRingColours()
{
    QList<QColor> colours;
    colours.reserve(kMaxRingColours);
    for (int i = 0; i < kMaxRingColours; ++i)
        colours.append(QColor::fromHsvF(qreal(i) / kMaxRingColours, 0.75, 0.95));
    return colours;
}

/// A point on the ring at an angle measured clockwise from the top, which is how
/// this widget counts everywhere. Screen y grows downward, which is why cosine
/// is subtracted rather than added.
QPointF pointAt(const QPointF &centre, qreal radius, qreal degrees)
{
    const qreal radians = qDegreesToRadians(degrees);
    return {centre.x() + radius * std::sin(radians), centre.y() - radius * std::cos(radians)};
}

/// The swatch marking one stop's exact colour: a dot at the middle of the band.
///
/// Sized from the band rather than fixed, so it stays in proportion as the widget
/// is resized, and bounded at both ends — large enough to read a colour off on a
/// small ring, small enough that sixteen of them do not run together on one.
constexpr qreal kSwatchBandRatio = 0.15;
constexpr qreal kSwatchMinRadius = 2.5;
constexpr qreal kSwatchMaxRadius = 7.0;
constexpr qreal kSwatchOutlineWidth = 1.0;
constexpr qreal kSwatchOutlineAlpha = 0.55;

/// sRGB's transfer function and its inverse.
///
/// The piecewise standard, not a bare 2.2 power: the linear toe near black is
/// exactly the region this widget cares about, since the whole point is what a
/// dark stop looks like once its neighbours bleed into it.
qreal toLinear(qreal encoded)
{
    return std::pow(encoded, kGamma);
}

qreal toEncoded(qreal linear)
{
    const qreal clamped = qBound(0.0, linear, 1.0);
    return std::pow(clamped, 1.0 / kGamma);
}

/// One colour in linear light, which is the space the blur has to happen in.
struct LinearRgb
{
    qreal r = 0.0;
    qreal g = 0.0;
    qreal b = 0.0;
};

LinearRgb toLinear(const QColor &colour)
{
    return {toLinear(colour.redF()), toLinear(colour.greenF()), toLinear(colour.blueF())};
}

QColor toColour(const LinearRgb &linear)
{
    return QColor::fromRgbF(toEncoded(linear.r), toEncoded(linear.g), toEncoded(linear.b));
}

/// The colour a stop list holds at `position`, in linear light.
///
/// Stops are ascending and the list ringGradientStops() produces spans 0.0 to 1.0
/// inclusive, so a position inside that range always falls between two of them
/// and the ends need no special case.
LinearRgb sampleStops(const QGradientStops &stops, qreal position)
{
    if (stops.isEmpty())
        return {};

    if (position <= stops.first().first)
        return toLinear(stops.first().second);

    for (int i = 1; i < stops.size(); ++i) {
        const auto &lower = stops.at(i - 1);
        const auto &upper = stops.at(i);
        if (position > upper.first)
            continue;

        const qreal span = upper.first - lower.first;
        // Two stops at the same position: the later one wins, as it does in Qt.
        if (span <= 0.0)
            return toLinear(upper.second);

        const qreal t = (position - lower.first) / span;
        const LinearRgb a = toLinear(lower.second);
        const LinearRgb b = toLinear(upper.second);
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }

    return toLinear(stops.last().second);
}

/// The blur kernel: a wrapped Gaussian over kDiffuserSamples positions,
/// normalised so it spreads a colour without shifting it.
///
/// Wrapped rather than truncated because the ring has no ends. At a wide sigma
/// the tails meet round the back, and dropping them would quietly darken
/// everything by whatever weight was thrown away.
///
/// Built fresh on every call rather than cached against the sigma. It is one
/// exponential per sample against the samples-squared of the convolution it
/// feeds, so caching would buy a fraction of a percent in exchange for a static
/// that has to be kept in step with its own argument.
QList<qreal> diffuserKernel(qreal sigmaDeg)
{
    QList<qreal> weights(kDiffuserSamples, 0.0);
    const qreal degreesPerSample = 360.0 / kDiffuserSamples;

    qreal total = 0.0;
    for (int i = 0; i < kDiffuserSamples; ++i) {
        // The shorter way round: sample kDiffuserSamples - 1 is one step behind
        // sample 0, not a whole ring ahead of it.
        const qreal degrees = qMin(i, kDiffuserSamples - i) * degreesPerSample;
        const qreal weight = std::exp(-0.5 * (degrees / sigmaDeg) * (degrees / sigmaDeg));
        weights[i] = std::pow(weight, kGammaDiffuserWeights);
        total += weight;
    }

    for (qreal &weight : weights)
        weight /= total;

    return weights;
}

} // namespace

QGradientStops ringGradientStops(const QList<QColor> &colours)
{
    if (colours.isEmpty())
        return {{0.0, kFallbackColour}, {1.0, kFallbackColour}};

    const int count = colours.size();
    QGradientStops stops;
    stops.reserve(count + 1);

    // Qt's conical gradient counts positions counter-clockwise, and this ring
    // counts angles clockwise, so colour i — which sits i/N of the way round
    // clockwise — lands at position 1 - i/N. Walking i downward from N-1 is what
    // makes that come out ascending, which is what QGradient requires.
    stops.append({0.0, colours.at(0)});
    for (int i = count - 1; i >= 1; --i)
        stops.append({1.0 - qreal(i) / count, colours.at(i)});
    stops.append({1.0, colours.at(0)});

    return stops;
}

QGradientStops diffuseGradientStops(const QGradientStops &stops, qreal sigmaDeg1, qreal sigmaDeg2)
{
    // A blur of nothing, or of no width, is what it started as.
    if (stops.isEmpty() || sigmaDeg1 <= 0.0)
        return stops;

    // Sampled in linear light, so the interpolation between stops is photometric
    // too. Qt would have interpolated in sRGB; over a fade this wide the
    // difference is invisible, and the alternative is converting spaces twice.
    QList<LinearRgb> samples;
    samples.reserve(kDiffuserSamples);
    for (int i = 0; i < kDiffuserSamples; ++i)
        samples.append(sampleStops(stops, qreal(i) / kDiffuserSamples));

    const QList<qreal> kernel1 = diffuserKernel(sigmaDeg1);
    const QList<qreal> kernel2 = diffuserKernel(sigmaDeg2);
    const qreal kernel1weight = 1.0 - kDiffuserSigmaDeg2Weight;
    const qreal kernel2weight = kDiffuserSigmaDeg2Weight;

    QGradientStops blurred;
    blurred.reserve(kDiffuserSamples + 1);
    for (int i = 0; i < kDiffuserSamples; ++i) {
        LinearRgb sum;
        for (int k = 0; k < kDiffuserSamples; ++k) {
            // Modulo, not a clamp: light arriving from behind the seam is light
            // arriving from the other side of the ring.
            const LinearRgb &sample = samples.at((i - k + kDiffuserSamples) % kDiffuserSamples);
            const qreal weight = kernel1.at(k) * kernel1weight + kernel2.at(k) * kernel2weight;
            sum.r += sample.r * weight;
            sum.g += sample.g * weight;
            sum.b += sample.b * weight;
        }
        blurred.append({qreal(i) / kDiffuserSamples, toColour(sum)});
    }

    // Closed the way ringGradientStops() closes its own: the last stop repeats
    // the first, so the fade wraps rather than snapping at the seam.
    blurred.append({1.0, blurred.first().second});

    return blurred;
}

LedRing::LedRing(QWidget *parent)
    : QWidget(parent)
    , m_all(defaultRingColours())
    , m_count(kInitialRingColours)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    // Without this the hover highlight and the cursor would only follow the
    // pointer while a button was held, which is exactly when they matter least.
    setMouseTracking(true);

    setToolTip(tr("Click a section of the ring to set the colour it fades through."));

    m_countLabel = new QLabel(tr("Colours"), this);
    m_countBox = new QSpinBox(this);
    m_countBox->setObjectName(QStringLiteral("ringColourCount"));
    m_countBox->setRange(1, kMaxRingColours);
    m_countBox->setValue(m_count);
    m_countBox->setToolTip(tr("How many colours the ring fades between, each with an equal share."));

    // Stacked in the middle of the widget, which is the middle of the ring —
    // ringRect() is a centred square, so the layout needs no geometry of its
    // own. AlignCenter on each is what stops the spin box stretching to the
    // full width.
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(m_countLabel, 0, Qt::AlignCenter);
    layout->addWidget(m_countBox, 0, Qt::AlignCenter);
    layout->addStretch(1);

    connect(m_countBox, &QSpinBox::valueChanged, this, &LedRing::setColourCount);
}

QSize LedRing::sizeHint() const
{
    return {220, 220};
}

QSize LedRing::minimumSizeHint() const
{
    // Below about the first the band is too narrow to judge a colour against.
    // The second is what stops the control in the middle ever being clipped by
    // the widget's own edge: past that point the band tightens around it, and
    // then simply passes behind it.
    return QSize(140, 140).expandedTo(layout()->minimumSize());
}

void LedRing::setColours(const QList<QColor> &colours)
{
    QList<QColor> clean;
    clean.reserve(qMin(colours.size(), kMaxRingColours));
    for (const QColor &colour : colours) {
        if (clean.size() == kMaxRingColours)
            break;
        clean.append(colour.isValid() ? colour : kFallbackColour);
    }

    // A ring with no stops has nothing to draw and no zone to click. Taking that
    // as one stop keeps every other method free of an empty case.
    if (clean.isEmpty())
        clean.append(kFallbackColour);

    if (clean == this->colours())
        return;

    // Written into the full-length array rather than replacing it, so whatever
    // sits past the new count is still there if the count goes back up.
    for (int i = 0; i < clean.size(); ++i)
        m_all[i] = clean.at(i);
    m_count = clean.size();

    syncCountBox();

    // The hovered zone was an index into the old array and may no longer exist.
    m_hovered = -1;
    unsetCursor();

    update();
    Q_EMIT coloursChanged();
}

void LedRing::setColourCount(int count)
{
    const int wanted = qBound(1, count, kMaxRingColours);
    if (wanted == m_count)
        return;

    m_count = wanted;
    syncCountBox();

    m_hovered = -1;
    unsetCursor();

    update();
    Q_EMIT coloursChanged();
}

void LedRing::syncCountBox()
{
    // What actually stops a loop here is setColourCount()'s own guard, which
    // returns on a count it already holds. The blocker is defence in depth: it
    // keeps the box from announcing a change nobody asked it for, so the guard
    // is never the only thing standing between the two.
    const QSignalBlocker blocker(m_countBox);
    m_countBox->setValue(m_count);
}

bool LedRing::isCountVisible() const
{
    return !m_countBox->isHidden();
}

void LedRing::setCountVisible(bool visible)
{
    m_countLabel->setVisible(visible);
    m_countBox->setVisible(visible);
}

void LedRing::setPreviewColours(const QList<QColor> &colours)
{
    if (m_preview == colours)
        return;

    m_preview = colours;

    // update(), not repaint(): this is called from a timer at animation rate,
    // and coalescing two frames that arrive inside one paint is exactly right.
    update();
}

QRectF LedRing::ringRect() const
{
    const qreal side = qMin(width(), height()) - 2 * kMargin;
    if (side <= 0)
        return {};

    // Centred, so the ring stays a circle whatever aspect the layout hands it.
    return {(width() - side) / 2.0, (height() - side) / 2.0, side, side};
}

qreal LedRing::outerRadius() const
{
    return ringRect().width() / 2.0;
}

qreal LedRing::innerRadius() const
{
    return outerRadius() * (1.0 - kBandRatio);
}

int LedRing::zoneAt(const QPointF &point) const
{
    const QRectF box = ringRect();
    if (box.isEmpty())
        return -1;

    const QPointF centre = box.center();
    const qreal dx = point.x() - centre.x();
    const qreal dy = point.y() - centre.y();
    const qreal radius = std::hypot(dx, dy);
    if (radius > outerRadius() || radius < innerRadius())
        return -1;

    // atan2 counts counter-clockwise from three o'clock; passing (dx, -dy)
    // instead of the usual (dy, dx) turns it into degrees clockwise from twelve,
    // which is how this widget counts.
    qreal degrees = qRadiansToDegrees(std::atan2(dx, -dy)) - kRingStartAngleDeg;
    degrees = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);

    // Each zone is centred on its own colour, so the zone is simply the nearest
    // anchor — a rounding, not a search. Rounding rather than truncating is the
    // whole of it: truncating would put the boundaries at the colours.
    const qreal span = 360.0 / m_count;
    return int(qRound(degrees / span)) % m_count;
}

void LedRing::mousePressEvent(QMouseEvent *event)
{
    const int zone = zoneAt(event->position());
    if (zone < 0) {
        // The hole and the corners are not ours; let them behave like background.
        QWidget::mousePressEvent(event);
        return;
    }

    const QColor picked = QColorDialog::getColor(m_all.at(zone), this,
                                                 tr("LED colour %1").arg(zone + 1));
    // An invalid colour is the dialog saying it was cancelled.
    if (picked.isValid() && picked != m_all.at(zone)) {
        QList<QColor> updated = colours();
        updated[zone] = picked;
        setColours(updated);
    }

    event->accept();
}

void LedRing::mouseMoveEvent(QMouseEvent *event)
{
    const int zone = zoneAt(event->position());
    if (zone != m_hovered) {
        m_hovered = zone;

        // The hand means "there is something here to click", so it must not
        // show over the hole in the middle, where there is not. Unset rather
        // than forced to an arrow, so the ring falls back to whatever its parent
        // uses instead of overriding it.
        if (zone >= 0)
            setCursor(Qt::PointingHandCursor);
        else
            unsetCursor();

        // Guarded, so a mouse crossing the widget costs one repaint per zone
        // rather than one per pixel.
        update();
    }

    QWidget::mouseMoveEvent(event);
}

void LedRing::leaveEvent(QEvent *event)
{
    if (m_hovered != -1) {
        m_hovered = -1;
        unsetCursor();
        update();
    }

    QWidget::leaveEvent(event);
}

void LedRing::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const QRectF box = ringRect();
    if (box.isEmpty())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF centre = box.center();
    const qreal outer = outerRadius();
    const qreal inner = innerRadius();
    const QList<QColor> shown = colours();
    const int count = shown.size();

    // The band's own colours, which are the picks unless something is animating
    // them. Everything below this line — the zones, the swatches, the hit
    // targets — stays on `shown`, because those describe what is set rather than
    // what is lit at this instant.
    const QList<QColor> &bandColours = m_preview.size() == count ? m_preview : shown;

    // --- The band ----------------------------------------------------------
    // Two ellipses in one path: the odd-even fill leaves the ring between them.
    // A path rather than a wide pen, so the ticks below can be drawn straight
    // across it without a second shape to keep in step.
    QPainterPath band;
    band.addEllipse(centre, outer, outer);
    band.addEllipse(centre, inner, inner);

    if (count == 1) {
        // A conical gradient with a single colour is a degenerate way to say
        // "fill", and Qt is entitled to render it as one. Blurring it would also
        // be a no-op: one colour spread round a ring is that colour.
        painter.fillPath(band, bandColours.at(0));
    } else {
        QConicalGradient fade(centre, 90.0 - kRingStartAngleDeg);
        fade.setStops(diffuseGradientStops(ringGradientStops(bandColours), kDiffuserSigmaDeg1,
                                           kDiffuserSigmaDeg2));
        painter.fillPath(band, fade);
    }

    const qreal span = 360.0 / count;

    // --- Zone boundaries ---------------------------------------------------
    if (count > 1) {
        QColor tick = palette().color(QPalette::Shadow);
        tick.setAlphaF(kTickAlpha);
        painter.setPen(QPen(tick, kTickWidth));

        for (int i = 0; i < count; ++i) {
            // Halfway between this colour's anchor and the next one's.
            const qreal degrees = kRingStartAngleDeg + (i + 0.5) * span;
            painter.drawLine(pointAt(centre, inner, degrees), pointAt(centre, outer, degrees));
        }
    }

    // --- Stop swatches -----------------------------------------------------
    // The band is blurred to match the wheel, so the colour at a stop's anchor is
    // not the colour that was picked there. These say what it actually is.
    //
    // Not drawn at one colour: the band is already a flat fill of exactly that
    // colour, so a dot on it would say nothing and hide a little of the thing it
    // was meant to describe.
    if (count > 1) {
        const qreal midRadius = (inner + outer) / 2.0;
        const qreal swatchRadius =
            qBound(kSwatchMinRadius, (outer - inner) * kSwatchBandRatio, kSwatchMaxRadius);

        for (int i = 0; i < count; ++i) {
            const QColor &colour = shown.at(i);
            const QPointF at = pointAt(centre, midRadius, kRingStartAngleDeg + i * span);

            // Outlined against itself rather than against the palette: a dark
            // swatch on a dark stretch of band, or a light one on a light
            // stretch, would otherwise have no edge at all.
            QColor outline = colour.lightnessF() > 0.5 ? Qt::black : Qt::white;
            outline.setAlphaF(kSwatchOutlineAlpha);

            painter.setPen(QPen(outline, kSwatchOutlineWidth));
            painter.setBrush(colour);
            painter.drawEllipse(at, swatchRadius, swatchRadius);
        }
    }

    // --- Rims --------------------------------------------------------------
    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(centre, outer - 0.5, outer - 0.5);
    painter.drawEllipse(centre, inner + 0.5, inner + 0.5);

    // --- Hover -------------------------------------------------------------
    if (m_hovered >= 0 && m_hovered < count) {
        const qreal markerRadius = outer + kHoverGap;
        const QRectF markerBox(centre.x() - markerRadius, centre.y() - markerRadius,
                               markerRadius * 2, markerRadius * 2);

        // drawArc measures in sixteenths of a degree, counter-clockwise from
        // three o'clock; this ring measures clockwise from twelve. Hence the
        // 90 - x, and the negative sweep.
        const qreal startDegrees = kRingStartAngleDeg + m_hovered * span - span / 2.0;

        painter.setPen(QPen(palette().color(QPalette::Highlight), kHoverWidth));
        painter.drawArc(markerBox, int(qRound((90.0 - startDegrees) * 16)),
                        int(qRound(-span * 16)));
    }
}

} // namespace hidcenter::simagic_alpha_evo
