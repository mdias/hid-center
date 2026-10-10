// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <QHashFunctions>

#include "core/usb/HidDriverInfo.h"
#include "core/usb/ReportDescriptor.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// A USB vendor/product pair. This is what a device module declares support for.
struct HIDCENTER_CORE_EXPORT UsbDeviceId
{
    quint16 vendorId = 0;
    quint16 productId = 0;

    bool isValid() const { return vendorId != 0 || productId != 0; }

    /// "3670:0500" — the lsusb spelling, used in logs and the UI.
    QString toString() const;

    friend bool operator==(const UsbDeviceId &a, const UsbDeviceId &b)
    {
        return a.vendorId == b.vendorId && a.productId == b.productId;
    }
    friend bool operator!=(const UsbDeviceId &a, const UsbDeviceId &b) { return !(a == b); }
};

inline size_t qHash(const UsbDeviceId &id, size_t seed = 0) noexcept
{
    return ::qHash((quint32(id.vendorId) << 16) | id.productId, seed);
}

/// Whether an interface's device node can be opened for reading and for writing.
///
/// Enumeration does not need either — report descriptors come from sysfs, which
/// is world-readable — so a device can list perfectly and still refuse every
/// transfer. That is the single most common way this application fails, and
/// until now the only sign of it was an open failing later.
struct HIDCENTER_CORE_EXPORT UsbAccessInfo
{
    /// False when nothing was checked: an offline preview, or a platform with no
    /// such concept. Deliberately distinct from "checked, and the answer is no",
    /// which is a real and actionable finding — the same distinction
    /// HidDriverInfo::known draws, and for the same reason.
    bool known = false;

    bool readable = false;
    bool writable = false;

    friend bool operator==(const UsbAccessInfo &a, const UsbAccessInfo &b)
    {
        return a.known == b.known && a.readable == b.readable && a.writable == b.writable;
    }
    friend bool operator!=(const UsbAccessInfo &a, const UsbAccessInfo &b) { return !(a == b); }
};

/// What an endpoint carries, from bmAttributes bits 0..1.
enum class UsbTransferType {
    Control,
    Isochronous,
    Bulk,
    Interrupt,
};

HIDCENTER_CORE_EXPORT QString usbTransferTypeName(UsbTransferType type);

/// bInterfaceClass as a word, for the classes worth naming, and the raw hex for
/// the rest. Only the handful a configuration tool actually meets are spelled
/// out; inventing names for the others would be a table nobody maintains.
HIDCENTER_CORE_EXPORT QString usbInterfaceClassName(quint8 interfaceClass);

/// One endpoint of one interface, as its descriptor declares it.
///
/// This is the level the host has to reach to send anything that is not a
/// control transfer, and nothing above it existed until the LED ring turned out
/// to want an interrupt OUT rather than a SET_REPORT.
struct HIDCENTER_CORE_EXPORT UsbEndpointInfo
{
    /// bEndpointAddress, **direction bit included**. 0x02 and 0x82 are the OUT
    /// and IN halves of endpoint 2 and are two different endpoints; keeping the
    /// bit is what lets one number address exactly one of them.
    quint8 address = 0;

    quint8 attributes = 0;  ///< bmAttributes, raw.
    UsbTransferType type = UsbTransferType::Control;
    quint16 maxPacketSize = 0;
    quint8 interval = 0;  ///< bInterval, in the units the speed implies.

    /// Bit 7 of the address: set means device-to-host.
    bool isIn() const { return (address & 0x80) != 0; }
    bool isOut() const { return !isIn(); }

    /// "0x02 OUT interrupt, 64 bytes" — one endpoint as the Status page prints it.
    QString describe() const;

    friend bool operator==(const UsbEndpointInfo &a, const UsbEndpointInfo &b)
    {
        return a.address == b.address && a.attributes == b.attributes && a.type == b.type
            && a.maxPacketSize == b.maxPacketSize && a.interval == b.interval;
    }
    friend bool operator!=(const UsbEndpointInfo &a, const UsbEndpointInfo &b) { return !(a == b); }
};

/// The HID half of an interface, when the OS gave it a hidraw node.
///
/// Split out rather than inherited because UsbInterfaceInfo is a value type
/// copied into a QVector and passed across the plugin boundary, where a
/// base/derived pair would slice on every copy. `present` is what a downcast
/// would otherwise have been asking.
///
/// An interface can be HID by class and still have none of this: the Alpha Evo's
/// interface 1 is class 3 with a 53-byte report descriptor its firmware declares,
/// and no HID driver is bound to it, so the kernel never created a node and never
/// parsed the descriptor.
struct HIDCENTER_CORE_EXPORT UsbHidInfo
{
    /// False when there is no hidraw node. Every other field is then empty, and
    /// no feature report or input report can be exchanged on this interface.
    bool present = false;

    QString devicePath;      ///< OS handle to open. Linux: "/dev/hidraw7".
    QString hidSysPath;      ///< The `hid` node between the interface and the hidraw one.

    /// The interface's report descriptor, filled in during enumeration.
    ///
    /// On Linux this comes from sysfs, which is world-readable even when the
    /// device node is not — so the analyzer can list a device's reports whether
    /// or not the udev rules granting I/O access are installed. Copies are cheap:
    /// the descriptor holds only implicitly shared containers.
    ReportDescriptor descriptor;

