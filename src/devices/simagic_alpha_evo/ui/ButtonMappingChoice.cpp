// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ButtonMappingChoice.h"

#include "AlphaEvoWheelConfig.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QStyle>

namespace hidcenter::simagic_alpha_evo {

QComboBox *newJoystickButtonList(QWidget *parent, int button)
{
    const auto name = [](int number) {
        return QCoreApplication::translate("ButtonMappingChoice", "Button %1").arg(number);
    };

    auto *list = new QComboBox(parent);
    for (int number = 1; number <= kWheelButtonMax; ++number)
        list->addItem(name(number), number);

    const int index = list->findData(button);
    if (index < 0)
        list->setPlaceholderText(name(button));
    list->setCurrentIndex(index);
    return list;
}

WarningIcon::WarningIcon(QWidget *parent)
    : QLabel(parent)
{
    const int iconPx = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, this)
                  .pixmap(QSize(iconPx, iconPx), devicePixelRatioF()));

    QSizePolicy policy = sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    setSizePolicy(policy);
    setVisible(false);
}

void WarningIcon::setWarning(const QString &text)
{
    setToolTip(text);
    setAccessibleDescription(text);
    setVisible(!text.isEmpty());
}

} // namespace hidcenter::simagic_alpha_evo
