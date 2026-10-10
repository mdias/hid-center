// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/telemetry_page/TelemetryDetailsPanel.h"

#include "app/telemetry_page/RpmBar.h"
#include "app/telemetry_page/WheelTempDisplay.h"
#include "core/telemetry/TelemetryHub.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace hidcenter {
namespace {

/// Make a control a readout rather than a control, without disabling it.
///
/// setEnabled(false) would grey it, and a telemetry light that dims itself
/// exactly when you most want to read it is the wrong trade. It keeps its normal
/// contrast and simply stops responding.
void setInteractive(QWidget *control, bool interactive)
{
    control->setAttribute(Qt::WA_TransparentForMouseEvents, !interactive);
    control->setFocusPolicy(interactive ? Qt::StrongFocus : Qt::NoFocus);
}

} // namespace

TelemetryDetailsPanel::TelemetryDetailsPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    TelemetryHub *hub = TelemetryHub::instance();
    connect(hub, &TelemetryHub::frameChanged, this, &TelemetryDetailsPanel::refreshFromHub);
    connect(hub, &TelemetryHub::testModeChanged, this, &TelemetryDetailsPanel::refreshEditable);
    // Same slot: it is what chooses between the three mode messages, and until
    // there was a source to flip this flag nothing ever made it re-run.
    connect(hub, &TelemetryHub::receivingChanged, this, &TelemetryDetailsPanel::refreshEditable);

    refreshFromHub();
    refreshEditable();
}

void TelemetryDetailsPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    // --- Mode -------------------------------------------------------------
    auto *modeRow = new QHBoxLayout;

    m_testMode = new QCheckBox(tr("Test mode"), this);
    m_testMode->setToolTip(
        tr("Drive these values by hand instead of taking them from a game. Everything "
           "downstream — the device modules that consume telemetry — sees them as if they "
           "were real."));
    connect(m_testMode, &QCheckBox::toggled, this,
            [](bool on) { TelemetryHub::instance()->setTestMode(on); });
    modeRow->addWidget(m_testMode);

    modeRow->addStretch(1);

    auto *popOut = new QPushButton(tr("Pop out"), this);
    popOut->setToolTip(tr("Open the telemetry details in their own window, so they can sit "
                          "beside a device page."));
    connect(popOut, &QPushButton::clicked, this, &TelemetryDetailsPanel::popOutRequested);
    popOut->setVisible(false);
    m_popOut = popOut;
    modeRow->addWidget(popOut);

    layout->addLayout(modeRow);

    m_mode = new QLabel(this);
    m_mode->setWordWrap(true);
    layout->addWidget(m_mode);

    // --- Values -----------------------------------------------------------
    auto *box = new QGroupBox(tr("Values"), this);
    auto *form = new QFormLayout(box);

    auto *rpmRow = new QWidget(box);
    auto *rpmLayout = new QHBoxLayout(rpmRow);
    rpmLayout->setContentsMargins(0, 0, 0, 0);

    m_rpm = new RpmBar(rpmRow);
    rpmLayout->addWidget(m_rpm, 1);

    m_rpmValue = new QLabel(rpmRow);
    // Fixed width from the widest value it will ever hold, so the bar beside it
    // does not resize on every update.
    m_rpmValue->setMinimumWidth(
        m_rpmValue->fontMetrics().horizontalAdvance(tr("%1 rpm").arg(kTelemetryRpmMax)) + 8);
    m_rpmValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rpmLayout->addWidget(m_rpmValue);

    connect(m_rpm, &RpmBar::valueChanged, this, &TelemetryDetailsPanel::publish);
    form->addRow(tr("Engine speed"), rpmRow);

    m_tractionControl = new QCheckBox(tr("Active"), box);
    connect(m_tractionControl, &QCheckBox::toggled, this, &TelemetryDetailsPanel::publish);
    form->addRow(tr("Traction control"), m_tractionControl);

    m_abs = new QCheckBox(tr("Active"), box);
    connect(m_abs, &QCheckBox::toggled, this, &TelemetryDetailsPanel::publish);
    form->addRow(tr("ABS"), m_abs);

    m_pitLimiter = new QCheckBox(tr("Active"), box);
    connect(m_pitLimiter, &QCheckBox::toggled, this, &TelemetryDetailsPanel::publish);
    form->addRow(tr("Pit limiter"), m_pitLimiter);

    m_flag = new QComboBox(box);
    for (int i = 0; i <= int(TelemetryFlag::Red); ++i)
        m_flag->addItem(telemetryFlagName(TelemetryFlag(i)), i);
    connect(m_flag, &QComboBox::currentIndexChanged, this, &TelemetryDetailsPanel::publish);
    form->addRow(tr("Flag"), m_flag);

    layout->addWidget(box);

    // --- Wheels -----------------------------------------------------------
    auto *wheelBox = new QGroupBox(tr("Wheels"), this);
    auto *wheelLayout = new QVBoxLayout(wheelBox);

    m_wheels = new WheelTempDisplay(wheelBox);
    m_wheels->setToolTip(tr("Tyre temperatures across the contact patch — left, centre, right — "
                            "with the brake disc beneath each. Colour runs from blue to red "
                            "around what this compound wants."));
    connect(m_wheels, &WheelTempDisplay::valueChanged, this, &TelemetryDetailsPanel::publish);
    wheelLayout->addWidget(m_wheels);

    layout->addWidget(wheelBox);
    layout->addStretch(1);
}

