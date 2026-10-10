// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCoreApplication>
#include <QString>
#include <QtGlobal>

#include <array>
#include <cstddef>
#include <optional>

#include "AlphaEvoWheelConfig.h"

namespace hidcenter::simagic_alpha_evo {

/// One thing that can make the LEDs under a steering wheel's button light.
struct LedTrigger
{
    /// Source string, left untranslated because the table is a constant. Use
    /// ledTriggerName() rather than this directly.
    const char *name;

    /// How it is written in the settings file. Never change one.
    const char *settingsName;

    /// The wheel's number for it, the low nibble of the LED slot. See
    /// kWheelLedTriggerStatic.
    quint8 wheelTrigger;

    /// Seen working: lit all the time, on ABS and on TC. The rest are
    /// guesses from their order and nothing more.
    bool confirmed;
};

/// Every trigger the wheel has a number for, in its own order.
inline constexpr std::array<LedTrigger, 6> kLedTriggers = {{
    {QT_TRANSLATE_NOOP("LedTriggers", "Always"), "always", kWheelLedTriggerStatic, true},
    {QT_TRANSLATE_NOOP("LedTriggers", "While ABS is active"), "abs", kWheelLedTriggerAbs, true},
    {QT_TRANSLATE_NOOP("LedTriggers", "While TC is active"), "tc", kWheelLedTriggerTc, true},
    {QT_TRANSLATE_NOOP("LedTriggers", "While the pit limiter is on"), "pit-limiter",
     kWheelLedTriggerPitLimiter, false},
    {QT_TRANSLATE_NOOP("LedTriggers", "While DRS is on"), "drs", kWheelLedTriggerDrs, false},
    {QT_TRANSLATE_NOOP("LedTriggers", "While a flag is out"), "flag", kWheelLedTriggerFlag, false},
}};

constexpr bool ledTriggersInWheelOrder()
{
    for (std::size_t i = 0; i < kLedTriggers.size(); ++i) {
        if (kLedTriggers[i].wheelTrigger != i)
            return false;
    }
    return true;
}

static_assert(ledTriggersInWheelOrder(), "entry i is the wheel's trigger i");

/// A trigger's name, in the user's language.
inline QString ledTriggerName(const LedTrigger &trigger)
{
    return QCoreApplication::translate("LedTriggers", trigger.name);
}

/// The table's entry for a trigger number, or null for one it has none for.
inline const LedTrigger *ledTriggerOf(int wheelTrigger)
{
    for (const LedTrigger &entry : kLedTriggers) {
        if (entry.wheelTrigger == wheelTrigger)
            return &entry;
    }
    return nullptr;
}

/// How a trigger is written in the settings file. Empty for a number the
/// table has no entry for.
inline QString triggerSettingsName(int wheelTrigger)
{
    const LedTrigger *entry = ledTriggerOf(wheelTrigger);
    return entry ? QString::fromLatin1(entry->settingsName) : QString();
}

/// The inverse of triggerSettingsName(). Nothing for a name it does not know.
inline std::optional<int> triggerFromSettingsName(const QString &name)
{
    for (const LedTrigger &entry : kLedTriggers) {
        if (name == QLatin1String(entry.settingsName))
            return entry.wheelTrigger;
    }
    return std::nullopt;
}

} // namespace hidcenter::simagic_alpha_evo