    quint16 usagePage = 0;  ///< Top-level collection usage page.
    quint16 usage = 0;      ///< Top-level collection usage.
};

/// One interface of a physical device.
///
/// A single device commonly exposes several: a wheelbase typically has a gamepad
/// interface the OS binds as a joystick plus a vendor-defined one that carries
/// the configuration protocol. They all share the same VID/PID, which is why
/// interfaces are grouped under UsbDeviceInfo rather than treated as devices.
struct HIDCENTER_CORE_EXPORT UsbInterfaceInfo
{
    QString sysPath;           ///< Stable OS identity of this interface node.
    int interfaceNumber = -1;  ///< bInterfaceNumber, or -1 when unknown.

    quint8 interfaceClass = 0;
    quint8 interfaceSubClass = 0;
    quint8 interfaceProtocol = 0;

    /// Every endpoint the interface declares, in descriptor order. Empty for a
    /// transport with no such concept — Bluetooth HID, say.
    QVector<UsbEndpointInfo> endpoints;

    /// The kernel driver bound to the *interface*, or empty when nothing holds
    /// it. "usbhid" for anything the HID stack took, whether or not a HID driver
    /// went on to bind below it.
    ///
    /// Separate from `driver` below, which is about the HID node one level down
    /// and is the one that answers the force-feedback question. This one answers
    /// a different question: whether claiming the interface for a raw transfer
    /// would take it away from something.
    QString kernelDriver;

    /// Which HID driver is bound below this interface, and whether it gives the
    /// interface force feedback. Filled in during enumeration, like `endpoints`
    /// and for the same reason: it is what the OS said at the moment this
    /// interface was seen.
    ///
    /// A backend that does not fill this in leaves `known` false, and callers say
    /// nothing rather than guess.
    HidDriverInfo driver;

    /// Whether the node this interface is opened through can be read and
    /// written, as it stood when the interface was enumerated.
    ///
    /// That node is the hidraw one when there is one, and the device's usbfs
    /// node otherwise — /dev/bus/usb/BBB/DDD on Linux. usbfs has one node per
    /// device, so every interface without a hidraw node shares one answer.
    ///
    /// Per interface rather than per device because the two kinds of node are
    /// granted separately, and a device can have one without the other: the
    /// configuration protocol answering while the LED ring stays dark.
    UsbAccessInfo access;

    /// Everything that is only true of an interface with a hidraw node. See
    /// UsbHidInfo, and check isHid() before reading any of it.
    UsbHidInfo hid;

    /// Whether feature reports and input reports can be exchanged here.
    bool isHid() const { return hid.present; }

    /// Short label for the UI: "if 1 (hidraw7)", or "if 1" with no node.
    QString displayName() const;

    /// The endpoint with this bEndpointAddress, direction bit included, or
    /// nullptr. 0x02 finds the OUT half of endpoint 2 and never the IN half.
    const UsbEndpointInfo *endpointByAddress(quint8 address) const;

    /// This interface's first interrupt OUT endpoint, or nullptr.
    ///
    /// The one an output report written to a hidraw node goes out on, which is
    /// what makes it the endpoint reachable without claiming the interface.
    const UsbEndpointInfo *interruptOutEndpoint() const;
};

/// One physical device, with every interface it exposes.
struct HIDCENTER_CORE_EXPORT UsbDeviceInfo
{
    UsbDeviceId id;
    QString manufacturer;
    QString product;
    QString serial;

    /// Identity of the physical device (its node in the OS device tree), stable
    /// for as long as it stays plugged in. This is the key that lets several
    /// identical devices — same VID, same PID, no serial — be told apart in the
    /// sidebar.
    QString devicePath;

    /// Bus and device number, for the raw transfer node — /dev/bus/usb/001/016 on
    /// Linux. Both are zero for a transport with no such addressing, which is
    /// also what says no endpoint on this device can be reached directly.
    int busNumber = 0;
    int deviceNumber = 0;

    QVector<UsbInterfaceInfo> interfaces;

    bool isValid() const { return !devicePath.isEmpty(); }

    /// Best available human name, falling back to the VID:PID pair.
    QString describe() const;

    /// The interface with this bInterfaceNumber, or nullptr.
    const UsbInterfaceInfo *interfaceByNumber(int number) const;

    /// The first HID interface whose top-level collection matches, or nullptr.
    /// Interfaces with no hidraw node have no usage to match and are skipped.
    const UsbInterfaceInfo *interfaceByUsage(quint16 usagePage, quint16 usage) const;

    /// The interface owning the endpoint with this address, or nullptr. Endpoints
    /// belong to interfaces, and an interface has to be opened before any of its
    /// endpoints can carry anything.
    const UsbInterfaceInfo *interfaceForEndpoint(quint8 address) const;
};

/// What distinguishes one enumeration of a device from another.
///
/// A device is keyed by its USB syspath, which survives a HID driver being
/// swapped underneath it. Nothing in the paths below can see that swap either:
/// the HID node between the USB device and the hidraw node persists, and the
/// hidraw node is destroyed and recreated — commonly with the same minor number,
/// since the freed one is the next allocated. What *is* bound is the part that
/// changes, so it has to be in here.
///
/// Getting this wrong is not a cosmetic matter: a rebind that goes unnoticed
/// leaves every layer holding a descriptor on a device node that no longer
/// exists.
HIDCENTER_CORE_EXPORT QString enumerationSignature(const UsbDeviceInfo &device);

} // namespace hidcenter