void TelemetryDetailsPanel::setPopOutVisible(bool visible)
{
    if (m_popOut)
        m_popOut->setVisible(visible);
}

void TelemetryDetailsPanel::refreshFromHub()
{
    const TelemetryHub *hub = TelemetryHub::instance();
    const TelemetryFrame frame = hub->frame();

    // Blocked because this also runs for changes this panel just made, and an
    // echo would fight whatever the user is dragging.
    {
        const QSignalBlocker rpmBlocker(m_rpm);
        const QSignalBlocker tcBlocker(m_tractionControl);
        const QSignalBlocker absBlocker(m_abs);
        const QSignalBlocker pitLimiterBlocker(m_pitLimiter);
        const QSignalBlocker flagBlocker(m_flag);
        const QSignalBlocker wheelBlocker(m_wheels);

        // Scale before value: a car change that raises the limiter would
        // otherwise clamp the new revs against the old, smaller scale.
        m_rpm->setMaximum(frame.rpmMax);
        m_rpm->setValue(frame.rpm);
        m_tractionControl->setChecked(frame.tractionControl);
        m_abs->setChecked(frame.abs);
        m_pitLimiter->setChecked(frame.pitLimiter);
        m_flag->setCurrentIndex(int(frame.flag));
        m_wheels->setWheels(frame.wheels);
    }

    m_rpmValue->setText(tr("%1 rpm").arg(QLocale().toString(frame.rpm)));

    const QSignalBlocker modeBlocker(m_testMode);
    m_testMode->setChecked(hub->isTestMode());
}

void TelemetryDetailsPanel::refreshEditable()
{
    const TelemetryHub *hub = TelemetryHub::instance();
    const bool editable = hub->isTestMode();

    m_rpm->setEditable(editable);
    m_wheels->setEditable(editable);
    setInteractive(m_tractionControl, editable);
    setInteractive(m_abs, editable);
    setInteractive(m_pitLimiter, editable);
    setInteractive(m_flag, editable);

    const QSignalBlocker modeBlocker(m_testMode);
    m_testMode->setChecked(editable);

    if (editable) {
        m_mode->setText(tr("These values are yours to set. Anything reading telemetry treats "
                           "them as live."));
    } else if (hub->isReceiving()) {
        m_mode->setText(tr("Showing live telemetry. Turn on test mode to set the values by "
                           "hand instead."));
    } else {
        m_mode->setText(tr("Waiting for telemetry data."));
    }
}

void TelemetryDetailsPanel::publish()
{
    TelemetryHub *hub = TelemetryHub::instance();
    if (!hub->isTestMode())
        return;

    TelemetryFrame frame = hub->frame();
    frame.rpm = m_rpm->value();
    frame.tractionControl = m_tractionControl->isChecked();
    frame.abs = m_abs->isChecked();
    frame.pitLimiter = m_pitLimiter->isChecked();
    frame.flag = TelemetryFlag(m_flag->currentData().toInt());
    frame.wheels = m_wheels->wheels();
    hub->setFrame(frame);
}

} // namespace hidcenter
