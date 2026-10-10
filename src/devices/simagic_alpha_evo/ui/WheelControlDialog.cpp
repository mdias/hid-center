// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelControlDialog.h"

#include "WheelLayouts.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {

WheelControlDialog::WheelControlDialog(const WheelControl &control, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(wheelControlName(control));

    auto *layout = new QVBoxLayout(this);

    m_content = new QVBoxLayout;
    layout->addLayout(m_content);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace hidcenter::simagic_alpha_evo
