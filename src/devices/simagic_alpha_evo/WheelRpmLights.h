// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

#include "AlphaEvoWheelConfig.h"
#include "WheelButtonColours.h"

namespace hidcenter::simagic_alpha_evo {

/// One LED of one pattern of a wheel's RPM lights.
struct RpmLed
{
    /// The wheel's colour number: 1 to 8 as kButtonColours, or
    /// kWheelLedColourOff. Anything else is a number the wheel was sent by
    /// something else, kept as it is.
    int colour = kWheelLedColourOff;

    bool flashing = false;

    friend bool operator==(const RpmLed &, const RpmLed &) = default;
};

/// One pattern of the RPM lights: the RPM percentage it is shown from, and
/// its LEDs, from the first.
struct RpmPattern
{
    int percent = 0;
    QList<RpmLed> leds;

    friend bool operator==(const RpmPattern &, const RpmPattern &) = default;
};

/// A wheel's RPM lights as the RPM lights window shows them: every pattern,
/// lowest percentage first, which is the block's pattern 9 first.
using RpmLights = QList<RpmPattern>;

/// How a pattern is written in the settings file: the percentage, then each
/// LED's colour as colourSettingsName() writes it, or its number when it has
/// no name, with `*` after a flashing one. `80 green green yellow red* off`.
inline QString rpmPatternSettingsValue(const RpmPattern &pattern)
{
    QStringList parts{QString::number(pattern.percent)};
    for (const RpmLed &led : pattern.leds) {
        const QColor colour = colourOfWheel(led.colour);
        QString part = colour.isValid() || led.colour == kWheelLedColourOff
                           ? colourSettingsName(colour)
                           : QString::number(led.colour);
        if (led.flashing)
            part += u'*';
        parts << part;
    }
    return parts.join(u' ');
}

/// The inverse of rpmPatternSettingsValue(). Nothing for a value it cannot
/// read whole, so a damaged entry is dropped rather than half applied.
inline std::optional<RpmPattern> rpmPatternFromSettingsValue(const QString &value)
{
    const QStringList parts = value.split(u' ', Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return std::nullopt;

    RpmPattern pattern;
    bool ok = false;
    pattern.percent = parts.first().toInt(&ok);
    if (!ok || pattern.percent < 0 || pattern.percent > kWheelRpmThresholdMax)
        return std::nullopt;

    for (qsizetype i = 1; i < parts.size(); ++i) {
        QString part = parts.at(i);
        RpmLed led;
        led.flashing = part.endsWith(u'*');
        if (led.flashing)
            part.chop(1);

        if (const std::optional<QColor> colour = colourFromSettingsName(part)) {
            led.colour = wheelColourOf(*colour);
        } else {
            led.colour = part.toInt(&ok);
            if (!ok || led.colour < 0 || led.colour > 15)
                return std::nullopt;
        }
        pattern.leds << led;
    }
    return pattern;
}

} // namespace hidcenter::simagic_alpha_evo
