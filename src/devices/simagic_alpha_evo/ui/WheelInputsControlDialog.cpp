// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelInputsControlDialog.h"

#include "ButtonMappingChoice.h"
#include "WheelLayouts.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {

WheelInputsControlDialog::WheelInputsControlDialog(const WheelControl &control,
                                                   const QList<std::optional<int>> &buttons,
                                                   WarningFunction warningFor, QWidget *parent)
    : WheelControlDialog(control, parent)
    , m_warningFor(std::move(warningFor))
{
    auto *box = new QGroupBox(tr("Button Mapping"), this);
    // The label, the list, and the warning beside it, which keeps its room
    // while hidden so the lists stay put.
    auto *grid = new QGridLayout(box);
    grid->setColumnStretch(3, 1);

    for (qsizetype action = 0; action < buttons.size(); ++action) {
        const std::optional<int> button = buttons.at(action);
        if (!button)
            continue;

        const int line = int(m_rows.size());
        ActionRow row;
        row.action = int(action);

        QString name = wheelControlActionName(control.kind, int(action));
        if (name.isEmpty())
            name = tr("Presses");
        auto *label = new QLabel(tr("%1:").arg(name), box);
        grid->addWidget(label, line, 0);

        row.list = newJoystickButtonList(box, *button);
        label->setBuddy(row.list);
        grid->addWidget(row.list, line, 1);

        row.warning = new WarningIcon(box);
        grid->addWidget(row.warning, line, 2);

        // Activated, so only a choice the user makes is reported.
        connect(row.list, &QComboBox::activated, this, [this, row](int index) {
            Q_EMIT joystickButtonChanged(row.action, row.list->itemData(index).toInt());
            showWarnings();
        });
        m_rows << row;
    }

    contentLayout()->addWidget(box);
    showWarnings();
}

void WheelInputsControlDialog::showWarnings()
{
    for (const ActionRow &row : std::as_const(m_rows)) {
        const int button = row.list->currentData().toInt();
        row.warning->setWarning(m_warningFor && button > 0 ? m_warningFor(row.action, button)
                                                           : QString());
    }
}

} // namespace hidcenter::simagic_alpha_evo
