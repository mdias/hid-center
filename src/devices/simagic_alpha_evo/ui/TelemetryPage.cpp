// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TelemetryPage.h"

#include "AlphaEvoSettings.h"
#include "AlphaEvoTelemetry.h"
#include "core/telemetry/TelemetryHub.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {

TelemetryPage::TelemetryPage(AlphaEvoTelemetry *telemetry, AlphaEvoSettings *settings,
                             QWidget *parent)
    : QWidget(parent)
    , m_telemetry(telemetry)
    , m_settings(settings)
{
    auto *layout = new QVBoxLayout(this);

    m_enabled = new QCheckBox(tr("Enable telemetry"), this);
    m_enabled->setObjectName(QStringLiteral("telemetryToggle"));
    m_enabled->setChecked(m_telemetry->isEnabled());
    m_enabled->setToolTip(tr("Follow the data on the Telemetry page and keep this wheelbase up to "
                             "date."));
    connect(m_enabled, &QCheckBox::toggled, m_telemetry, &AlphaEvoTelemetry::setEnabled);
    layout->addWidget(m_enabled);

    auto *hint = new QLabel(
        tr("Values come from the sidebar's Telemetry page, shared by all devices. Use its "
           "test mode to set them by hand."),
        this);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    layout->addWidget(hint);

    // --- Update rate -------------------------------------------------------
    auto *rateBox = new QGroupBox(tr("Updates"), this);
    auto *rateForm = new QFormLayout(rateBox);

    auto *rateRow = new QWidget(rateBox);
    auto *rateLayout = new QHBoxLayout(rateRow);
    rateLayout->setContentsMargins(0, 0, 0, 0);

    m_rate = new QSlider(Qt::Horizontal, rateRow);
    m_rate->setRange(AlphaEvoTelemetry::kMinimumRateHz, AlphaEvoTelemetry::kMaximumRateHz);
    m_rate->setMinimumWidth(140);
    rateLayout->addWidget(m_rate, 1);

    m_rateSpin = new QSpinBox(rateRow);
    m_rateSpin->setRange(AlphaEvoTelemetry::kMinimumRateHz, AlphaEvoTelemetry::kMaximumRateHz);
    m_rateSpin->setSuffix(tr(" Hz"));
    m_rateSpin->setMinimumWidth(80);
    rateLayout->addWidget(m_rateSpin);

    // The slider is the control; the spin box exists so the exact value is both
    // visible and typeable, which a slider alone is not. Same pairing as the
    // main settings page, minus the commit machinery — nothing here is written
    // to a device.
    connect(m_rate, &QSlider::valueChanged, m_telemetry, &AlphaEvoTelemetry::setRateHz);
    connect(m_rateSpin, &QSpinBox::valueChanged, m_telemetry, &AlphaEvoTelemetry::setRateHz);
    connect(m_telemetry, &AlphaEvoTelemetry::rateChanged, this, [this](int hz) {
        const QSignalBlocker sliderBlocker(m_rate);
        const QSignalBlocker spinBlocker(m_rateSpin);
        m_rate->setValue(hz);
        m_rateSpin->setValue(hz);
        refreshStatus();
    });

    m_rate->setValue(m_telemetry->rateHz());
    m_rateSpin->setValue(m_telemetry->rateHz());

    rateForm->addRow(tr("Frequency"), rateRow);

    m_status = new QLabel(rateBox);
    m_status->setWordWrap(true);
    rateForm->addRow(tr("Status:"), m_status);

    layout->addWidget(rateBox);

    // --- Data --------------------------------------------------------------
    // Everything this module puts on the wire, read-only, field for field. It is
    // here so the conversion can be checked against the source values on the
    // sidebar's Telemetry page, which is the only way to see it go wrong before
    // a device acts on it.
    auto *dataBox = new QGroupBox(tr("Data"), this);
    auto *dataForm = new QFormLayout(dataBox);

    const auto addRow = [this, dataBox, dataForm](const QString &label) {
        auto *value = new QLabel(dataBox);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        dataForm->addRow(label, value);
        return value;
    };

    m_rpm = addRow(tr("Engine speed"));
    m_rpmPercent = addRow(tr("RPM"));
    m_tractionControl = addRow(tr("Traction control"));
    m_abs = addRow(tr("ABS"));
    m_pitLimiter = addRow(tr("Pit limiter"));
    m_flag = addRow(tr("Flag"));

    layout->addWidget(dataBox);
    layout->addStretch(1);

    connect(m_telemetry, &AlphaEvoTelemetry::dataChanged, this, &TelemetryPage::refreshData);
    connect(m_telemetry, &AlphaEvoTelemetry::enabledChanged, this, &TelemetryPage::refreshStatus);

    // The dry-run box is on the FFB Settings tab, but it gates telemetry too,
    // so this page's account of where the reports are going has to follow it.
    if (m_settings)
        connect(m_settings, &AlphaEvoSettings::dryRunChanged, this, &TelemetryPage::refreshStatus);

    // Whether anything is arriving decides whether anything is sent, so the
    // status line has to watch the hub as well as this module.
    TelemetryHub *hub = TelemetryHub::instance();
    connect(hub, &TelemetryHub::receivingChanged, this, &TelemetryPage::refreshStatus);
    connect(hub, &TelemetryHub::testModeChanged, this, &TelemetryPage::refreshStatus);

    refreshData();
    refreshStatus();
}

void TelemetryPage::refreshData()
{
    const SimagicTelemetry &data = m_telemetry->data();

    m_rpm->setText(tr("%1 rpm").arg(QLocale().toString(data.rpm)));
    m_rpmPercent->setText(tr("%1 %").arg(data.rpmPercent));
    m_tractionControl->setText(data.tractionControl ? tr("Active") : tr("Inactive"));
    m_abs->setText(data.abs ? tr("Active") : tr("Inactive"));
    m_pitLimiter->setText(data.pitLimiter ? tr("Active") : tr("Inactive"));
    m_flag->setText(telemetryFlagName(data.flag));
}

void TelemetryPage::refreshStatus()
{
    if (!m_telemetry->isEnabled()) {
        m_status->setText(tr("Off: nothing is tracked or sent."));
        return;
    }

    const int hz = m_telemetry->rateHz();
    const TelemetryHub *hub = TelemetryHub::instance();

    // Four ways to be tracking without the base hearing any of it, and none of
    // them is interchangeable with the others: nothing to say, no device, no
    // device worth talking to, and choosing not to talk.
    if (!hub->isReceiving() && !hub->isTestMode()) {
        m_status->setText(tr("Tracking at %1 Hz, but no telemetry is arriving. Start a game or "
                             "turn on test mode on the Telemetry page.")
                              .arg(hz));
    } else if (!m_settings || !m_settings->isReady()) {
        m_status->setText(tr("Tracking at %1 Hz. Nothing is sent: the base did not answer.")
                              .arg(hz));
    } else if (m_settings->isOffline()) {
        m_status->setText(tr("Tracking at %1 Hz. Nothing is sent: no wheelbase connected.")
                              .arg(hz));
    } else if (m_settings->isDryRun()) {
        m_status->setText(tr("Tracking at %1 Hz. Nothing is sent: dry run is on (see FFB "
                             "Settings).")
                              .arg(hz));
    } else {
        m_status->setText(tr("Sending to the wheelbase at %1 Hz.").arg(hz));
    }
}

} // namespace hidcenter::simagic_alpha_evo
