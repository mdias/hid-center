// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QCoreApplication>
#include <QString>
#include <QtGlobal>

#include <array>
#include <cstddef>
#include <optional>

#include "AlphaEvoWheelConfig.h"

namespace hidcenter::simagic_alpha_evo {

/// One colour the LEDs under a steering wheel's button can be set to.
struct ButtonColour
{
    /// Source string, left untranslated because the table is a constant. Use
    /// buttonColourName() rather than this directly.
    const char *name;

    QRgb rgb;

    /// The number the wheel knows the colour by, 1 to 8. See kWheelLedColourOff
    /// for what the numbers are and how they were found.
    quint8 wheelColour;
};

/// Every colour a button can be set to, in the wheel's own order. A button can
/// also be turned off, which is not a colour and so is not in here.
///
/// **The wheel's numbering, the app's shades.** Each entry is the colour the wheel
/// shows for that number. The shades drawn are the GT Neo's stock button colours
/// as its SVG has them, which read better on a dark picture of a wheel than
/// fully saturated ones.
inline constexpr std::array<ButtonColour, 8> kButtonColours = {{
    {QT_TRANSLATE_NOOP("ButtonColours", "Red"), 0xffff3b30, 1},
    {QT_TRANSLATE_NOOP("ButtonColours", "Orange"), 0xffffa21f, 2},
    {QT_TRANSLATE_NOOP("ButtonColours", "Yellow"), 0xffffe23a, 3},
    {QT_TRANSLATE_NOOP("ButtonColours", "Green"), 0xff35e05c, 4},
    {QT_TRANSLATE_NOOP("ButtonColours", "Blue"), 0xff4a6cff, 5},
    {QT_TRANSLATE_NOOP("ButtonColours", "Cyan"), 0xff26b8ff, 6},
    {QT_TRANSLATE_NOOP("ButtonColours", "Purple"), 0xffb36bff, 7},
    {QT_TRANSLATE_NOOP("ButtonColours", "White"), 0xffeef0f4, 8},
}};

constexpr bool buttonColoursInWheelOrder()
{
    for (std::size_t i = 0; i < kButtonColours.size(); ++i) {
        if (kButtonColours[i].wheelColour != i + 1)
            return false;
    }
    return kWheelLedColourOff == int(kButtonColours.size()) + 1;
}

static_assert(buttonColoursInWheelOrder(),
              "entry i is the wheel's colour i + 1, and off comes straight after");

/// A colour's name, in the user's language.
inline QString buttonColourName(const ButtonColour &colour)
{
    return QCoreApplication::translate("ButtonColours", colour.name);
}

/// Where a colour is in kButtonColours, or -1 when it is not one of them. An
/// invalid colour, which is how a button turned off is kept, is not.
inline int buttonColourIndex(const QColor &colour)
{
    if (!colour.isValid())
        return -1;

    const QRgb rgb = colour.rgb() & RGB_MASK;
    for (std::size_t i = 0; i < kButtonColours.size(); ++i) {
        if ((kButtonColours[i].rgb & RGB_MASK) == rgb)
            return int(i);
    }
    return -1;
}

/// The wheel's number for a colour: 1 to 8, kWheelLedColourOff for an invalid
/// colour, and -1 for a shade that is not in kButtonColours.
inline int wheelColourOf(const QColor &colour)
{
    if (!colour.isValid())
        return kWheelLedColourOff;
    const int index = buttonColourIndex(colour);
    return index < 0 ? -1 : kButtonColours[std::size_t(index)].wheelColour;
}

/// The colour the wheel shows for a number, invalid for off. So is any number
/// the table has no colour for, such as the 0 sent for a colour with
/// no match: what the wheel does with one is not known.
inline QColor colourOfWheel(int wheelColour)
{
    for (const ButtonColour &entry : kButtonColours) {
        if (entry.wheelColour == wheelColour)
            return QColor(entry.rgb);
    }
    return {};
}

/// How a colour is written in the settings file: the English name in lower
/// case, or "off" for an invalid colour. Empty for a shade not in the table.
inline QString colourSettingsName(const QColor &colour)
{
    if (!colour.isValid())
        return QStringLiteral("off");
    const int index = buttonColourIndex(colour);
    return index < 0 ? QString()
                     : QString::fromLatin1(kButtonColours[std::size_t(index)].name).toLower();
}

/// The inverse of colourSettingsName(). Nothing is returned for a name it does
/// not know, which is different from off.
inline std::optional<QColor> colourFromSettingsName(const QString &name)
{
    if (name == QLatin1String("off"))
        return QColor();
    for (const ButtonColour &entry : kButtonColours) {
        if (name == QString::fromLatin1(entry.name).toLower())
            return QColor(entry.rgb);
    }
    return std::nullopt;
}

} // namespace hidcenter::simagic_alpha_evo
