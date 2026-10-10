// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QString>

#include <optional>

#include "WheelControlDialog.h"

class QButtonGroup;
class QComboBox;

namespace hidcenter::simagic_alpha_evo {

class WarningIcon;

/// What a button's settings window opens with. A part left empty is a setting
/// the button does not have, and is left out of the window.
struct WheelButtonSettings
{
    /// What the button shows, invalid for off. One that is not in
    /// kButtonColours opens with nothing chosen.
    QColor colour;

    /// What lights it, one of the kLedTriggers numbers. A number with no entry
    /// opens with none chosen.
    std::optional<int> trigger;

    /// The joystick button it presses, from 1. One past kWheelButtonMax opens
    /// with nothing chosen.
    std::optional<int> joystickButton;

    /// What to say about choosing each joystick button, by its number: that
    /// other controls already press it. No entry for a button nothing else
    /// presses.
    QHash<int, QString> joystickButtonWarnings;
};

/// The settings window for a lit control on the wheel: the colour of its LEDs,
/// one of kButtonColours or off, and, where the wheel can be told, what lights
/// them and which joystick button it presses.
///
/// A button's, and also a lit label's, such as a GTC's ABS window, which has a
/// colour and a trigger but nothing that presses a joystick button.
///
/// Every choice is reported straight away through its signal, as
/// WheelControlDialog describes, so the wheel drawing shows it while the window
/// is still open.
class WheelButtonControlDialog : public WheelControlDialog
{
    Q_OBJECT

public:
    WheelButtonControlDialog(const WheelControl &control, const WheelButtonSettings &settings,
                             QWidget *parent = nullptr);

    /// The colour chosen, invalid for off.
    QColor colour() const { return m_colour; }

Q_SIGNALS:
    /// The user picked a colour, or off, which is an invalid colour. Only for a
    /// click: the choice the window opens with is not reported.
    void colourChanged(const QColor &colour);

    /// The user picked what lights the button. Only for a click.
    void triggerChanged(int trigger);

    /// The user picked the joystick button it presses. Only for a choice made
    /// in the list.
    void joystickButtonChanged(int button);

private:
    void pick(int wheelColour);

    /// The radio buttons for what lights the button, `trigger` checked.
    void addTriggers(int trigger);
    void pickTrigger(int trigger);

    /// The list of joystick buttons, `button` chosen, and the warning beside it.
    void addJoystickButton(int button);
    void pickJoystickButton(int index);

    /// Show the warning for the joystick button chosen, or hide it.
    void showJoystickButtonWarning();

    QButtonGroup *m_swatches = nullptr;
    QColor m_colour;
    int m_trigger = -1;

    QComboBox *m_joystickButton = nullptr;
    WarningIcon *m_joystickButtonWarning = nullptr;
    QHash<int, QString> m_joystickButtonWarnings;
};

} // namespace hidcenter::simagic_alpha_evo
