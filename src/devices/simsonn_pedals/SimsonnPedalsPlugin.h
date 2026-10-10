// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

#include "core/plugin/IDevicePlugin.h"

namespace hidcenter::simsonn_pedals {

/// The SIMSONN family of sim-racing pedal sets.
///
/// Adding another SIMSONN model is a one-line addition to the model table in the
/// .cpp — no new module, and no change anywhere in the application.
class SimsonnPedalsPlugin : public QObject, public IDevicePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID HIDCENTER_DEVICE_PLUGIN_IID FILE "simsonn_pedals.json")
    Q_INTERFACES(hidcenter::IDevicePlugin)

public:
    int abiVersion() const override { return kDevicePluginAbiVersion; }
    QString familyName() const override;
    QVector<UsbDeviceId> supportedIds() const override;
    QString displayNameFor(const UsbDeviceInfo &info) const override;
    IDevice *createDevice(IUsbDevice *context, QObject *parent) override;
};

} // namespace hidcenter::simsonn_pedals
