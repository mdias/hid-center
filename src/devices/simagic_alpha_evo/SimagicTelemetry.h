// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMetaType>
#include <QtGlobal>

#include "core/telemetry/TelemetryFrame.h"

namespace hidcenter::simagic_alpha_evo {

/// Telemetry in the form the wheelbase wants it.
///
/// The application's TelemetryFrame is in the units a car has — revolutions per
/// minute, and so on. This is what goes on the wire, and the conversion between
/// them is the module's job, because only the module knows what the hardware
/// expects.
///
/// Every member is shown on the Telemetry page, so adding one here means adding
/// a row there. Every member also goes into TelemetryBlock, which is the point:
/// this is the report's contents in the module's own terms, and composing the
/// bytes from it needs no second conversion.
struct SimagicTelemetry
{
    /// Engine speed, clamped to the width of the field that carries it rather
    /// than to the car's limiter — the frame keeps true revs through an overrev,
    /// and 65535 is where this stops being representable, not where it stops
    /// being true.
    quint16 rpm = 0;

    quint8 rpmPercent = 0;  ///< 0..100, from TelemetryFrame::rpmPercent().
    quint8 rpmLeds = 10; ///< 0..10, led program

    bool tractionControl = false;
    bool abs = false;
    bool pitLimiter = false;

    /// hidcenter::TelemetryFlag straight through — composeTelemetry() is what maps
    /// it onto the wire's single byte, the same place rpm becomes rpmPercent.
    TelemetryFlag flag = TelemetryFlag::None;

    friend bool operator==(const SimagicTelemetry &a, const SimagicTelemetry &b)
    {
        // rpm has to be in here. Comparing percentages alone calls two readings
        // ninety revs apart identical, and the page would sit still through most
        // of a gear.
        return a.rpm == b.rpm && a.rpmPercent == b.rpmPercent
            && a.tractionControl == b.tractionControl && a.abs == b.abs
            && a.pitLimiter == b.pitLimiter && a.flag == b.flag;
    }
    friend bool operator!=(const SimagicTelemetry &a, const SimagicTelemetry &b)
    {
        return !(a == b);
    }
};

} // namespace hidcenter::simagic_alpha_evo

/// Crosses to the I/O thread through a queued invocation, which needs it to be a
/// metatype — the same reason StatusReport is one, and just as cheap: a handful
/// of scalars whose copy is a memcpy.
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::SimagicTelemetry)
