// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <atomic>

#include "core/usb/IUsbDeviceInterface.h"

namespace hidcenter {

/// One interface driven directly over usbfs, for endpoints the HID stack cannot
/// reach.
///
/// This is what an interface with no hidraw node opens as. There is no report
/// layer here and no input stream: bytes go to an endpoint exactly as given, and
/// come back only when asked for.
///
/// **Claiming detaches whatever kernel driver holds the interface.** That is not
/// optional — usbfs refuses USBDEVFS_CLAIMINTERFACE on a claimed interface — and
/// it is why LinuxUsbDevice never opens one of these for an interface that has a
/// hidraw node. A driver that produced a node is a driver something may be
/// using; one that did not has nothing to take away. The detach is undone on
/// close, so the kernel gets the interface back when the application lets go.
///
/// Transfers are USBDEVFS_BULK ioctls. The name is the kernel's, not a
/// description: usb_bulk_msg() looks at the endpoint descriptor and submits an
/// interrupt URB for an interrupt endpoint, which is what every endpoint reached
/// through here is.
///
/// Threading follows IUsbDeviceInterface. m_ioMutex is held across each ioctl and
/// by closeDevice() before it closes, so unplugging waits for a transfer already
/// in flight rather than pulling the descriptor out from under it.
class LinuxUsbInterface final : public IUsbDeviceInterface
{
    Q_OBJECT

public:
    /// Open /dev/bus/usb/BBB/DDD and claim `interfaceInfo`. On failure the object
    /// is constructed but isOpen() returns false and lastError() explains why;
    /// callers should use LinuxUsbDevice::openInterface(), which checks this.
    ///
    /// `busNumber` and `deviceNumber` come from the device, not the interface,
    /// which is why they are passed rather than read out of the info struct.
    LinuxUsbInterface(const UsbInterfaceInfo &interfaceInfo, int busNumber, int deviceNumber,
                      QObject *parent = nullptr);
    ~LinuxUsbInterface() override;

    /// The usbfs node for a device: "/dev/bus/usb/001/016". Empty when either
    /// number is not a real address, as for a device on no USB bus.
    ///
    /// Public so that enumeration checks access on exactly the node this class
    /// goes on to open, rather than on a second spelling of it.
    static QString nodePathFor(int busNumber, int deviceNumber);

    /// Report an interface this process holds as it was before the claim.
    ///
    /// Claiming changes what sysfs says about an interface: the driver becomes
    /// usbfs, and the hid node usbhid made under it goes away with usbhid. Seen
    /// by enumeration, that is a device that changed, so the backend tears the
    /// module down, the close hands the interface back to usbhid, the module is
    /// rebuilt and claims it again — forever. None of that is a change anyone
    /// else made, and the claim is undone on close, so enumeration is told what
    /// the interface goes back to rather than what it is while held.
    ///
    /// Does nothing for an interface no LinuxUsbInterface in this process holds.
    static void maskOwnClaim(const QString &nodePath, UsbInterfaceInfo &iface);

    bool isOpen() const override { return m_fd >= 0; }

    const UsbInterfaceInfo &interfaceInfo() const override { return m_interface; }

    bool writeInterrupt(quint8 endpoint, const QByteArray &data,
                        int timeoutMs = kDefaultTransferTimeoutMs) override;
    bool readInterrupt(quint8 endpoint, QByteArray &out,
                       int timeoutMs = kDefaultTransferTimeoutMs) override;

    QString lastError() const override;

private:
    /// Detach the kernel driver if one holds the interface, then claim it.
    /// Returns false with m_lastError set.
    bool claim();

    /// Whether the driver holding this interface may be taken off it.
    ///
    /// True only for usbhid with no HID driver bound below it, which means no
    /// node was created and nothing is using the interface. Every other driver
    /// is doing something for somebody and is left alone.
    bool detachable() const;

    /// Release the claim and hand the interface back to the kernel.
    void release();

    void closeDevice();

    /// Record why the last operation failed. Safe from either thread.
    void setError(const QString &message);

    /// setError() for a failed syscall, using the current errno, and log it.
    /// Returns the message so the caller can announce it *after* releasing
    /// m_ioMutex — a directly connected slot may issue another transfer.
    QString fail(const QString &context);

    /// The endpoint, checked against this interface's descriptors. Returns
    /// nullptr with m_lastError set when it is not one of ours, not interrupt,
    /// or pointing the wrong way.
    const UsbEndpointInfo *checkEndpoint(quint8 endpoint, bool wantOut);

    UsbInterfaceInfo m_interface;
    QString m_nodePath;  ///< /dev/bus/usb/BBB/DDD.

    /// Whether this object detached a kernel driver and owes it a reattach.
    /// False when nothing was bound, which is the ordinary case here.
    bool m_detached = false;
    bool m_claimed = false;

    /// Whether this object put the interface in the registry maskOwnClaim()
    /// reads, and so must take it back out.
    bool m_registered = false;

    /// Guards m_fd against being closed under an in-flight transfer.
    QMutex m_ioMutex;

    /// Atomic because isOpen() is read from both threads without the mutex; the
    /// mutex above is what orders a close against a transfer, not this.
    std::atomic<int> m_fd = -1;

    mutable QMutex m_errorMutex;
    QString m_lastError;
};

} // namespace hidcenter
