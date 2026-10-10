// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SimagicAlphaEvoPlugin.h"

#include "SimagicAlphaEvoDevice.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

struct Model
{
    UsbDeviceId id;
    const char *name;
};

/// Every wheelbase this module claims. The single source of truth for matching:
/// deliberately not repeated in simagic_alpha_evo.json, where it could drift.
constexpr Model kModels[] = {
    {{0x3670, 0x0500}, "SIMAGIC Alpha Evo"}, // this appears to include both the "EVO Sport" and the "EVO" variants
    {{0x3670, 0x0501}, "SIMAGIC Alpha Evo"},
    {{0x3670, 0x0502}, "SIMAGIC Alpha Evo"},
};

} // namespace

QString SimagicAlphaEvoPlugin::familyName() const
{
    return QStringLiteral("SIMAGIC Alpha Evo");
}

QVector<UsbDeviceId> SimagicAlphaEvoPlugin::supportedIds() const
{
    QVector<UsbDeviceId> ids;
    ids.reserve(int(std::size(kModels)));
    for (const Model &model : kModels)
        ids.append(model.id);
    return ids;
}

QString SimagicAlphaEvoPlugin::displayNameFor(const UsbDeviceInfo &info) const
{
    for (const Model &model : kModels) {
        if (model.id == info.id)
            return QString::fromLatin1(model.name);
    }
    return familyName();
}

IDevice *SimagicAlphaEvoPlugin::createDevice(IUsbDevice *context, QObject *parent)
{
    return new SimagicAlphaEvoDevice(context, displayNameFor(context->info()), parent);
}

} // namespace hidcenter::simagic_alpha_evo
