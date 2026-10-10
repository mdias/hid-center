// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QPointer>

#include "core/analyzer/IPacketSource.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

class IUsbDevice;

/// Captures traffic by tapping the interface handles an IUsbDevice owns.
///
/// Attaches to every interface already open and, through
/// IUsbDevice::interfaceOpened, to any opened later — so a module that opens
/// its vendor interface lazily still shows up in the log without knowing the
/// analyzer exists.
class HIDCENTER_CORE_EXPORT UsbPacketSource : public IPacketSource
{
    Q_OBJECT

public:
    explicit UsbPacketSource(IUsbDevice *device, QObject *parent = nullptr);

    QString description() const override;

    /// Whether start() opens every interface the device exposes, or taps only
    /// those already open.
    ///
    /// True is right for the analyzer: an interface nothing uses still carries
    /// input reports worth seeing. False is for an observer that must not change
    /// what the application does — opening an interface no one asked for starts
    /// an input-report stream this process then has to drain. With it false,
    /// start() succeeds even with nothing yet open, and interfaces opened later
    /// are picked up as usual.
    void setOpensAllInterfaces(bool opensAll) { m_opensAllInterfaces = opensAll; }
    bool opensAllInterfaces() const { return m_opensAllInterfaces; }

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running; }

    QString lastError() const override { return m_lastError; }

private:
    void attach(IUsbDeviceInterface *interface);
    void emitPacket(IUsbDeviceInterface *interface, TrafficDirection direction, quint8 endpoint,
                    const QByteArray &data);

    QPointer<IUsbDevice> m_device;
    QHash<IUsbDeviceInterface *, QVector<QMetaObject::Connection>> m_connections;
    QString m_lastError;
    bool m_opensAllInterfaces = true;
    bool m_running = false;
};

} // namespace hidcenter
