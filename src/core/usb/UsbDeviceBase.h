// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <memory>

#include "core/usb/IUsbDevice.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// The half of IUsbDevice that has nothing to do with any operating system.
///
/// Claims, the permanent-claim rule, the owner watch and closing an interface
/// nothing holds any more are all bookkeeping over handles, so they live here
/// once rather than in every backend. What a backend supplies is
/// createInterface(): one open handle, or nullptr and a reason.
class HIDCENTER_CORE_EXPORT UsbDeviceBase : public IUsbDevice
{
    Q_OBJECT

public:
    explicit UsbDeviceBase(const UsbDeviceInfo &info, QObject *parent = nullptr);
    ~UsbDeviceBase() override;

    const UsbDeviceInfo &info() const override { return m_info; }

    IUsbDeviceInterface *openInterface(const UsbInterfaceInfo &interfaceInfo) override;
    IUsbDeviceInterface *openInterfaceByNumber(int interfaceNumber) override;
    IUsbDeviceHidInterface *openInterfaceByUsage(quint16 usagePage, quint16 usage) override;
    IUsbDeviceHidInterface *openHidInterface(const UsbInterfaceInfo &interfaceInfo) override;
    IUsbDeviceInterface *openInterfaceForEndpoint(quint8 endpoint) override;
    QVector<IUsbDeviceInterface *> openAllInterfaces() override;

    IUsbDeviceInterface *openFor(QObject *owner, const UsbInterfaceInfo &interfaceInfo) override;
    IUsbDeviceHidInterface *openHidFor(QObject *owner,
                                       const UsbInterfaceInfo &interfaceInfo) override;
    QVector<IUsbDeviceInterface *> openAllInterfacesFor(QObject *owner) override;
    void releaseFor(QObject *owner, const UsbInterfaceInfo &interfaceInfo) override;
    void releaseAllFor(QObject *owner) override;

    QVector<IUsbDeviceInterface *> openInterfaces() const override { return m_openOrder; }

    QString lastError() const override { return m_lastError; }

protected:
    /// Open one interface, or return nullptr with `error` set. The one thing a
    /// backend has to implement; everything else in this class is bookkeeping
    /// over what this returns.
    virtual std::unique_ptr<IUsbDeviceInterface> createInterface(
        const UsbInterfaceInfo &interfaceInfo, QString *error) = 0;

    void setLastError(const QString &error) { m_lastError = error; }

private:
    /// Open the interface if it is not open already. `permanent` marks it as one
    /// no release may ever close.
    IUsbDeviceInterface *openHandle(const UsbInterfaceInfo &interfaceInfo, bool permanent);

    /// Close the interface if nothing holds it any more. Does nothing to one with
    /// a permanent claim.
    void closeIfUnclaimed(const QString &key);

    /// How an interface is keyed in the maps below.
    ///
    /// The sysfs path, not the device node: an interface with no hidraw node has
    /// no device node to be keyed by, and two of them would collide on an empty
    /// string. Every interface has a sysPath, and it is unique per interface.
    static QString keyFor(const UsbInterfaceInfo &interfaceInfo);

    UsbDeviceInfo m_info;
    QString m_lastError;

    // The handles are QObject children of this device, so they are destroyed
    // with it.
    QHash<QString, IUsbDeviceInterface *> m_open;
    QVector<IUsbDeviceInterface *> m_openOrder;

    /// Interfaces a plain openInterface() asked for. Nothing closes these.
    QSet<QString> m_permanent;

    /// Who is holding each interface open, for those held temporarily.
    QHash<QString, QSet<QObject *>> m_claims;

    /// One per owner, so an owner that is destroyed without releasing does not
    /// hold an interface open for the rest of the session.
    QHash<QObject *, QMetaObject::Connection> m_ownerWatch;
};

/// An IUsbDevice with nothing behind it.
///
/// What backs the sidebar's entry for a device a module supports and which is not
/// plugged in: the pages build, the controls work against an in-memory state, and
/// every attempt to open an interface is refused rather than crashing into a
/// backend that is not there.
class HIDCENTER_CORE_EXPORT OfflineUsbDevice final : public UsbDeviceBase
{
    Q_OBJECT

public:
    explicit OfflineUsbDevice(const UsbDeviceInfo &info, QObject *parent = nullptr)
        : UsbDeviceBase(info, parent)
    {
    }

    bool isOffline() const override { return true; }

protected:
    std::unique_ptr<IUsbDeviceInterface> createInterface(const UsbInterfaceInfo &interfaceInfo,
                                                         QString *error) override;
};

} // namespace hidcenter
