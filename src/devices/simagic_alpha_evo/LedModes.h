// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <array>

#include "SimagicLeds.h"
#include "core/telemetry/TelemetryFrame.h"

/// What the wheel's LED strip shows, and when.
///
/// Two things live here, kept apart on purpose. `SimagicLeds.h` next door is
/// about the *wire* — the colours in the eight bits a channel the base takes.
/// This is about what the user chose and what the car is doing, and it turns the
/// two into a set of colours for a moment in time.
///
/// Everything in this header is free and pure. Nothing samples a clock, opens a
/// device or touches a widget, which is what lets the on-screen ring and the
/// animation thread animate from one definition rather than two that have to be
/// kept in step.
namespace hidcenter::simagic_alpha_evo {

// ---------------------------------------------------------------------------
// What a mode is
// ---------------------------------------------------------------------------

/// What an animation does to a mode's colours over its cycle.
///
/// The numbers are not the saved form — LedModeStore writes names — so a value
/// may be inserted here without invalidating anyone's settings.
enum class LedAnimation : quint8 {
    Static = 0,  ///< The colours, unchanging.
    Breathing,   ///< The colours fading down and back up.
    Rotating,    ///< The colours turning around the ring. See ledFrameAt().
    Blinking,    ///< The colours, then black, then the colours.
};

/// Which look the wheel is wearing.
///
/// Not a set of flags: the strip can only show one thing, so a frame that says
/// both traction control and ABS still resolves to one of them. See
/// ledModeForFrame() for which, and ledModeSpecs() for where that is decided.
enum class LedMode : quint8 {
    Idle = 0,
    TractionControl,
    Abs,
};

inline constexpr int kLedModeCount = 3;

/// One full cycle of an animation: a breath down and back up, one blink on and
/// off, one turn of the ring.
///
/// The floor is where a fade stops reading as an animation and starts reading as
/// a flicker. The ceiling is where a breath stops reading as a breath and starts
/// reading as a colour that is simply wrong. The default is a resting-breath
/// cadence, which is what an idle wheel wants and the only one of the three that
/// is a taste rather than a limit.
inline constexpr int kLedMinPeriodMs = 100;
inline constexpr int kLedMaxPeriodMs = 2000;
inline constexpr int kLedDefaultPeriodMs = 200;

/// The dimmest a breath goes, as a fraction of the colour chosen.
///
/// Not zero. A strip that reaches black once a second reads as one that has
/// failed and is coming back, rather than as one that is breathing; a floor is
/// what keeps the colour readable all the way through the cycle. It is also
/// honest about the hardware — the strip sits behind a diffuser, so a stop set
/// to black does not read black anyway.
inline constexpr qreal kLedBreathFloor = 0.08;

/// The fastest ring frames may leave the host, as a gap in milliseconds.
///
/// Twenty-five a second. The clock that drives an animation is the telemetry
/// clock, which the user may put as high as ninety, and the ring neither needs
/// nor wants that: at sixteen stops one frame is four 64-byte feature reports,
/// so this alone is already a hundred transfers a second sharing a device with
/// telemetry at sixty. A strip behind a diffuser shows nothing faster.
inline constexpr int kLedMinFrameGapMs = 40;

/// One mode's whole configuration.
///
/// Trivially copyable, for the two reasons LedRingColours is: it crosses to the
/// animation thread as a queued call, where a struct that allocates would drag a
/// shared reference count over with it, and it is small enough that copying it
/// per tick costs nothing worth measuring.
struct LedModeSettings
{
    LedRingColours colours;
    LedAnimation animation = LedAnimation::Static;

    /// Clamped to kLedMinPeriodMs..kLedMaxPeriodMs by everything that sets it.
    /// Meaningless for Static, and left at whatever it was rather than zeroed,
    /// so switching away from Static and back does not lose the speed.
    quint16 periodMs = kLedDefaultPeriodMs;

    friend bool operator==(const LedModeSettings &a, const LedModeSettings &b)
    {
        return a.colours == b.colours && a.animation == b.animation
            && a.periodMs == b.periodMs;
    }
    friend bool operator!=(const LedModeSettings &a, const LedModeSettings &b) { return !(a == b); }
};

/// Every mode's settings, indexed by LedMode.
struct LedModeSet
{
    std::array<LedModeSettings, kLedModeCount> modes{};

    LedModeSettings &at(LedMode mode) { return modes[std::size_t(mode)]; }
    const LedModeSettings &at(LedMode mode) const { return modes[std::size_t(mode)]; }

    friend bool operator==(const LedModeSet &a, const LedModeSet &b) { return a.modes == b.modes; }
    friend bool operator!=(const LedModeSet &a, const LedModeSet &b) { return !(a == b); }
};

// ---------------------------------------------------------------------------
// The list of modes
// ---------------------------------------------------------------------------

/// One mode, described once.
///
/// The page builds its sections from this table rather than writing three of
/// everything out, and the store saves under `key`, so adding a mode is a row
/// here and nothing else.
struct LedModeSpec
{
    LedMode mode;

