// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LedModes.h"

#include <QCoreApplication>
#include <QtMath>

#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Scale one channel of a colour.
///
/// **The 8-bit value is scaled directly, not the light behind it.** sRGB is
/// roughly a perceptual encoding, so halving the code value is roughly halving
/// the apparent brightness; scaling luminance instead would make a breath crawl
/// through the bottom of its range and rush through the top.
///
/// This looks like the wrong thing to anyone who has just read LedRing's
/// diffuser, which works in linear light on purpose. That is a different job:
/// adding light from neighbouring LEDs, where linear is the only way it adds up.
/// Dimming one LED is not addition, and the eye is what is being matched.
quint8 scaleChannel(quint8 value, qreal factor)
{
    return quint8(qBound(0, qRound(value * factor), 255));
}

LedRingColours scaled(const LedRingColours &colours, qreal factor)
{
    LedRingColours out = colours;
    const int count = qMin(int(out.count), kMaxLedStops);
    for (int i = 0; i < count; ++i) {
        LedRgb &stop = out.stops[std::size_t(i)];
        stop.r = scaleChannel(stop.r, factor);
        stop.g = scaleChannel(stop.g, factor);
        stop.b = scaleChannel(stop.b, factor);
    }
    return out;
}

/// Where in the cycle `elapsedMs` falls, as 0..1.
///
/// The period is clamped before the division rather than trusted: a zero would
/// divide by nothing, and this is reached from a settings file a user may have
/// edited by hand.
qreal phaseOf(quint16 periodMs, qint64 elapsedMs)
{
    const qint64 period = qBound(qint64(kLedMinPeriodMs), qint64(periodMs),
                                 qint64(kLedMaxPeriodMs));
    if (elapsedMs <= 0)
        return 0.0;
    return qreal(elapsedMs % period) / qreal(period);
}

} // namespace

// ---------------------------------------------------------------------------
// The list of modes
// ---------------------------------------------------------------------------

const std::array<LedModeSpec, kLedModeCount> &ledModeSpecs()
{
    // Listed as the accordion lists them, the default first. The last column is
    // what decides which one wins; see the declaration.
    static const std::array<LedModeSpec, kLedModeCount> specs{{
        {LedMode::Idle, "idle", QT_TRANSLATE_NOOP("LedModes", "Idle"),
         QT_TRANSLATE_NOOP("LedModes", "What the wheel shows the rest of the time."), 100},
        {LedMode::TractionControl, "tractionControl",
         QT_TRANSLATE_NOOP("LedModes", "Traction Control"),
         QT_TRANSLATE_NOOP("LedModes", "While traction control is cutting in."), 20},
        {LedMode::Abs, "abs", QT_TRANSLATE_NOOP("LedModes", "ABS"),
         QT_TRANSLATE_NOOP("LedModes", "While the anti-lock brakes are working."), 10},
    }};
    return specs;
}

const LedModeSpec &ledModeSpec(LedMode mode)
{
    const auto &specs = ledModeSpecs();
    for (const LedModeSpec &spec : specs) {
        if (spec.mode == mode)
            return spec;
    }

    // Unreachable while every enumerator has a row, which is the point: a mode
    // added to the enum and not to the table lands here rather than silently
    // being drawn as something else.
    Q_ASSERT_X(false, "ledModeSpec", "a LedMode with no row in ledModeSpecs()");
    return specs.front();
}

QString ledModeTitle(LedMode mode)
{
    return QCoreApplication::translate("LedModes", ledModeSpec(mode).title);
}

QString ledModeBlurb(LedMode mode)
{
    return QCoreApplication::translate("LedModes", ledModeSpec(mode).blurb);
}

bool ledModeApplies(LedMode mode, const TelemetryFrame &frame)
{
    switch (mode) {
    case LedMode::Abs:
        return frame.abs;
    case LedMode::TractionControl:
        return frame.tractionControl;
    case LedMode::Idle:
        // Always. What makes it the fallback is its priority, not a test that
        // every other mode has to remember to come before.
        return true;
    }
    return false;
}

