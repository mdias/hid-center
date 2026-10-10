// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelRpmLightsDialog.h"

#include "WheelButtonColours.h"
#include "WheelColourSwatches.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {
namespace {

constexpr int kCellPx = 22;

/// What an LED is drawn in when its colour number is one the wheel has no
/// name for: something set by other software, kept but not guessed at.
const QColor kUnnamedColour(0x80, 0x81, 0x88);

QColor shadeOf(int wheelColour)
{
    if (wheelColour == kWheelLedColourOff)
        return {};
    const QColor colour = colourOfWheel(wheelColour);
    return colour.isValid() ? colour : kUnnamedColour;
}

QString colourName(int wheelColour)
{
    if (wheelColour == kWheelLedColourOff)
        return QCoreApplication::translate("WheelRpmLightsDialog", "Off");
    for (const ButtonColour &colour : kButtonColours) {
        if (colour.wheelColour == wheelColour)
            return buttonColourName(colour);
    }
    return QCoreApplication::translate("WheelRpmLightsDialog", "Colour number %1").arg(wheelColour);
}

} // namespace

WheelRpmLightsDialog::WheelRpmLightsDialog(const WheelControl &control, const RpmLights &lights,
                                           QWidget *parent)
    : WheelControlDialog(control, parent)
    , m_lights(lights)
    , m_ledCount(lights.isEmpty() ? 0 : int(lights.first().leds.size()))
{
    for (RpmPattern &pattern : m_lights)
        pattern.leds.resize(m_ledCount);

    auto *hint = new QLabel(tr("Each row shows from its RPM percentage up to the next row's; below "
                               "the first row all LEDs are off. Choose a colour, then click LEDs "
                               "to paint them."),
                            this);
    hint->setWordWrap(true);
    contentLayout()->addWidget(hint);

    // --- The brush -----------------------------------------------------------
    auto *brush = new QGroupBox(tr("Paint with"), this);
    auto *brushRow = new QHBoxLayout(brush);
    m_colours = addWheelColourSwatches(brush, brushRow);
    m_colours->button(kButtonColours.front().wheelColour)->setChecked(true);

    brushRow->addSpacing(style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) * 2);
    m_flashing = new QCheckBox(tr("&Flashing"), brush);
    brushRow->addWidget(m_flashing);

    // An LED that is off has nothing to flash.
    connect(m_colours, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked)
            m_flashing->setEnabled(id != kWheelLedColourOff);
    });
    contentLayout()->addWidget(brush);

    // --- The matrix ----------------------------------------------------------
    auto *patterns = new QGroupBox(tr("Patterns"), this);
    auto *grid = new QGridLayout(patterns);
    grid->setHorizontalSpacing(2);
    grid->addWidget(new QLabel(tr("From"), patterns), 0, 0);
    for (int led = 0; led < m_ledCount; ++led) {
        auto *number = new QLabel(QString::number(led + 1), patterns);
        number->setAlignment(Qt::AlignCenter);
        number->setEnabled(false);
        grid->addWidget(number, 0, led + 1);
    }

    for (int row = 0; row < m_lights.size(); ++row) {
        auto *percent = new QSpinBox(patterns);
        percent->setRange(0, kWheelRpmThresholdMax);
        percent->setSuffix(tr(" %"));
        percent->setValue(m_lights.at(row).percent);
        // Typing 85 would otherwise pass through 8 and push every row below up
        // to it on the way.
        percent->setKeyboardTracking(false);
        percent->setToolTip(tr("The RPM percentage this row is shown from"));
        grid->addWidget(percent, row + 1, 0);
        m_percents << percent;

        QList<QToolButton *> cells;
        for (int led = 0; led < m_ledCount; ++led) {
            auto *cell = new QToolButton(patterns);
            cell->setAutoRaise(true);
            cell->setIconSize(QSize(kCellPx, kCellPx));
            grid->addWidget(cell, row + 1, led + 1, Qt::AlignCenter);
            connect(cell, &QToolButton::clicked, this, [this, row, led] { paint(row, led); });
            cells << cell;
        }
        m_cells << cells;
        for (int led = 0; led < m_ledCount; ++led)
            showLed(row, led);
    }

    // Each row held to the one above from the start. Lights that came in out of
    // order are shown put right, but not reported until the user changes
    // something.
    for (int row = 1; row < m_percents.size(); ++row) {
        const QSignalBlocker blocker(m_percents.at(row));
        m_percents.at(row)->setMinimum(m_percents.at(row - 1)->value());
    }
    for (int row = 0; row < m_percents.size(); ++row)
        m_lights[row].percent = m_percents.at(row)->value();

    // Connected only now, so the minimums set above are not reported.
    for (int row = 0; row < m_percents.size(); ++row) {
        connect(m_percents.at(row), &QSpinBox::valueChanged, this,
                [this, row] { percentChanged(row); });
    }

    contentLayout()->addWidget(patterns);
}

void WheelRpmLightsDialog::paint(int row, int led)
{
    RpmLed painted;
    painted.colour = m_colours->checkedId();
    painted.flashing = painted.colour != kWheelLedColourOff && m_flashing->isChecked();

    RpmLed &current = m_lights[row].leds[led];
    if (current == painted)
        return;

    current = painted;
    showLed(row, led);
    Q_EMIT rpmLightsChanged(m_lights);
}

void WheelRpmLightsDialog::percentChanged(int row)
{
    // A minimum above the value raises the value, so a row pushed past the ones
    // below carries them up with it, one after another. Blocked, so the whole
    // change is reported once.
    for (int below = row + 1; below < m_percents.size(); ++below) {
        const QSignalBlocker blocker(m_percents.at(below));
        m_percents.at(below)->setMinimum(m_percents.at(below - 1)->value());
    }

    for (int each = 0; each < m_percents.size(); ++each) {
        m_lights[each].percent = m_percents.at(each)->value();
        for (int led = 0; led < m_ledCount; ++led)
            showLed(each, led);
    }
    Q_EMIT rpmLightsChanged(m_lights);
}

void WheelRpmLightsDialog::showLed(int row, int led)
{
    const RpmLed &light = m_lights.at(row).leds.at(led);
    QToolButton *cell = m_cells.at(row).at(led);
    // Off plain rather than struck through as on its swatch: most of a bar is
    // off, and a field of strokes would bury the lit LEDs.
    cell->setIcon(QIcon(wheelColourCap(shadeOf(light.colour), kCellPx, devicePixelRatioF(),
                                       light.flashing, false)));

    const QString colour = light.flashing ? tr("%1, flashing").arg(colourName(light.colour))
                                          : colourName(light.colour);
    cell->setToolTip(tr("LED %1 from %2 %: %3").arg(led + 1).arg(m_lights.at(row).percent).arg(colour));
}

} // namespace hidcenter::simagic_alpha_evo
