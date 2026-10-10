// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>

#include "WheelControlDialog.h"
#include "WheelRpmLights.h"

class QButtonGroup;
class QCheckBox;
class QSpinBox;
class QToolButton;

namespace hidcenter::simagic_alpha_evo {

/// The settings window for a wheel's RPM lights: every pattern the wheel can
/// show, as a row of its LEDs, and the RPM percentage it is shown from.
///
/// A matrix, one row per pattern, lowest percentage at the top, one column per
/// LED. Choosing a colour, and whether it flashes, and then clicking LEDs paints
/// them. The percentages can only rise down the list: a row cannot go below the
/// one above it, and raising a row past the ones below takes them with it.
///
/// Every change is reported straight away through rpmLightsChanged(), as
/// WheelControlDialog describes.
class WheelRpmLightsDialog : public WheelControlDialog
{
    Q_OBJECT

public:
    /// `lights` is what the RPM lights are set to now, lowest percentage first.
    /// Every row is shown with as many LEDs as the first has.
    WheelRpmLightsDialog(const WheelControl &control, const RpmLights &lights,
                         QWidget *parent = nullptr);

    RpmLights lights() const { return m_lights; }

Q_SIGNALS:
    /// The user changed a percentage or painted an LED. Only for what the user
    /// does: the lights the window opens with are not reported.
    void rpmLightsChanged(const hidcenter::simagic_alpha_evo::RpmLights &lights);

private:
    /// Paint an LED with the colour and flashing chosen.
    void paint(int row, int led);

    /// Keep every row below `row` at least as high as the one above, and
    /// report the change.
    void percentChanged(int row);

    /// Draw an LED's cell as m_lights has it.
    void showLed(int row, int led);

    RpmLights m_lights;
    int m_ledCount = 0;

    QButtonGroup *m_colours = nullptr;
    QCheckBox *m_flashing = nullptr;
    QList<QSpinBox *> m_percents;
    QList<QList<QToolButton *>> m_cells;
};

} // namespace hidcenter::simagic_alpha_evo
