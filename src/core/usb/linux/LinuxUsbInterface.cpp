// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/linux/LinuxUsbInterface.h"

#include "core/Logging.h"

#include <QHash>
#include <QMutexLocker>

#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace hidcenter {
namespace {

QString errnoString(int err)
{
    char buffer[256] = {};
    // The GNU strerror_r may return a pointer to its own static string rather
    // than filling the buffer, so use whatever it hands back.
    const char *message = strerror_r(err, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(message);
}

QString endpointName(quint8 endpoint)
{
    return QStringLiteral("0x%1").arg(endpoint, 2, 16, QLatin1Char('0'));
}

/// What a claim changes about an interface, as it stood before the claim.
struct PreClaimState
{
    QString kernelDriver;
    HidDriverInfo driver;
};

/// Every interface a LinuxUsbInterface in this process holds.
///
/// Process-wide because the reader is enumeration, which knows nothing of which
/// handles are open, and locked because a handle can be closed from the I/O
/// thread that drives it while the backend enumerates on the main one.
struct ClaimRegistry
{
    QMutex mutex;
    QHash<QString, PreClaimState> interfaces;
};

ClaimRegistry &claimRegistry()
{
    static ClaimRegistry registry;
    return registry;
}

/// Keyed on the usbfs node rather than the sysfs path: the node carries the
/// device number, which a replug changes, so a claim not yet released on a
/// device that went away can never be mistaken for one on its replacement.
QString claimKey(const QString &nodePath, int interfaceNumber)
{
    return nodePath + QLatin1Char('#') + QString::number(interfaceNumber);
}

} // namespace

LinuxUsbInterface::LinuxUsbInterface(const UsbInterfaceInfo &interfaceInfo, int busNumber,
                                     int deviceNumber, QObject *parent)
    : IUsbDeviceInterface(parent)
    , m_interface(interfaceInfo)
{
    if (busNumber <= 0 || deviceNumber <= 0) {
        setError(QStringLiteral("%1 is not on a bus this backend can address directly")
                     .arg(m_interface.displayName()));
        return;
    }

    if (m_interface.interfaceNumber < 0) {
        setError(QStringLiteral("%1 has no interface number to claim")
                     .arg(m_interface.displayName()));
        return;
    }

    m_nodePath = nodePathFor(busNumber, deviceNumber);

    // Blocking, unlike the hidraw handle next door: there is no notifier here to
    // drive, and every transfer through this class is a synchronous ioctl that
    // carries its own timeout.
    m_fd = ::open(m_nodePath.toLocal8Bit().constData(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        const int err = errno;
        QString message =
            QStringLiteral("Cannot open %1: %2").arg(m_nodePath, errnoString(err));
        if (err == EACCES || err == EPERM) {
            message += QStringLiteral(
                " — install udev/70-hid-center.rules and replug the device to grant access.");
        }
        setError(message);
        qCWarning(hidcenterHid) << message;
        return;
    }

    if (!claim()) {
        closeDevice();
        return;
    }

    qCDebug(hidcenterHid) << "Claimed interface" << m_interface.interfaceNumber << "on" << m_nodePath
                       << "with" << m_interface.endpoints.size() << "endpoint(s)";
}

LinuxUsbInterface::~LinuxUsbInterface()
{
    closeDevice();
}

QString LinuxUsbInterface::nodePathFor(int busNumber, int deviceNumber)
{
    if (busNumber <= 0 || deviceNumber <= 0)
        return {};

    return QStringLiteral("/dev/bus/usb/%1/%2")
        .arg(busNumber, 3, 10, QLatin1Char('0'))
        .arg(deviceNumber, 3, 10, QLatin1Char('0'));
}

void LinuxUsbInterface::maskOwnClaim(const QString &nodePath, UsbInterfaceInfo &iface)
{
    if (nodePath.isEmpty() || iface.interfaceNumber < 0)
        return;

    ClaimRegistry &registry = claimRegistry();
    const QMutexLocker locker(&registry.mutex);
    const auto it = registry.interfaces.constFind(claimKey(nodePath, iface.interfaceNumber));
    if (it == registry.interfaces.constEnd())
        return;

    // Only these two. Nothing else a claim can reach changes: the endpoints
    // belong to the interface whoever holds it, no hidraw node existed to lose —
    // an interface with one is never opened here — and access is the usbfs
    // node's, which a claim does not touch.
    iface.kernelDriver = it->kernelDriver;
    iface.driver = it->driver;
}

bool LinuxUsbInterface::claim()
{
    // The rule that keeps this from breaking the rest of the machine.
    //
    // Claiming an interface means taking it off whatever kernel driver holds it,
    // and most drivers are holding it for a reason — uvcvideo is running a
    // camera, snd-usb-audio is running a sound card, simagic-ff is giving a
    // wheel its force feedback. None of that may be pulled out from under the
    // user because an application wanted to address an endpoint.
    //
    // usbhid with nothing below it is the one exception, and it is the case this
    // class exists for: usbhid took the interface, no HID driver bound under it,
    // so no node was created and nothing anywhere is using it. Detaching that
    // takes nothing away from anyone, and it is undone on close.
    if (!m_interface.kernelDriver.isEmpty() && !detachable()) {
        setError(QStringLiteral("%1 is bound to %2, which is using it; its endpoints cannot be "
                                "addressed directly without taking the interface away")
                     .arg(m_interface.displayName(), m_interface.kernelDriver));
        return false;
    }

    // Registered before the detach rather than after the claim: udev reports
    // the unbind the moment it happens, and a rescan that lands between the two
    // must already see the interface as ours. See maskOwnClaim().
    bool alreadyHeld = false;
    {
        ClaimRegistry &registry = claimRegistry();
        const QMutexLocker locker(&registry.mutex);
        const QString key = claimKey(m_nodePath, m_interface.interfaceNumber);
        alreadyHeld = registry.interfaces.contains(key);
        if (!alreadyHeld) {
            registry.interfaces.insert(key, {m_interface.kernelDriver, m_interface.driver});
            m_registered = true;
        }
    }
    // Refused here rather than left to the kernel, which would refuse it too but
    // with an EINVAL from USBDEVFS_DISCONNECT that explains nothing.
    if (alreadyHeld) {
        setError(QStringLiteral("%1 is already open elsewhere in this application")
                     .arg(m_interface.displayName()));
        return false;
    }

    // Only when something actually holds it. USBDEVFS_DISCONNECT on a free
    // interface returns ENODATA, which is not a failure but is noise, and the
    // reattach on close would then hand the interface to a driver that never had
    // it.
    if (!m_interface.kernelDriver.isEmpty()) {
        usbdevfs_ioctl command = {};
        command.ifno = m_interface.interfaceNumber;
        command.ioctl_code = USBDEVFS_DISCONNECT;
        command.data = nullptr;

        if (::ioctl(m_fd, USBDEVFS_IOCTL, &command) < 0 && errno != ENODATA) {
            fail(QStringLiteral("USBDEVFS_DISCONNECT"));
            return false;
        }

        m_detached = true;
        qCInfo(hidcenterHid).noquote()
            << QStringLiteral("Detached %1 from interface %2 of %3 so its endpoints can be "
                              "addressed directly; it is reattached when the device is closed.")
                   .arg(m_interface.kernelDriver)
                   .arg(m_interface.interfaceNumber)
                   .arg(m_nodePath);
    }

    unsigned int number = static_cast<unsigned int>(m_interface.interfaceNumber);
    if (::ioctl(m_fd, USBDEVFS_CLAIMINTERFACE, &number) < 0) {
        fail(QStringLiteral("USBDEVFS_CLAIMINTERFACE"));
        release();  // Undo the detach, so a failure here leaves nothing changed.
        return false;
    }

    m_claimed = true;
    return true;
}

void LinuxUsbInterface::release()
{
    if (m_fd < 0)
        return;

    if (m_claimed) {
        unsigned int number = static_cast<unsigned int>(m_interface.interfaceNumber);
        if (::ioctl(m_fd, USBDEVFS_RELEASEINTERFACE, &number) < 0)
            qCDebug(hidcenterHid) << "USBDEVFS_RELEASEINTERFACE:" << errnoString(errno);
        m_claimed = false;
    }

    if (m_detached) {
        // Best effort, and quiet about it: the ordinary reason this fails is that
        // the device has been unplugged, which is the same reason we are closing.
        usbdevfs_ioctl command = {};
        command.ifno = m_interface.interfaceNumber;
        command.ioctl_code = USBDEVFS_CONNECT;
        command.data = nullptr;
        if (::ioctl(m_fd, USBDEVFS_IOCTL, &command) < 0)
            qCDebug(hidcenterHid) << "USBDEVFS_CONNECT:" << errnoString(errno);
        m_detached = false;
    }

    // After the reattach, not before, for the same reason the registration
    // comes before the detach.
    if (m_registered) {
        ClaimRegistry &registry = claimRegistry();
        const QMutexLocker locker(&registry.mutex);
        registry.interfaces.remove(claimKey(m_nodePath, m_interface.interfaceNumber));
        m_registered = false;
    }
}

bool LinuxUsbInterface::detachable() const
{
    // "usbhid holds it and produced nothing." The node is the evidence: usbhid
    // creates one as soon as any HID driver binds below it, so its absence means
    // nothing bound, which means nothing is reading or writing this interface.
    return m_interface.kernelDriver == QLatin1String("usbhid") && !m_interface.isHid();
}

const UsbEndpointInfo *LinuxUsbInterface::checkEndpoint(quint8 endpoint, bool wantOut)
{
    const UsbEndpointInfo *found = m_interface.endpointByAddress(endpoint);
    if (!found) {
        setError(QStringLiteral("%1 declares no endpoint %2")
                     .arg(m_interface.displayName(), endpointName(endpoint)));
        return nullptr;
    }

    if (found->isOut() != wantOut) {
        setError(QStringLiteral("Endpoint %1 points %2")
                     .arg(endpointName(endpoint),
                          found->isIn() ? QStringLiteral("in, not out")
                                        : QStringLiteral("out, not in")));
        return nullptr;
    }

    // Refused rather than sent anyway. USBDEVFS_BULK would carry it perfectly
    // well on a bulk endpoint, but a caller asking for an interrupt transfer and
    // silently getting a bulk one is a protocol bug that would surface far from
    // here.
    if (found->type != UsbTransferType::Interrupt) {
        setError(QStringLiteral("Endpoint %1 is %2, not interrupt")
                     .arg(endpointName(endpoint), usbTransferTypeName(found->type)));
        return nullptr;
    }

    return found;
}

bool LinuxUsbInterface::writeInterrupt(quint8 endpoint, const QByteArray &data, int timeoutMs)
{
    if (data.isEmpty()) {
        setError(QStringLiteral("Refusing to send an empty transfer"));
        return false;
    }

    const UsbEndpointInfo *target = checkEndpoint(endpoint, /*wantOut=*/true);
    if (!target)
        return false;

    // One packet, not a stream. Every protocol reaching this class sends whole
    // frames sized to the endpoint, and a payload that would have to be split
    // is a composer bug rather than something to paper over here.
    if (data.size() > target->maxPacketSize) {
        setError(QStringLiteral("%1 bytes will not fit endpoint %2, which takes %3")
                     .arg(data.size())
                     .arg(endpointName(endpoint))
                     .arg(target->maxPacketSize));
        return false;
    }

    // usbfs writes through this pointer for an IN transfer and only reads it for
    // an OUT one, but the struct field is not const either way, so the buffer
    // has to be one we own.
    QByteArray buffer = data;

    usbdevfs_bulktransfer transfer = {};
    transfer.ep = endpoint;
    transfer.len = static_cast<unsigned int>(buffer.size());
    transfer.timeout = static_cast<unsigned int>(timeoutMs < 0 ? 0 : timeoutMs);
    transfer.data = buffer.data();

    int written = 0;
    int lastErrno = 0;
    QString failure;
    {
        // The descriptor is checked inside the lock, so a close that was waiting
        // on it cannot slip in between the check and the transfer.
        const QMutexLocker locker(&m_ioMutex);
        if (m_fd < 0) {
            setError(QStringLiteral("Interface is not open"));
            return false;
        }
        // USBDEVFS_BULK despite the endpoint being interrupt: usb_bulk_msg()
        // reads the endpoint descriptor and submits an interrupt URB for an
        // interrupt endpoint. The ioctl name is about the API, not the wire.
        written = ::ioctl(m_fd, USBDEVFS_BULK, &transfer);
        if (written < 0) {
            lastErrno = errno;
            failure = fail(QStringLiteral("USBDEVFS_BULK"));
        }
    }

    // Announced outside the lock: a slot reached by a direct connection is free
    // to turn round and issue another transfer, which would deadlock in here.
    if (!failure.isEmpty()) {
        // ENODEV is the device being unplugged, not a transfer going wrong. Say
        // so, because nothing else here would: unlike the hidraw handle next
        // door there is no reader on this interface to notice first.
        if (lastErrno == ENODEV || lastErrno == ENOENT) {
            closeDevice();
            Q_EMIT disconnected();
            return false;
        }
        Q_EMIT errorOccurred(failure);
        return false;
    }

    if (written != buffer.size()) {
        const QString message = QStringLiteral("Short transfer on %1: %2 of %3 bytes")
                                    .arg(endpointName(endpoint))
                                    .arg(written)
                                    .arg(buffer.size());
        setError(message);
        Q_EMIT errorOccurred(message);
        return false;
    }

    Q_EMIT transferred(TrafficDirection::Out, endpoint, data);
    return true;
}

bool LinuxUsbInterface::readInterrupt(quint8 endpoint, QByteArray &out, int timeoutMs)
{
    const UsbEndpointInfo *target = checkEndpoint(endpoint, /*wantOut=*/false);
    if (!target)
        return false;

    QByteArray buffer(target->maxPacketSize, Qt::Uninitialized);

    usbdevfs_bulktransfer transfer = {};
    transfer.ep = endpoint;
    transfer.len = static_cast<unsigned int>(buffer.size());
    transfer.timeout = static_cast<unsigned int>(timeoutMs < 0 ? 0 : timeoutMs);
    transfer.data = buffer.data();

    int read = 0;
    QString failure;
    {
        const QMutexLocker locker(&m_ioMutex);
        if (m_fd < 0) {
            setError(QStringLiteral("Interface is not open"));
            return false;
        }
        read = ::ioctl(m_fd, USBDEVFS_BULK, &transfer);
        if (read < 0)
            failure = fail(QStringLiteral("USBDEVFS_BULK"));
    }

    if (!failure.isEmpty()) {
        Q_EMIT errorOccurred(failure);
        return false;
    }

    buffer.resize(read);
    out = buffer;
    Q_EMIT transferred(TrafficDirection::In, endpoint, out);
    return true;
}

QString LinuxUsbInterface::lastError() const
{
    const QMutexLocker locker(&m_errorMutex);
    return m_lastError;
}

void LinuxUsbInterface::setError(const QString &message)
{
    const QMutexLocker locker(&m_errorMutex);
    m_lastError = message;
}

QString LinuxUsbInterface::fail(const QString &context)
{
    // errno is thread-local, so this is the failing thread's own error even when
    // the call came from a worker.
    const int err = errno;
    const QString message =
        QStringLiteral("%1 on %2 failed: %3").arg(context, m_nodePath, errnoString(err));
    setError(message);
    qCWarning(hidcenterHid) << message;
    return message;
}

void LinuxUsbInterface::closeDevice()
{
    // Waits for a transfer already in flight on a worker thread. Closing under
    // one would leave it ioctl'ing a descriptor number the kernel is free to
    // hand to the next thing that opens a file.
    const QMutexLocker locker(&m_ioMutex);
    if (m_fd < 0)
        return;

    release();
    ::close(m_fd);
    m_fd = -1;
}

} // namespace hidcenter
