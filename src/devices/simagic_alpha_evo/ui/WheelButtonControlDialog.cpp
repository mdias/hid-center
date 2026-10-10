// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelButtonControlDialog.h"

#include "ButtonMappingChoice.h"
#include "WheelButtonColours.h"
#include "WheelColourSwatches.h"
#include "WheelLedTriggers.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {

WheelButtonControlDialog::WheelButtonControlDialog(const WheelControl &control,
                                                   const WheelButtonSettings &settings,
                                                   QWidget *parent)
    : WheelControlDialog(control, parent)
    , m_colour(settings.colour)
    , m_joystickButtonWarnings(settings.joystickButtonWarnings)
{
    auto *box = new QGroupBox(tr("LED colour"), this);
    auto *row = new QHBoxLayout(box);
    m_swatches = addWheelColourSwatches(box, row);

    // A colour from outside the palette checks nothing, rather than the nearest
    // colour, which would claim the button shows something it does not.
    if (QAbstractButton *checked = m_swatches->button(wheelColourOf(m_colour)))
        checked->setChecked(true);

    // Clicked rather than toggled, so only the user's choice is reported and not
    // the check made just above.
    connect(m_swatches, &QButtonGroup::idClicked, this, &WheelButtonControlDialog::pick);

    contentLayout()->addWidget(box);

    if (settings.trigger)
        addTriggers(*settings.trigger);
    if (settings.joystickButton)
        addJoystickButton(*settings.joystickButton);
}

void WheelButtonControlDialog::addTriggers(int trigger)
{
    auto *box = new QGroupBox(tr("When it lights"), this);
    auto *grid = new QGridLayout(box);
    auto *group = new QButtonGroup(this);

    // Two columns, filled down, so the list keeps the wheel's order and the
    // window stays about as wide as the colours above.
    const int rows = (int(kLedTriggers.size()) + 1) / 2;
    for (std::size_t i = 0; i < kLedTriggers.size(); ++i) {
        const LedTrigger &entry = kLedTriggers[i];
        auto *radio = new QRadioButton(ledTriggerName(entry), box);
        if (!entry.confirmed) {
            radio->setToolTip(tr("Untested: may not behave as named."));
        }
        group->addButton(radio, entry.wheelTrigger);
        grid->addWidget(radio, int(i) % rows, int(i) / rows);
    }

    m_trigger = trigger;
    if (QAbstractButton *checked = group->button(trigger))
        checked->setChecked(true);

    // Clicked, for the same reason as the colours.
    connect(group, &QButtonGroup::idClicked, this, &WheelButtonControlDialog::pickTrigger);

    contentLayout()->addWidget(box);
}

void WheelButtonControlDialog::pickTrigger(int trigger)
{
    if (trigger == m_trigger)
        return;

    m_trigger = trigger;
    Q_EMIT triggerChanged(m_trigger);
}

void WheelButtonControlDialog::addJoystickButton(int button)
{
    auto *box = new QGroupBox(tr("Joystick button"), this);
    auto *row = new QHBoxLayout(box);

    auto *label = new QLabel(tr("&Presses"), box);
    row->addWidget(label);
    m_joystickButton = newJoystickButtonList(box, button);
    label->setBuddy(m_joystickButton);
    row->addWidget(m_joystickButton);

    m_joystickButtonWarning = new WarningIcon(box);
    row->addWidget(m_joystickButtonWarning);
    row->addStretch(1);
    showJoystickButtonWarning();

    // Activated, so only a choice the user makes is reported.
    connect(m_joystickButton, &QComboBox::activated, this,
            &WheelButtonControlDialog::pickJoystickButton);

    contentLayout()->addWidget(box);
}

void WheelButtonControlDialog::pickJoystickButton(int index)
{
    showJoystickButtonWarning();
    Q_EMIT joystickButtonChanged(m_joystickButton->itemData(index).toInt());
}

void WheelButtonControlDialog::showJoystickButtonWarning()
{
    m_joystickButtonWarning->setWarning(
        m_joystickButtonWarnings.value(m_joystickButton->currentData().toInt()));
}

void WheelButtonControlDialog::pick(int wheelColour)
{
    const QColor picked = colourOfWheel(wheelColour);
    if (picked == m_colour)
        return;

    m_colour = picked;
    Q_EMIT colourChanged(m_colour);
}

} // namespace hidcenter::simagic_alpha_evo
