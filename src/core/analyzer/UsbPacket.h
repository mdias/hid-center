// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include "core/usb/IUsbDeviceInterface.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// One captured transfer.
struct HIDCENTER_CORE_EXPORT UsbPacket
{
    qint64 timestampUs = 0;   ///< Wall clock, microseconds since the epoch.
    TrafficDirection direction = TrafficDirection::In;
    /// Which interface the transfer went through, and what to call it.
    ///
    /// Two fields because the two jobs disagree. `interfaceId` is the sysfs path,
    /// which every interface has and no two share — including the ones with no
    /// device node to be named after, which is what filtering has to match on.
    /// `interfaceName` is for reading.
    QString interfaceId;
    QString interfaceName;

    /// bEndpointAddress the transfer used, direction bit included, or 0 when it
    /// went through the control pipe — which every feature report does.
    quint8 endpoint = 0;

    quint8 reportId = 0;      ///< 0 when there is no report layer, or the descriptor declares no IDs.
    QByteArray data;          ///< Full transfer, report ID byte included.

    /// "01 2f ff 00" — the payload as spaced lowercase hex.
    QString toHex() const { return QString::fromLatin1(data.toHex(' ')); }
};

/// Read back what toHex() writes: whitespace-separated hex, as the analyzer's
/// Data column prints it. Empty text gives an empty array and no error, leaving
/// the caller to decide whether empty means anything.
///
/// Tokens may carry a `0x` prefix. One digit is one byte, so "f" is 0x0f; an
/// even number of digits is that many half-bytes, so a pasted "80013f" reads the
/// same as "80 01 3f". Anything else fails, with `error` naming the token.
///
/// Deliberately not QByteArray::fromHex(), which drops every character it does
/// not recognise: "zz" comes back empty rather than refused, and there is
/// nothing left to tell the user what they mistyped.
HIDCENTER_CORE_EXPORT QByteArray parseHexBytes(const QString &text, QString *error = nullptr);

} // namespace hidcenter

Q_DECLARE_METATYPE(hidcenter::UsbPacket)
