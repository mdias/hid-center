// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <array>

#include "hidcenter/core_export.h"

namespace hidcenter {

/// Full scale of the RPM readout when nothing has said what it should be.
///
/// A car's real limiter arrives with every frame, and that is what the bar and
/// the percentages actually use. This is the resting scale: before a sim
/// connects, in test mode, and for the frames a sim sends from menus and replays
/// where the limiter reads zero. Round rather than plausible, precisely so it
/// does not get mistaken for a fact about a car.
inline constexpr int kTelemetryRpmMax = 10000;

/// Where the tyre colour scale centres when the sim does not say what this
/// compound wants.
///
/// Same job as kTelemetryRpmMax, and chosen the same way: a resting scale for
/// the frames that carry no optimum — before a sim connects, in test mode, and
/// from menus and replays. Round rather than plausible, precisely so it does not
/// get mistaken for a fact about a tyre.
inline constexpr int kTelemetryOptimalTyreTempC = 85;

/// The corner-worker flag panel currently shown for this car, or none.
///
/// Not a set — a marshal shows one panel at a time, and this is the one the sim
/// is signalling right now. `None` means no flag is out, the same way
/// `tractionControl` false means it is not cutting in: a source with nothing to
/// report leaves this at the default rather than a value meaning "unknown".
///
/// `Checkered`, `Orange` and `Red` are carried here even though SIMAGIC's own
/// wire format has no slot for them — because this struct describes what a sim can say, not what one device can
/// show. A module that cannot represent a value decides what to do with it,
/// the same way it decides how to turn rpm into a percentage.
enum class TelemetryFlag : quint8 {
    None = 0,
    Green,
    Yellow,
    Blue,
    White,
    Black,
    Checkered,
    Orange,
    Red,
};

/// A flag's name, for a readout. Kept beside the enum so the sidebar's
/// Telemetry page and a module's own Data group do not each keep a switch of
/// their own.
HIDCENTER_CORE_EXPORT QString telemetryFlagName(TelemetryFlag flag);

/// The wheels in the order a sim usually sends them, which is also the order
/// they are drawn in: front row first, left before right.
enum TelemetryWheelIndex {
    TelemetryFrontLeft = 0,
    TelemetryFrontRight,
    TelemetryRearLeft,
    TelemetryRearRight,
    TelemetryWheelCount
};

/// One corner's temperatures, in whole degrees Celsius.
///
/// **Whole degrees on purpose.** TelemetryHub::setFrame() publishes nothing when
/// a frame matches the last one, and that dedupe is what makes a car sitting at
/// a steady idle cost the application nothing. Sixteen raw doubles would differ
/// in their last bits on every packet and turn frameChanged() into a 90 Hz
/// signal for every consumer. A degree is also all a readout of this kind can
/// honestly show.
struct HIDCENTER_CORE_EXPORT TelemetryWheel
{
    /// Disc temperature. Hundreds of degrees under braking, unlike the tyre.
    int brakeTemp = 0;

    /// Across the contact patch: left, centre, right. **Not** inside, centre,
    /// outside — which side is the inside depends on which side of the car this
    /// wheel is on, and a sim does not do that translation for us.
    std::array<int, 3> tyreTemp {0, 0, 0};

    /// What this compound wants, when the sim says. Zero means it did not, and a
    /// reader should fall back to kTelemetryOptimalTyreTempC rather than treat
    /// zero as a temperature.
    ///
    /// It travels with the wheel for the reason rpmMax travels with the revs: it
    /// changes when the car does, and a value paired with the wrong scale is a
    /// wrong reading.
    int optimalTyreTemp = 0;

    friend bool operator==(const TelemetryWheel &a, const TelemetryWheel &b)
    {
        return a.brakeTemp == b.brakeTemp && a.tyreTemp == b.tyreTemp
            && a.optimalTyreTemp == b.optimalTyreTemp;
    }
    friend bool operator!=(const TelemetryWheel &a, const TelemetryWheel &b) { return !(a == b); }
};

/// One instant of sim telemetry, as the application sees it.
///
/// Deliberately not device-shaped. A module turns this into whatever its
/// hardware wants — SIMAGIC packs the RPM into a single percentage byte — and
/// that conversion belongs in the module, not here. It is equally not
/// *sim*-shaped: this is the common denominator every telemetry source plugin
/// fills in, and adding a sim means a new source module rather than a change to
/// this struct.
///
/// In its own header, away from TelemetryHub, so that a source plugin can
/// describe a reading without compiling — or being able to reach — the hub it
/// will never touch. Including TelemetryHub.h still gets you this.
struct HIDCENTER_CORE_EXPORT TelemetryFrame
{
    /// True engine speed. Not clamped to rpmMax: an engine on an overrev really
    /// is past its limiter for a moment, and a readout that hides that is worse
    /// than one that pins its bar and reports the number.
    int rpm = 0;

    /// Full scale — this car's rev limit, or kTelemetryRpmMax until something
    /// says otherwise. It travels with the frame rather than sitting beside it
    /// because it changes when the car does, and a value paired with the wrong
    /// scale is a wrong reading.
    int rpmMax = kTelemetryRpmMax;

    bool tractionControl = false;
    bool abs = false;

    /// Whether the pit lane speed limiter is engaged.
    bool pitLimiter = false;

    /// The flag panel currently shown for this car, or TelemetryFlag::None.
    TelemetryFlag flag = TelemetryFlag::None;

    /// The four corners, indexed by TelemetryWheelIndex.
    std::array<TelemetryWheel, TelemetryWheelCount> wheels;

    /// 0..100 of rpmMax, which is the form a device usually wants. Clamped at
    /// both ends, and safe against a nonsensical rpmMax.
    int rpmPercent() const;

    friend bool operator==(const TelemetryFrame &a, const TelemetryFrame &b)
    {
        // rpmMax belongs here. Leave it out and changing car publishes nothing
        // when the revs happen to match, so every bar keeps the old scale.
        return a.rpm == b.rpm && a.rpmMax == b.rpmMax
            && a.tractionControl == b.tractionControl && a.abs == b.abs
            && a.pitLimiter == b.pitLimiter && a.flag == b.flag && a.wheels == b.wheels;
    }
    friend bool operator!=(const TelemetryFrame &a, const TelemetryFrame &b) { return !(a == b); }
};

} // namespace hidcenter

// A frame crosses a thread boundary on its way from a source to the router, so
// Qt has to know how to copy one into a queued call. main.cpp registers it.
Q_DECLARE_METATYPE(hidcenter::TelemetryFrame)
