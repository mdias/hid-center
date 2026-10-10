// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

#include "core/analyzer/UsbPacket.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// What the analyzer's packet log should show.
///
/// Every criterion is optional and they combine with AND. Adding a new one means
/// adding a field here and a clause in matches() — the log model, the proxy and
/// the UI need no structural change.
struct HIDCENTER_CORE_EXPORT PacketFilter
{
    std::optional<quint8> reportId;
    std::optional<TrafficDirection> direction;

    /// Keep only packets whose payload starts with these bytes. Empty disables
    /// the check. The comparison includes the report ID byte, so it matches what
    /// the log shows.
    QByteArray prefix;

    /// Keep only packets from this device node. Empty disables the check.
    QString interfaceId;

    bool isActive() const
    {
        return reportId.has_value() || direction.has_value() || !prefix.isEmpty()
            || !interfaceId.isEmpty();
    }

    bool matches(const UsbPacket &packet) const
    {
        if (reportId.has_value() && packet.reportId != *reportId)
            return false;
        if (direction.has_value() && packet.direction != *direction)
            return false;
        if (!interfaceId.isEmpty() && packet.interfaceId != interfaceId)
            return false;
        if (!prefix.isEmpty() && !packet.data.startsWith(prefix))
            return false;
        return true;
    }
};

} // namespace hidcenter