    /// The name this mode is saved under. **Never change one**: it is what a
    /// settings file written by an older build says, and renaming it silently
    /// discards whatever the user had set.
    const char *key;

    /// Source strings, left untranslated because this table is a constant and
    /// translating needs a running application. Use ledModeTitle() and
    /// ledModeBlurb() rather than these directly, or the translation will be
    /// looked up in whatever context happens to be asking.
    const char *title;
    const char *blurb;

    /// Which mode wins when more than one applies. **Lower takes precedence.**
    ///
    /// A separate field rather than the table's order, because the table has a
    /// second job: it is the order the page's accordion lists modes in, and
    /// there the calm default belongs at the top while the most urgent mode
    /// belongs at the bottom. Tying the two together would mean changing the UI
    /// to change the rule.
    ///
    /// ABS beats traction control because braking is the more urgent of the two
    /// and the strip can only say one thing — a car doing both at once is one
    /// that is braking hard, and that is what the driver needs to see. Idle sits
    /// last and applies always, which is what makes it the fallback rather than
    /// a case anything has to test for.
    int priority;
};

/// Every mode, **in the order the UI lists them**: the default first, then the
/// exceptions. Precedence is LedModeSpec::priority, not this order.
const std::array<LedModeSpec, kLedModeCount> &ledModeSpecs();

/// The spec for one mode. Never null: every enumerator has a row.
const LedModeSpec &ledModeSpec(LedMode mode);

/// A mode's name and description, in the user's language.
QString ledModeTitle(LedMode mode);
QString ledModeBlurb(LedMode mode);

/// Whether a frame puts the wheel in this mode. Idle applies always; it is its
/// priority that makes it the fallback.
bool ledModeApplies(LedMode mode, const TelemetryFrame &frame);

/// The mode a frame calls for: the applicable one with the lowest priority
/// number.
///
/// Free and pure, so precedence is checkable without a device, a thread or a
/// telemetry source.
LedMode ledModeForFrame(const TelemetryFrame &frame);

// ---------------------------------------------------------------------------
// The frame
// ---------------------------------------------------------------------------

/// The colours a mode shows `elapsedMs` into its cycle.
///
/// **A function of elapsed time, not of a step counter.** The same settings and
/// the same time give the same frame on any thread, which is what lets the
/// on-screen ring and the wheel animate from one definition without being kept
/// in step with each other — and what would let a second set of LEDs animate in
/// phase with this one from a shared epoch rather than a shared clock. A step
/// counter would have made both of those a synchronisation problem.
///
/// Negative time is taken as zero rather than wrapped, so a clock that has not
/// started yet shows the beginning of the cycle rather than a point chosen by
/// the sign rules of integer modulo.
///
/// Rotating is deliberately Static for now. Turning the ring means putting stops
/// at angles that are not LED boundaries, which is `keyIdForStop`'s precise-angle
/// path in AlphaEvoLedProtocol.h and a protocol change of its own. The value
/// exists here, and in the saved file, so the day that lands nothing else has to.
LedRingColours ledFrameAt(const LedModeSettings &mode, qint64 elapsedMs);

/// Whether an animation has a speed worth setting. Static has not, and the page
/// hides the slider rather than showing one that does nothing.
bool ledAnimationIsPaced(LedAnimation animation);

// ---------------------------------------------------------------------------
// The speed control
// ---------------------------------------------------------------------------

/// The slider that sets a period runs 0..100 and is **logarithmic**.
///
/// 250 ms to 5 s is a twentyfold range. Spread linearly, half the travel would
/// sit between 2.6 and 5 seconds — all of which reads as "slow" — and every
/// setting worth having would be crushed into the first centimetre. Geometric
/// spacing gives each end the same proportional resolution.
inline constexpr int kLedSpeedSliderMax = 100;

int sliderToPeriodMs(int slider);
int periodMsToSlider(int periodMs);

// ---------------------------------------------------------------------------
// Between the widget's colours and the wire's
// ---------------------------------------------------------------------------

/// QColor keeps sixteen bits a channel and a colour space; the wheel takes
/// eight. Narrowing here, on the GUI thread, is what keeps QtGui out of the
/// threads that sample, compose and transfer.
LedRingColours toWireColours(const QList<QColor> &colours);
QList<QColor> toRingColours(const LedRingColours &colours);

} // namespace hidcenter::simagic_alpha_evo

/// Both cross to the animation thread through queued invocations, the same
/// reason LedRingColours and SimagicTelemetry are metatypes.
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::LedMode)
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::LedModeSettings)
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::LedModeSet)
