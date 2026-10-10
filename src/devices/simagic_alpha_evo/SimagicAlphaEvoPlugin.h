// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

#include "core/plugin/IDevicePlugin.h"

namespace hidcenter::simagic_alpha_evo {

/// The SIMAGIC Alpha Evo family of direct-drive wheelbases.
///
/// Only the Evo: the earlier Alpha, Alpha Mini and Alpha U are a separate family
/// with their own USB ids, and would be their own module beside this one — which
/// is why this module has a namespace of its own.
///
/// Supporting another Evo variant is a one-line addition to the model table in
/// the .cpp — no new module, and no change anywhere in the application.
class SimagicAlphaEvoPlugin : public QObject, public IDevicePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID HIDCENTER_DEVICE_PLUGIN_IID FILE "simagic_alpha_evo.json")
    Q_INTERFACES(hidcenter::IDevicePlugin)

public:
    int abiVersion() const override { return kDevicePluginAbiVersion; }
    QString familyName() const override;
    QVector<UsbDeviceId> supportedIds() const override;
    QString displayNameFor(const UsbDeviceInfo &info) const override;
    IDevice *createDevice(IUsbDevice *context, QObject *parent) override;
};

} // namespace hidcenter::simagic_alpha_evo