LedMode ledModeForFrame(const TelemetryFrame &frame)
{
    const LedModeSpec *winner = nullptr;
    for (const LedModeSpec &spec : ledModeSpecs()) {
        if (!ledModeApplies(spec.mode, frame))
            continue;
        if (!winner || spec.priority < winner->priority)
            winner = &spec;
    }

    // Idle always applies, so `winner` is only null if the table has lost its
    // Idle row — which ledModeSpec() would already have asserted on.
    return winner ? winner->mode : LedMode::Idle;
}

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

bool ledAnimationIsPaced(LedAnimation animation)
{
    return animation != LedAnimation::Static;
}

LedRingColours ledFrameAt(const LedModeSettings &mode, qint64 elapsedMs)
{
    switch (mode.animation) {
    case LedAnimation::Static:
    case LedAnimation::Rotating:
        // Rotating renders as Static until the protocol can place a stop
        // anywhere on the ring. See the declaration.
        return mode.colours;

    case LedAnimation::Breathing: {
        // Starts at the floor and rises, so a mode that has just become active
        // fades *in* rather than appearing at full brightness and dropping. The
        // cosine is what makes it linger at both ends, which is what separates a
        // breath from a triangle wave.
        const qreal phase = phaseOf(mode.periodMs, elapsedMs);
        const qreal wave = 0.5 - 0.5 * std::cos(2.0 * M_PI * phase);
        return scaled(mode.colours, kLedBreathFloor + (1.0 - kLedBreathFloor) * wave);
    }

    case LedAnimation::Blinking:
        // Lit for the first half, so like breathing a mode begins by showing
        // itself. Black rather than dimmed: a blink that only dips reads as a
        // stutter, and the base holds whatever it was last told, so the dark
        // half has to be said explicitly.
        return phaseOf(mode.periodMs, elapsedMs) < 0.5 ? mode.colours
                                                       : scaled(mode.colours, 0.0);
    }

    return mode.colours;
}

// ---------------------------------------------------------------------------
// The speed control
// ---------------------------------------------------------------------------

int sliderToPeriodMs(int slider)
{
    // Inverted: the slider says *speed*, so its right-hand end is the shortest
    // period. Getting this backwards is invisible in a screenshot and obvious on
    // a wheel, which is why it is here rather than in the page.
    const qreal t = qreal(qBound(0, slider, kLedSpeedSliderMax)) / kLedSpeedSliderMax;
    const qreal ratio = qreal(kLedMaxPeriodMs) / kLedMinPeriodMs;
    return qBound(kLedMinPeriodMs, qRound(kLedMaxPeriodMs / std::pow(ratio, t)),
                  kLedMaxPeriodMs);
}

int periodMsToSlider(int periodMs)
{
    const qreal clamped = qBound(kLedMinPeriodMs, periodMs, kLedMaxPeriodMs);
    const qreal ratio = qreal(kLedMaxPeriodMs) / kLedMinPeriodMs;
    const qreal t = std::log(kLedMaxPeriodMs / clamped) / std::log(ratio);
    return qBound(0, qRound(t * kLedSpeedSliderMax), kLedSpeedSliderMax);
}

// ---------------------------------------------------------------------------
// Between the widget's colours and the wire's
// ---------------------------------------------------------------------------

LedRingColours toWireColours(const QList<QColor> &colours)
{
    LedRingColours wire;
    const int count = qMin(int(colours.size()), kMaxLedStops);
    wire.count = quint8(count);
    for (int i = 0; i < count; ++i) {
        const QColor &c = colours.at(i);
        wire.stops[std::size_t(i)] = LedRgb{quint8(c.red()), quint8(c.green()), quint8(c.blue())};
    }
    return wire;
}

QList<QColor> toRingColours(const LedRingColours &colours)
{
    QList<QColor> out;
    const int count = qMin(int(colours.count), kMaxLedStops);
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const LedRgb &stop = colours.stops[std::size_t(i)];
        out.append(QColor(stop.r, stop.g, stop.b));
    }
    return out;
}

} // namespace hidcenter::simagic_alpha_evo
