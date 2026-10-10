// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <optional>

#include "WheelControlDialog.h"

class QComboBox;

namespace hidcenter::simagic_alpha_evo {

class WarningIcon;

/// The settings window for a control with no LEDs, such as an encoder or a
/// 7-way switch: the joystick button each of its actions presses, a list per
/// action in wheelControlActions() order.
///
/// Every choice is reported straight away, as WheelControlDialog describes.
class WheelInputsControlDialog : public WheelControlDialog
{
    Q_OBJECT

public:
    /// What to warn about `action` pressing joystick `button`: what else
    /// presses it. Empty when nothing does.
    using WarningFunction = std::function<QString(int action, int button)>;

    /// `buttons` has the joystick button each action presses now, from 1, by
    /// its index in wheelControlActions(). An action with nothing, or past the
    /// end, has no known input and is left out.
    ///
    /// `warningFor` is asked again for every list after each choice, since one
    /// of the control's own actions may be what shares a button. So whatever
    /// takes joystickButtonChanged() must have made the change before it
    /// returns.
    WheelInputsControlDialog(const WheelControl &control, const QList<std::optional<int>> &buttons,
                             WarningFunction warningFor, QWidget *parent = nullptr);

Q_SIGNALS:
    /// The user picked the joystick button `action` presses. Only for a choice
    /// made in a list.
    void joystickButtonChanged(int action, int button);

private:
    struct ActionRow
    {
        int action = 0;
        QComboBox *list = nullptr;
        WarningIcon *warning = nullptr;
    };

    void showWarnings();

    WarningFunction m_warningFor;
    QList<ActionRow> m_rows;
};

} // namespace hidcenter::simagic_alpha_evo
