// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QPixmap>

class QBoxLayout;
class QButtonGroup;
class QWidget;

namespace hidcenter::simagic_alpha_evo {

/// A light's cap as the settings windows show it: a disc of `colour`, or for
/// an invalid colour, which is off, the dark cap the wheel drawing shows an
/// unlit light with, struck through unless `strikeOff` is false. `flashing`
/// rings the disc with a dashed halo. `px` is the side in device-independent
/// pixels.
QPixmap wheelColourCap(const QColor &colour, int px, qreal pixelRatio, bool flashing = false,
                       bool strikeOff = true);

/// Add a swatch for every colour of kButtonColours and one for off to `row`:
/// checkable tool buttons, each a cap with its name under it, in an exclusive
/// group whose ids are the wheel's colour numbers, kWheelLedColourOff for off.
/// The group is parented to `parent`, and nothing is checked.
QButtonGroup *addWheelColourSwatches(QWidget *parent, QBoxLayout *row);

} // namespace hidcenter::simagic_alpha_evo
