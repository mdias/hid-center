// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/analyzer/UsbPacketSource.h"

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"

#include <QDateTime>

namespace hidcenter {

UsbPacketSource::UsbPacketSource(IUsbDevice *device, QObject *parent)
    : IPacketSource(parent)
    , m_device(device)
{
}

QString UsbPacketSource::description() const
{
    if (!m_device)
        return QStringLiteral("usb (device gone)");
    return QStringLiteral("usb: %1 interface(s)").arg(m_device->info().interfaces.size());
}

bool UsbPacketSource::start()
{
    if (m_running)
        return true;

    if (!m_device) {
        m_lastError = QStringLiteral("The device is no longer available");
        return false;
    }

    m_running = true;

    // Open everything, not just what the device module uses: an interface we
    // never write to still carries input reports worth seeing. An observe-only
    // source takes what is already there instead.
    //
    // Claimed rather than opened outright, so stopping really does close what
    // starting opened. Anything else holding one of these interfaces keeps it,
    // and an interface a module opened for its own protocol is untouched — see
    // IUsbDevice::openFor().
    const QVector<IUsbDeviceInterface *> interfaces =
        m_opensAllInterfaces ? m_device->openAllInterfacesFor(this) : m_device->openInterfaces();
    for (IUsbDeviceInterface *interface : interfaces)
        attach(interface);

    // Interfaces opened after this point are picked up automatically.
    connect(m_device, &IUsbDevice::interfaceOpened, this, &UsbPacketSource::attach,
            Qt::UniqueConnection);

    // Only a failure when we asked for the interfaces ourselves. Observing
    // nothing yet is a legitimate state: the module may not have opened its
    // interface, and interfaceOpened will bring us what it does open.
    if (m_opensAllInterfaces && m_connections.isEmpty()) {
        m_running = false;
        disconnect(m_device, &IUsbDevice::interfaceOpened, this, nullptr);
        m_lastError = m_device->lastError().isEmpty()
            ? QStringLiteral("No interface of this device could be opened")
            : m_device->lastError();
        qCWarning(hidcenterAnalyzer) << "Capture could not start:" << m_lastError;
        return false;
    }

    m_lastError.clear();
    qCDebug(hidcenterAnalyzer) << "Capturing on" << m_connections.size() << "interface(s)";
    return true;
}

void UsbPacketSource::stop()
{
    if (!m_running)
        return;

    for (auto it = m_connections.constBegin(); it != m_connections.constEnd(); ++it) {
        for (const QMetaObject::Connection &connection : it.value())
            QObject::disconnect(connection);
    }
    m_connections.clear();

    if (m_device) {
        disconnect(m_device, &IUsbDevice::interfaceOpened, this, nullptr);

        // Give back what start() claimed. Harmless for an observe-only source,
        // which claims nothing because it opens nothing.
        m_device->releaseAllFor(this);
    }

    m_running = false;
    qCDebug(hidcenterAnalyzer, "Capture stopped");
}

void UsbPacketSource::attach(IUsbDeviceInterface *interface)
{
    if (!m_running || !interface || m_connections.contains(interface))
        return;

    QVector<QMetaObject::Connection> connections;

    // One tap, not one per kind of transfer. transferred() is emitted by every
    // route a handle has — input reports, output reports, feature reports and
    // raw endpoint writes — and it is the only one an interface with no report
    // layer emits at all, which is the whole reason the analyzer can see traffic
    // on an interface the HID stack never took.
    connections << connect(interface, &IUsbDeviceInterface::transferred, this,
                           [this, interface](TrafficDirection direction, quint8 endpoint,
                                             const QByteArray &data) {
                               emitPacket(interface, direction, endpoint, data);
                           });

    // Drop the taps when the interface disappears, so a stale
    // IUsbDeviceInterface* is never used as a hash key after the handle is
    // destroyed.
    connections << connect(interface, &QObject::destroyed, this, [this, interface] {
        m_connections.remove(interface);
    });

    m_connections.insert(interface, connections);
}

void UsbPacketSource::emitPacket(IUsbDeviceInterface *interface, TrafficDirection direction,
                                 quint8 endpoint, const QByteArray &data)
{
    UsbPacket packet;
    packet.timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    packet.direction = direction;
    packet.endpoint = endpoint;
    packet.interfaceId = interface->interfaceInfo().sysPath;
    packet.interfaceName = interface->interfaceInfo().displayName();
    packet.data = data;

    // Only a HID handle has a descriptor to ask, and without report IDs in it
    // byte 0 is payload rather than an ID. A raw endpoint transfer has neither,
    // and reports 0.
    if (auto *hid = qobject_cast<IUsbDeviceHidInterface *>(interface)) {
        packet.reportId =
            (hid->descriptor().usesReportIds() && !data.isEmpty()) ? quint8(data.at(0)) : 0;
    }

    Q_EMIT packetCaptured(packet);
}

} // namespace hidcenter
