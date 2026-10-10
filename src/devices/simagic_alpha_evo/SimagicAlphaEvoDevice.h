// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include "core/usb/IUsbDevice.h"
#include "core/plugin/IDevice.h"

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoButtonInput;
class AlphaEvoForceTrace;
class AlphaEvoSettings;
class AlphaEvoTelemetry;
class WheelConfigController;
class WheelLedController;

/// One connected SIMAGIC Alpha wheelbase.
class SimagicAlphaEvoDevice : public IDevice
{
    Q_OBJECT

public:
    SimagicAlphaEvoDevice(IUsbDevice *context, QString modelName, QObject *parent = nullptr);

    bool initialize(QString *error) override;
    QVector<DevicePage> pages() override;
    QVector<DeviceInfoField> infoFields() const override;

private:
    IUsbDevice *m_context = nullptr;
    QString m_modelName;

    AlphaEvoSettings *m_settings = nullptr;
    AlphaEvoTelemetry *m_telemetry = nullptr;
    WheelLedController *m_leds = nullptr;
    WheelConfigController *m_wheelConfig = nullptr;
    AlphaEvoForceTrace *m_forceTrace = nullptr;
    AlphaEvoButtonInput *m_buttons = nullptr;

    /// Why probing failed, empty when it succeeded. Kept rather than returned,
    /// because a base that will not talk is still worth showing a page for.
    QString m_probeError;
};

} // namespace hidcenter::simagic_alpha_evo
