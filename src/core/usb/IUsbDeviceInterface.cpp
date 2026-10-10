// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/IUsbDeviceInterface.h"

namespace hidcenter {

QString trafficDirectionName(TrafficDirection direction)
{
    switch (direction) {
    case TrafficDirection::In:
        return QStringLiteral("IN");
    case TrafficDirection::Out:
        return QStringLiteral("OUT");
    case TrafficDirection::FeatureGet:
        return QStringLiteral("FEAT GET");
    case TrafficDirection::FeatureSet:
        return QStringLiteral("FEAT SET");
    }
    return QStringLiteral("?");
}

} // namespace hidcenter
