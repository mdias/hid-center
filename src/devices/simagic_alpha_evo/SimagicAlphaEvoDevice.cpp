// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SimagicAlphaEvoDevice.h"

#include "AlphaEvoButtonInput.h"
#include "AlphaEvoForceTrace.h"
#include "AlphaEvoSettings.h"
#include "AlphaEvoTelemetry.h"
#include "WheelConfigController.h"
#include "WheelLedController.h"
#include "core/Logging.h"
#include "ui/MainSettingsPage.h"
#include "ui/StatusPage.h"
#include "ui/TelemetryPage.h"
#include "ui/WheelPage.h"
#include "ui/WheelbaseLedPage.h"

namespace hidcenter::simagic_alpha_evo {

SimagicAlphaEvoDevice::SimagicAlphaEvoDevice(IUsbDevice *context, QString modelName, QObject *parent)
    : IDevice(parent)
    , m_context(context)
    , m_modelName(std::move(modelName))
{
}

bool SimagicAlphaEvoDevice::initialize(QString *error)
{
    Q_UNUSED(error);

    m_settings = new AlphaEvoSettings(m_context, this);

    // Independent of the configuration protocol: telemetry flows the other way
    // and does not need the base to have answered anything. It is given the
    // settings object because that owns the clock its reports go out on — and
    // that clock exists whether or not there is a base behind it.
    m_telemetry = new AlphaEvoTelemetry(m_settings, this);

    // Built here rather than with the page, and for the same reason: the strip
    // should follow the car from the moment the base is detected, whether or not
    // anyone ever opens the tab. It also loads the saved modes, which is what
    // puts the user's own colours on the wheel at start-up rather than the
    // defaults the animation thread began with.
    m_leds = new WheelLedController(m_settings, m_context ? m_context->info() : UsbDeviceInfo{},
                                    this);

    // The steering wheel's own settings, for the same reason and at the same
    // point. It restores the saved brightness and hands it over straight away,
    // before the probe below: the settings object keeps it until the probe has
    // started the I/O thread, and sends it then. The colours wait for the probe
    // to say which wheel is on the base; see below.
    m_wheelConfig = new WheelConfigController(
        m_settings, m_context ? m_context->info() : UsbDeviceInfo{}, this);

    // Also independent of the configuration protocol, and of the probe below: it
    // listens to input reports the kernel is already delivering rather than
    // asking the base for anything. Built here for the ordinary reason — the
    // Status page may be opened, closed and rebuilt, and the source it taps
    // should not be rediscovered every time.
    m_forceTrace = new AlphaEvoForceTrace(m_context, this);

    // The buttons pressed on the wheel, from the same input report, for the
    // same reasons. The Wheel tab switches it on while it is shown.
    m_buttons = new AlphaEvoButtonInput(m_context, this);

    if (!m_settings->probe(&m_probeError)) {
        // Deliberately not a failure. Returning false would replace every tab
        // with an error page, and a base that will not answer is exactly when
        // the Protocol Analyzer is worth reaching.
        qCWarning(hidcenterDevice).noquote()
            << m_modelName << "did not answer the configuration protocol:" << m_probeError;
        Q_EMIT statusMessage(tr("%1: settings unavailable — see the FFB Settings tab.")
                                 .arg(m_modelName));
        return true;
    }

    // The probe read the base status, wheel id and all, without announcing it,
    // and only now is it known whether this is offline. So the wheel is looked
    // at once here, which is what sends a GTC its colours at start-up; later
    // changes arrive with each status refresh.
    m_wheelConfig->refreshAttachedWheel();

    // The firmware version comes from the same status report as the settings, so
    // it changes whenever that is re-read — after a firmware update, say.
    connect(m_settings, &AlphaEvoSettings::statusRefreshed, this,
            &SimagicAlphaEvoDevice::infoFieldsChanged);

    Q_EMIT statusMessage(tr("%1: firmware %2, on %3.")
                             .arg(m_modelName)
                             .arg(m_settings->firmwareVersion())
                             .arg(m_settings->activeInterface()
                                      ? m_settings->activeInterface()->displayName()
                                      : tr("no interface")));
    return true;
}

QVector<DeviceInfoField> SimagicAlphaEvoDevice::infoFields() const
{
    // Nothing offline: the firmware version would be read out of a status report
    // no base ever sent, and "Firmware 0" reads as a fact rather than a blank.
    if (!m_settings || !m_settings->isReady() || m_settings->isOffline())
        return {};

    return {{tr("Firmware"), QString::number(m_settings->firmwareVersion()),
             tr("Raw firmware field: %1 (0x%2)")
                 .arg(m_settings->firmwareRaw())
                 .arg(m_settings->firmwareRaw(), 4, 16, QLatin1Char('0'))}};
}

QVector<DevicePage> SimagicAlphaEvoDevice::pages()
{
    // Status first, and so selected by default: what is true about the base
    // before what can be changed on it. It is also the tab that still says
    // something useful when the base did not answer at all.
    return {{tr("Status"), new StatusPage(m_settings, m_forceTrace, m_probeError)},
            {tr("FFB Settings"), new MainSettingsPage(m_modelName, m_settings, m_probeError)},
            {tr("Telemetry"), new TelemetryPage(m_telemetry, m_settings)},
            {tr("Wheelbase LED"), new WheelbaseLedPage(m_leds, m_settings, m_probeError)},
            {tr("Wheel"), new WheelPage(m_wheelConfig, m_buttons, m_settings, m_probeError)}};
}

} // namespace hidcenter::simagic_alpha_evo
