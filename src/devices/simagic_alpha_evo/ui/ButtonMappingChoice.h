// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLabel>
#include <QString>

class QComboBox;
class QWidget;

namespace hidcenter::simagic_alpha_evo {

/// A list of the joystick buttons a wheel control can press, "Button 1" to
/// kWheelButtonMax, each entry's data its number, with `button` chosen. A
/// number the list does not offer is still named, as the placeholder, rather
/// than shown as the first entry, which would claim the control presses that.
QComboBox *newJoystickButtonList(QWidget *parent, int button);

/// The style's small warning icon, put beside a choice, with what it warns
/// about as its tooltip. Hidden while it has nothing to say, but still taking
/// its room, so the choice beside it does not move when it comes and goes.
class WarningIcon : public QLabel
{
public:
    explicit WarningIcon(QWidget *parent = nullptr);

    /// Warn about `text`, or hide for an empty one.
    void setWarning(const QString &text);
};

} // namespace hidcenter::simagic_alpha_evo
