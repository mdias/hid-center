// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/UsbDeviceBase.h"

#include "core/Logging.h"

namespace hidcenter {

UsbDeviceBase::UsbDeviceBase(const UsbDeviceInfo &info, QObject *parent)
    : IUsbDevice(parent)
    , m_info(info)
{
}

UsbDeviceBase::~UsbDeviceBase() = default;

QString UsbDeviceBase::keyFor(const UsbInterfaceInfo &interfaceInfo)
{
    // Falls back to the node only for a transport that reports no sysfs path at
    // all, which no current backend does; one of the two is always there.
    return interfaceInfo.sysPath.isEmpty() ? interfaceInfo.hid.devicePath : interfaceInfo.sysPath;
}

IUsbDeviceInterface *UsbDeviceBase::openInterface(const UsbInterfaceInfo &interfaceInfo)
{
    return openHandle(interfaceInfo, /*permanent=*/true);
}

IUsbDeviceInterface *UsbDeviceBase::openHandle(const UsbInterfaceInfo &interfaceInfo,
                                               bool permanent)
{
    const QString key = keyFor(interfaceInfo);

    if (permanent)
        m_permanent.insert(key);

    if (IUsbDeviceInterface *existing = m_open.value(key))
        return existing;

    QString error;
    std::unique_ptr<IUsbDeviceInterface> handle = createInterface(interfaceInfo, &error);
    if (!handle) {
        m_lastError = error;
        qCWarning(hidcenterHid) << "Failed to open" << interfaceInfo.displayName() << ":" << error;
        return nullptr;
    }

    // Hand the handle to Qt's ownership: it becomes a child of this device and
    // dies with it, which is exactly the lifetime we want.
    IUsbDeviceInterface *raw = handle.release();
    raw->setParent(this);
    connect(raw, &IUsbDeviceInterface::disconnected, this, &IUsbDevice::deviceLost);

    m_open.insert(key, raw);
    m_openOrder.append(raw);

    qCDebug(hidcenterHid) << "Opened" << interfaceInfo.displayName() << "on" << m_info.devicePath;
    Q_EMIT interfaceOpened(raw);
    return raw;
}

IUsbDeviceInterface *UsbDeviceBase::openInterfaceByNumber(int interfaceNumber)
{
    if (const UsbInterfaceInfo *iface = m_info.interfaceByNumber(interfaceNumber))
        return openInterface(*iface);

    m_lastError = QStringLiteral("Device %1 has no interface %2")
                      .arg(m_info.id.toString())
                      .arg(interfaceNumber);
    return nullptr;
}

IUsbDeviceHidInterface *UsbDeviceBase::openInterfaceByUsage(quint16 usagePage, quint16 usage)
{
    if (const UsbInterfaceInfo *iface = m_info.interfaceByUsage(usagePage, usage))
        return openHidInterface(*iface);

    m_lastError = QStringLiteral("Device %1 has no interface with usage %2:%3")
                      .arg(m_info.id.toString())
                      .arg(usagePage, 4, 16, QLatin1Char('0'))
                      .arg(usage, 4, 16, QLatin1Char('0'));
    return nullptr;
}

IUsbDeviceHidInterface *UsbDeviceBase::openHidInterface(const UsbInterfaceInfo &interfaceInfo)
{
    if (!interfaceInfo.isHid()) {
        m_lastError = QStringLiteral("%1 has no HID device node").arg(interfaceInfo.displayName());
        return nullptr;
    }

    // A HID interface can only ever be opened as a HID handle, so this cast is
    // about the static type rather than a question about the object.
    return qobject_cast<IUsbDeviceHidInterface *>(openInterface(interfaceInfo));
}

IUsbDeviceInterface *UsbDeviceBase::openInterfaceForEndpoint(quint8 endpoint)
{
    if (const UsbInterfaceInfo *iface = m_info.interfaceForEndpoint(endpoint))
        return openInterface(*iface);

    m_lastError = QStringLiteral("Device %1 declares no endpoint 0x%2")
                      .arg(m_info.id.toString())
                      .arg(endpoint, 2, 16, QLatin1Char('0'));
    return nullptr;
}

QVector<IUsbDeviceInterface *> UsbDeviceBase::openAllInterfaces()
{
    for (const UsbInterfaceInfo &iface : m_info.interfaces) {
        // HID interfaces only, and the reason is not that the others are
        // uninteresting. This call exists so the analyzer can watch traffic it
        // did not cause, and an interface with no report layer produces none:
        // nothing arrives on it unless something asks, so there would be nothing
        // to watch. Opening one is also not free the way opening a node is — it
        // claims the interface — and doing that to every interface of a device
        // because someone opened a tab is not a trade to make silently.
        //
        // An interface a module wants is opened by name, through
        // openInterfaceForEndpoint().
        if (!iface.isHid())
            continue;

        // A failure here is expected and not fatal: some interfaces are claimed
        // by kernel drivers or restricted by permissions. Open what we can.
        openInterface(iface);
    }
    return m_openOrder;
}

// --- Shared claims ----------------------------------------------------------

IUsbDeviceInterface *UsbDeviceBase::openFor(QObject *owner, const UsbInterfaceInfo &interfaceInfo)
{
    // No owner means nobody to release it, so treat it as the permanent open it
    // effectively is rather than recording a claim that can never be given back.
    if (!owner)
        return openInterface(interfaceInfo);

    IUsbDeviceInterface *handle = openHandle(interfaceInfo, /*permanent=*/false);
    if (!handle)
        return nullptr;

    // A set, so claiming twice from one owner is claiming once — a caller need
    // not remember whether it already holds this.
    m_claims[keyFor(interfaceInfo)].insert(owner);

    if (!m_ownerWatch.contains(owner)) {
        m_ownerWatch.insert(owner, connect(owner, &QObject::destroyed, this, [this, owner] {
            m_ownerWatch.remove(owner);
            releaseAllFor(owner);
        }));
    }

    return handle;
}

IUsbDeviceHidInterface *UsbDeviceBase::openHidFor(QObject *owner,
                                                  const UsbInterfaceInfo &interfaceInfo)
{
    if (!interfaceInfo.isHid()) {
        m_lastError = QStringLiteral("%1 has no HID device node").arg(interfaceInfo.displayName());
        return nullptr;
    }
    return qobject_cast<IUsbDeviceHidInterface *>(openFor(owner, interfaceInfo));
}

QVector<IUsbDeviceInterface *> UsbDeviceBase::openAllInterfacesFor(QObject *owner)
{
    for (const UsbInterfaceInfo &iface : m_info.interfaces) {
        // Both rules from openAllInterfaces(): HID interfaces only, and a failure
        // on one of those is expected rather than fatal.
        if (!iface.isHid())
            continue;
        openFor(owner, iface);
    }
    return m_openOrder;
}

void UsbDeviceBase::releaseFor(QObject *owner, const UsbInterfaceInfo &interfaceInfo)
{
    const QString key = keyFor(interfaceInfo);

    const auto it = m_claims.find(key);
    if (it == m_claims.end())
        return;

    it->remove(owner);
    if (it->isEmpty())
        m_claims.erase(it);

    closeIfUnclaimed(key);
}

void UsbDeviceBase::releaseAllFor(QObject *owner)
{
    // Collected first: closeIfUnclaimed() reaches back into m_claims, and the
    // keys are needed after the entries they came from may be gone.
    QStringList touched;
    for (auto it = m_claims.begin(); it != m_claims.end();) {
        if (it->remove(owner))
            touched << it.key();

        if (it->isEmpty())
            it = m_claims.erase(it);
        else
            ++it;
    }

    for (const QString &key : touched)
        closeIfUnclaimed(key);
}

void UsbDeviceBase::closeIfUnclaimed(const QString &key)
{
    // A permanent claim outranks every temporary one. This is what keeps the
    // interface a module opened for its own protocol from being closed by
    // something else letting go of it — that module holds it as a bare pointer
    // and may be using it from a worker thread.
    if (m_permanent.contains(key) || m_claims.contains(key))
        return;

    IUsbDeviceInterface *handle = m_open.take(key);
    if (!handle)
        return;

    m_openOrder.removeOne(handle);

    // Out of the maps before it is destroyed, so openInterfaces() cannot hand
    // out a handle on its way to the grave. Deleted later rather than now
    // because a release can come from inside one of this device's own signals —
    // an unplug releasing its claim from a disconnected() handler is exactly
    // that — and deleting an object part-way through emitting is a crash.
    handle->setParent(nullptr);
    handle->deleteLater();

    qCDebug(hidcenterHid) << "Closed" << key << "— nothing holds it any more";
}

// --- Offline ----------------------------------------------------------------

std::unique_ptr<IUsbDeviceInterface> OfflineUsbDevice::createInterface(
    const UsbInterfaceInfo &interfaceInfo, QString *error)
{
    Q_UNUSED(interfaceInfo);
    if (error)
        *error = QStringLiteral("There is no device behind this page");
    return {};
}

} // namespace hidcenter
