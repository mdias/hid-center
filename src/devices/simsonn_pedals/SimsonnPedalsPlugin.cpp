// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SimsonnPedalsPlugin.h"

#include "SimsonnPedalsDevice.h"

namespace hidcenter::simsonn_pedals {
namespace {

struct Model
{
    UsbDeviceId id;
    const char *name;
};

/// Every pedal set this module claims. The single source of truth for matching:
/// deliberately not repeated in simsonn_pedals.json, where it could drift.
constexpr Model kModels[] = {
    {{0xddfd, 0x6011}, "SIMSONN Plus X"},
};

} // namespace

QString SimsonnPedalsPlugin::familyName() const
{
    return QStringLiteral("SIMSONN");
}

QVector<UsbDeviceId> SimsonnPedalsPlugin::supportedIds() const
{
    QVector<UsbDeviceId> ids;
    ids.reserve(int(std::size(kModels)));
    for (const Model &model : kModels)
        ids.append(model.id);
    return ids;
}

QString SimsonnPedalsPlugin::displayNameFor(const UsbDeviceInfo &info) const
{
    for (const Model &model : kModels) {
        if (model.id == info.id)
            return QString::fromLatin1(model.name);
    }
    return familyName();
}

IDevice *SimsonnPedalsPlugin::createDevice(IUsbDevice *context, QObject *parent)
{
    return new SimsonnPedalsDevice(context, displayNameFor(context->info()), parent);
}

} // namespace hidcenter::simsonn_pedals
