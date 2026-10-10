// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMetaType>
#include <QtGlobal>

#include <array>
#include <cstddef>

namespace hidcenter::simagic_alpha_evo {

/// The most colour stops a ring packet can carry.
///
/// Sixteen encode to 166 payload bytes across four reports, so this is well
/// inside what the protocol will hold; the ceiling is a UI decision rather than
/// a wire one, and the structural checks in AlphaEvoLedProtocol.h are what say
/// so if it moves again.
///
/// LedRing has its own ceiling and deliberately does not share this one — it is
/// self-contained so it can be reused without dragging the protocol in. The two
/// are tied together by a static_assert in WheelbaseLedPage.cpp, the one file
/// that includes both, so raising the widget's limit past what the wire can
/// carry is a compile error rather than a truncated packet.
inline constexpr int kMaxLedStops = 16;

/// One colour stop, in the eight bits per channel the wire carries.
///
/// QColor keeps sixteen bits a channel and a colour space; the wheel takes
/// eight. Narrowing here rather than at the point of use means the packet
/// composer never sees a type it cannot be constexpr over.
struct LedRgb
{
    quint8 r = 0;
    quint8 g = 0;
    quint8 b = 0;

    friend constexpr bool operator==(const LedRgb &a, const LedRgb &c)
    {
        return a.r == c.r && a.g == c.g && a.b == c.b;
    }
    friend constexpr bool operator!=(const LedRgb &a, const LedRgb &c) { return !(a == c); }
};

/// The ring's colours in the form the wire wants them.
///
/// Fixed capacity with a live count rather than a list, for two reasons. The
/// packet composer is constexpr and asserts itself against captured reference
/// packets, which rules out any type that allocates. And this crosses to the I/O
/// thread through a queued invocation, where a trivially copyable struct is a
/// memcpy of thirty-one bytes with no shared reference count following it over.
///
/// This is the *stops*, not one entry per physical LED: it is what the user
/// chose and what the packet carries. How stops map onto the strip
/// is the firmware's business, and the packet only says where each one sits.
struct LedRingColours
{
    std::array<LedRgb, kMaxLedStops> stops{};

    /// How many entries of `stops` are live. Anything past it is ignored.
    quint8 count = 0;

    friend constexpr bool operator==(const LedRingColours &a, const LedRingColours &b)
    {
        if (a.count != b.count)
            return false;
        for (std::size_t i = 0; i < a.count; ++i) {
            if (a.stops[i] != b.stops[i])
                return false;
        }
        // Deliberately not comparing past `count`: LedRing keeps colours behind a
        // lowered count so raising it again restores them, so two rings showing
        // the same thing can differ in the tail. Comparing it would defeat the
        // page's "nothing changed, do not send" check.
        return true;
    }
    friend constexpr bool operator!=(const LedRingColours &a, const LedRingColours &b)
    {
        return !(a == b);
    }
};

} // namespace hidcenter::simagic_alpha_evo

/// Crosses to the I/O thread through a queued invocation, which needs it to be a
/// metatype — the same reason SimagicTelemetry is one.
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::LedRingColours)
