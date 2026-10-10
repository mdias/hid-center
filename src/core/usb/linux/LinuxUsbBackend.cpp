// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/linux/LinuxUsbBackend.h"

#include "core/Logging.h"
#include "core/usb/linux/LinuxUsbDevice.h"
#include "core/usb/linux/LinuxUsbHidInterface.h"
#include "core/usb/linux/LinuxUsbInterface.h"
#include "core/usb/linux/LinuxHidDriverInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>

#include <libudev.h>
#include <unistd.h>

#include <fcntl.h>

#include <algorithm>

namespace hidcenter {
namespace {

/// udev events arrive one per interface, so a single plug produces a burst.
constexpr int kRescanDebounceMs = 200;

QString sysattr(udev_device *device, const char *name)
{
    if (!device)
        return {};
    const char *value = udev_device_get_sysattr_value(device, name);
    return value ? QString::fromUtf8(value).trimmed() : QString();
}

QString udevProperty(udev_device *device, const char *name)
{
    if (!device)
        return {};
    const char *value = udev_device_get_property_value(device, name);
    return value ? QString::fromUtf8(value).trimmed() : QString();
}

QString syspathOf(udev_device *device)
{
    if (!device)
        return {};
    const char *value = udev_device_get_syspath(device);
    return value ? QString::fromUtf8(value) : QString();
}

/// The HID_ID property of a `hid` device reads "0003:00003670:00000500", i.e.
/// bus:vendor:product in hex. This is the fallback identity for transports with
/// no USB parent, such as Bluetooth.
bool parseHidId(const QString &hidId, quint16 *vendorId, quint16 *productId)
{
    const QStringList parts = hidId.split(QLatin1Char(':'));
    if (parts.size() != 3)
        return false;

    bool vendorOk = false;
    bool productOk = false;
    const uint vendor = parts.at(1).toUInt(&vendorOk, 16);
    const uint product = parts.at(2).toUInt(&productOk, 16);
    if (!vendorOk || !productOk)
        return false;

    *vendorId = quint16(vendor & 0xFFFF);
    *productId = quint16(product & 0xFFFF);
    return true;
}

/// bInterfaceClass for Human Interface Device.
constexpr quint8 kHidInterfaceClass = 3;

/// Whether this device is one hid-center has any business listing.
///
/// Walking the USB tree finds everything on the bus — hubs, webcams, the sound
/// card — and the application configures none of it. One HID interface is the
/// bar, and it is deliberately the *class* rather than a hidraw node: an
/// interface the kernel bound no HID driver to has no node and is exactly the
/// thing this rewrite exists to reach.
bool hasHidInterface(const UsbDeviceInfo &device)
{
    for (const UsbInterfaceInfo &iface : device.interfaces) {
        if (iface.interfaceClass == kHidInterfaceClass || iface.isHid())
            return true;
    }
    return false;
}

/// The endpoints an interface declares, from its ep_XX/ sysfs children.
///
/// Read out of sysfs rather than by parsing the configuration descriptor,
/// because sysfs is world-readable and /dev/bus/usb is not: an endpoint list the
/// Status page can show whether or not the udev rules are installed, on the same
/// reasoning as the report descriptor below.
QVector<UsbEndpointInfo> readEndpoints(const QString &interfaceSysPath)
{
    QVector<UsbEndpointInfo> endpoints;
    if (interfaceSysPath.isEmpty())
        return endpoints;

    const QDir dir(interfaceSysPath);
    const QStringList names = dir.entryList({QStringLiteral("ep_*")}, QDir::Dirs);

    for (const QString &name : names) {
        const QString path = dir.filePath(name);

        const auto attribute = [&path](const char *attr) {
            QFile file(path + QLatin1Char('/') + QLatin1String(attr));
            if (!file.open(QIODevice::ReadOnly))
                return QString();
            return QString::fromUtf8(file.readAll()).trimmed();
        };

        bool addressOk = false;
        const uint address = attribute("bEndpointAddress").toUInt(&addressOk, 16);
        if (!addressOk)
            continue;

        UsbEndpointInfo endpoint;
        endpoint.address = quint8(address & 0xFF);
        endpoint.attributes = quint8(attribute("bmAttributes").toUInt(nullptr, 16) & 0xFF);
        // Bits 0..1 of bmAttributes, in the order the USB spec lists them.
        endpoint.type = UsbTransferType(endpoint.attributes & 0x03);
        endpoint.maxPacketSize =
            quint16(attribute("wMaxPacketSize").toUInt(nullptr, 16) & 0x07FF);
        endpoint.interval = quint8(attribute("bInterval").toUInt(nullptr, 16) & 0xFF);

        endpoints.append(endpoint);
    }

    // Ordered by address, so two enumerations of one device always produce the
    // same list and enumerationSignature() compares equal.
    std::sort(endpoints.begin(), endpoints.end(),
              [](const UsbEndpointInfo &a, const UsbEndpointInfo &b) {
                  return a.address < b.address;
              });
    return endpoints;
}

/// The `hid` node directly under an interface, or an empty string.
///
/// Present whenever usbhid took the interface, whether or not a HID driver went
/// on to bind to it — which is the difference between an interface with a hidraw
/// node and one without.
QString findHidChild(udev_device *usbInterface)
{
    const QString interfaceSysPath = syspathOf(usbInterface);
    if (interfaceSysPath.isEmpty())
        return {};

    const QDir dir(interfaceSysPath);
    // The HID bus names these "0003:3670:0500.023C": bus, vendor, product and a
    // per-boot counter.
    const QStringList names = dir.entryList({QStringLiteral("[0-9A-F][0-9A-F][0-9A-F][0-9A-F]:*")},
                                            QDir::Dirs);
    for (const QString &name : names) {
        if (QFile::exists(dir.filePath(name) + QStringLiteral("/report_descriptor")))
            return dir.filePath(name);
    }
    return {};
}

/// The /dev/hidrawN node under a hid device, or an empty string when no HID
/// driver is bound and the kernel therefore made none.
QString findHidrawNode(const QString &hidSysPath)
{
    const QDir dir(hidSysPath + QStringLiteral("/hidraw"));
    if (!dir.exists())
        return {};

    const QStringList names = dir.entryList({QStringLiteral("hidraw*")}, QDir::Dirs);
    if (names.isEmpty())
        return {};

    return QStringLiteral("/dev/") + names.first();
}

/// Read a HID interface's report descriptor straight out of sysfs.
///
/// This attribute is world-readable even when /dev/hidrawN is not, so the
/// top-level usage — and the analyzer's list of reports — is available for every
/// device, whether or not the udev rules granting write access are installed.
QByteArray readDescriptorFromSysfs(const QString &hidSysPath)
{
    if (hidSysPath.isEmpty())
        return {};

    QFile file(hidSysPath + QStringLiteral("/report_descriptor"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

/// Whether this device node can be opened for reading and for writing.
///
/// Small enough to live here rather than in a file of its own, unlike the driver
/// reading next door — but it is still OS-specific, and this file is where that
/// belongs.
///
/// AT_EACCESS rather than a plain access(2): the udev rules hand out access
/// through `uaccess`, which is a POSIX ACL against the *effective* uid, and
/// access(2) tests the real one. Checking the wrong uid would report a device as
/// unreachable that opens perfectly.
UsbAccessInfo readNodeAccess(const QString &devicePath)
{
    if (devicePath.isEmpty())
        return {};

    const QByteArray path = devicePath.toLocal8Bit();
    UsbAccessInfo access;
    access.known = true;
    access.readable = ::faccessat(AT_FDCWD, path.constData(), R_OK, AT_EACCESS) == 0;
    access.writable = ::faccessat(AT_FDCWD, path.constData(), W_OK, AT_EACCESS) == 0;
    return access;
}

} // namespace

LinuxUsbBackend::LinuxUsbBackend(QObject *parent)
    : IUsbBackend(parent)
{
    m_udev = udev_new();
    if (!m_udev)
        qCWarning(hidcenterHid, "udev_new() failed; no devices will be enumerated.");
}

LinuxUsbBackend::~LinuxUsbBackend()
{
    stopMonitoring();
    if (m_udev)
        udev_unref(m_udev);
}

QVector<UsbDeviceInfo> LinuxUsbBackend::enumerate()
{
    QVector<UsbDeviceInfo> result;
    if (!m_udev)
        return result;

    QHash<QString, UsbDeviceInfo> devices;
    enumerateUsb(devices);
    enumerateNonUsbHid(devices);

    result.reserve(devices.size());
    for (UsbDeviceInfo &info : devices) {
        if (!hasHidInterface(info))
            continue;

        // Stable ordering so the UI does not shuffle between rescans.
        std::sort(info.interfaces.begin(), info.interfaces.end(),
                  [](const UsbInterfaceInfo &a, const UsbInterfaceInfo &b) {
                      if (a.interfaceNumber != b.interfaceNumber)
                          return a.interfaceNumber < b.interfaceNumber;
                      return a.sysPath < b.sysPath;
                  });
        result.append(info);
    }
    std::sort(result.begin(), result.end(),
              [](const UsbDeviceInfo &a, const UsbDeviceInfo &b) {
                  return a.devicePath < b.devicePath;
              });

    return result;
}

void LinuxUsbBackend::enumerateUsb(QHash<QString, UsbDeviceInfo> &devices)
{
    udev_enumerate *enumerator = udev_enumerate_new(m_udev);
    if (!enumerator) {
        qCWarning(hidcenterHid, "udev_enumerate_new() failed.");
        return;
    }

    // usb_interface rather than usb_device, because the interface is what
    // carries everything worth knowing — its endpoints, its driver, and the
    // hidraw node underneath it when there is one. The device above it is
    // reached from here and is where the interfaces are grouped.
    udev_enumerate_add_match_subsystem(enumerator, "usb");
    udev_enumerate_add_match_property(enumerator, "DEVTYPE", "usb_interface");
    udev_enumerate_scan_devices(enumerator);

    udev_list_entry *entry = nullptr;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerator))
    {
        const char *sysPath = udev_list_entry_get_name(entry);
        if (!sysPath)
            continue;

        udev_device *usbInterface = udev_device_new_from_syspath(m_udev, sysPath);
        if (!usbInterface)
            continue;

        // Parents are owned by the child device; they must not be unref'd.
        udev_device *usbDevice =
            udev_device_get_parent_with_subsystem_devtype(usbInterface, "usb", "usb_device");
        if (!usbDevice) {
            udev_device_unref(usbInterface);
            continue;
        }

        UsbDeviceId id;
        bool vendorOk = false;
        bool productOk = false;
        id.vendorId = quint16(sysattr(usbDevice, "idVendor").toUInt(&vendorOk, 16));
        id.productId = quint16(sysattr(usbDevice, "idProduct").toUInt(&productOk, 16));
        if (!vendorOk || !productOk) {
            udev_device_unref(usbInterface);
            continue;
        }

        const QString devicePath = syspathOf(usbDevice);

        UsbDeviceInfo &info = devices[devicePath];
        if (!info.isValid()) {
            info.id = id;
            info.devicePath = devicePath;
            info.manufacturer = sysattr(usbDevice, "manufacturer");
            info.product = sysattr(usbDevice, "product");
            info.serial = sysattr(usbDevice, "serial");
            // Decimal here, unlike bInterfaceNumber below: these two are the
            // only sysfs USB attributes printed in base ten, and they are what
            // /dev/bus/usb/BBB/DDD is built from.
            info.busNumber = sysattr(usbDevice, "busnum").toInt();
            info.deviceNumber = sysattr(usbDevice, "devnum").toInt();
        }

        info.interfaces.append(readInterface(
            usbInterface, sysPath,
            LinuxUsbInterface::nodePathFor(info.busNumber, info.deviceNumber)));

        udev_device_unref(usbInterface);
    }

    udev_enumerate_unref(enumerator);
}

UsbInterfaceInfo LinuxUsbBackend::readInterface(udev_device *usbInterface, const char *sysPath,
                                                const QString &usbfsNode)
{
    UsbInterfaceInfo iface;
    iface.sysPath = QString::fromUtf8(sysPath);

    bool numberOk = false;
    // sysfs prints these in hex.
    const int number = sysattr(usbInterface, "bInterfaceNumber").toInt(&numberOk, 16);
    if (numberOk)
        iface.interfaceNumber = number;

    iface.interfaceClass = quint8(sysattr(usbInterface, "bInterfaceClass").toUInt(nullptr, 16));
    iface.interfaceSubClass =
        quint8(sysattr(usbInterface, "bInterfaceSubClass").toUInt(nullptr, 16));
    iface.interfaceProtocol =
        quint8(sysattr(usbInterface, "bInterfaceProtocol").toUInt(nullptr, 16));

    iface.endpoints = readEndpoints(iface.sysPath);

    // Absent symlink means nothing holds the interface, which is an answer
    // rather than a gap.
    const QString bound =
        QFileInfo(iface.sysPath + QStringLiteral("/driver")).symLinkTarget();
    if (!bound.isEmpty())
        iface.kernelDriver = QFileInfo(bound).fileName();

    // The driver on the *interface* is usbhid for anything the HID stack took,
    // which says nothing about force feedback. The one that matters is on the
    // hid node below, and is read with it.
    const QString hidSysPath = findHidChild(usbInterface);
    if (!hidSysPath.isEmpty()) {
        iface.driver = readHidDriver(hidSysPath);

        const QString node = findHidrawNode(hidSysPath);
        if (!node.isEmpty()) {
            iface.hid.present = true;
            iface.hid.devicePath = node;
            iface.hid.hidSysPath = hidSysPath;
            iface.hid.descriptor = ReportDescriptor::parse(readDescriptorFromSysfs(hidSysPath));
            iface.hid.usagePage = iface.hid.descriptor.topUsagePage();
            iface.hid.usage = iface.hid.descriptor.topUsage();
            // Keyed on the device node, not the sysfs path: this is about
            // opening /dev/hidrawN, which is the thing that will actually be
            // refused.
            iface.access = readNodeAccess(node);
        }
    }

    // No hidraw node, so LinuxUsbDevice opens this one over usbfs instead, and
    // that is the node whose permissions decide whether it can be reached. A
    // separate udev rule grants it, so it can be refused while the hidraw
    // nodes beside it open perfectly.
    if (!iface.isHid())
        iface.access = readNodeAccess(usbfsNode);

    // Last, over what was read above. An interface this application has
    // claimed reads as bound to usbfs with no hid node under it, and reported
    // that way it would look changed, rebuild the module holding it, and be
    // claimed again by the rebuilt one — a device reconnecting four times a
    // second.
    LinuxUsbInterface::maskOwnClaim(usbfsNode, iface);

    return iface;
}

void LinuxUsbBackend::enumerateNonUsbHid(QHash<QString, UsbDeviceInfo> &devices)
{
    // Bluetooth and I2C HID devices have no usb_device above them, so the walk
    // over usb_interface nodes above never sees them. They are perfectly usable
    // and were listed before this backend started walking USB, so they are
    // gathered here rather than lost: one device, one interface, no endpoints.
    udev_enumerate *enumerator = udev_enumerate_new(m_udev);
    if (!enumerator) {
        qCWarning(hidcenterHid, "udev_enumerate_new() failed.");
        return;
    }

    udev_enumerate_add_match_subsystem(enumerator, "hidraw");
    udev_enumerate_scan_devices(enumerator);

    udev_list_entry *entry = nullptr;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerator))
    {
        const char *sysPath = udev_list_entry_get_name(entry);
        if (!sysPath)
            continue;

        udev_device *hidraw = udev_device_new_from_syspath(m_udev, sysPath);
        if (!hidraw)
            continue;

        const char *devNode = udev_device_get_devnode(hidraw);
        udev_device *hidParent =
            udev_device_get_parent_with_subsystem_devtype(hidraw, "hid", nullptr);
        udev_device *usbDevice =
            udev_device_get_parent_with_subsystem_devtype(hidraw, "usb", "usb_device");

        // Anything with a USB parent was already found, endpoints and all.
        if (!devNode || !hidParent || usbDevice) {
            udev_device_unref(hidraw);
            continue;
        }

        UsbDeviceId id;
        if (!parseHidId(udevProperty(hidParent, "HID_ID"), &id.vendorId, &id.productId)) {
            udev_device_unref(hidraw);
            continue;
        }

        const QString hidSysPath = syspathOf(hidParent);

        UsbDeviceInfo &info = devices[hidSysPath];
        if (!info.isValid()) {
            info.id = id;
            info.devicePath = hidSysPath;
            info.product = udevProperty(hidParent, "HID_NAME");
            info.serial = udevProperty(hidParent, "HID_UNIQ");
        }

        UsbInterfaceInfo iface;
        iface.sysPath = QString::fromUtf8(sysPath);
        iface.driver = readHidDriver(hidSysPath);
        iface.hid.present = true;
        iface.hid.devicePath = QString::fromUtf8(devNode);
        iface.hid.hidSysPath = hidSysPath;
        iface.hid.descriptor = ReportDescriptor::parse(readDescriptorFromSysfs(hidSysPath));
        iface.hid.usagePage = iface.hid.descriptor.topUsagePage();
        iface.hid.usage = iface.hid.descriptor.topUsage();
        iface.access = readNodeAccess(iface.hid.devicePath);

        info.interfaces.append(iface);

        udev_device_unref(hidraw);
    }

    udev_enumerate_unref(enumerator);
}

std::unique_ptr<IUsbDevice> LinuxUsbBackend::openDevice(const UsbDeviceInfo &info)
{
    return std::make_unique<LinuxUsbDevice>(info);
}

bool LinuxUsbBackend::startMonitoring()
{
    if (m_monitor)
        return true;
    if (!m_udev)
        return false;

    m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_monitor) {
        qCWarning(hidcenterHid, "udev_monitor_new_from_netlink() failed; hotplug is disabled.");
        return false;
    }

    // Both subsystems. A device being plugged in or out is a usb event, and a
    // driver binding or unbinding underneath one that stays plugged in is only a
    // hidraw event — and that second one changes whether an interface can be
    // opened at all, which enumerationSignature() turns into a rebuild.
    udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "usb", nullptr);
    udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "hidraw", nullptr);
    if (udev_monitor_enable_receiving(m_monitor) < 0) {
        qCWarning(hidcenterHid, "udev_monitor_enable_receiving() failed; hotplug is disabled.");
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
        return false;
    }

    m_rescanTimer = new QTimer(this);
    m_rescanTimer->setSingleShot(true);
    m_rescanTimer->setInterval(kRescanDebounceMs);
    connect(m_rescanTimer, &QTimer::timeout, this, &LinuxUsbBackend::rescan);

    m_notifier = new QSocketNotifier(udev_monitor_get_fd(m_monitor), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &LinuxUsbBackend::onMonitorReadable);

    // Seed the baseline so the first rescan reports only genuine changes.
    m_known.clear();
    for (const UsbDeviceInfo &device : enumerate())
        m_known.insert(device.devicePath, device);

    qCDebug(hidcenterHid) << "Monitoring usb and hidraw hotplug events;" << m_known.size()
                       << "device(s) present";
    return true;
}

void LinuxUsbBackend::stopMonitoring()
{
    delete m_notifier;
    m_notifier = nullptr;

    delete m_rescanTimer;
    m_rescanTimer = nullptr;

    if (m_monitor) {
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
    }
}

void LinuxUsbBackend::onMonitorReadable()
{
    if (!m_monitor)
        return;

    // The event must be consumed or the notifier fires forever. What changed
    // does not matter: the debounced rescan diffs the whole picture.
    while (udev_device *device = udev_monitor_receive_device(m_monitor))
        udev_device_unref(device);

    if (m_rescanTimer)
        m_rescanTimer->start();
}

void LinuxUsbBackend::rescan()
{
    QHash<QString, UsbDeviceInfo> current;
    for (const UsbDeviceInfo &device : enumerate())
        current.insert(device.devicePath, device);

    // Removals first, so a device that re-enumerated is torn down before its
    // replacement is announced.
    const QStringList previousPaths = m_known.keys();
    for (const QString &path : previousPaths) {
        const auto it = current.constFind(path);
        const bool gone = it == current.constEnd();
        // Compared by signature rather than by interface count. A driver being
        // swapped underneath a device leaves the count alone — and often the
        // paths too, since the freed hidraw minor is the next one allocated — so
        // a count comparison would report nothing and leave every layer holding
        // a descriptor on a node that no longer exists.
        const bool changed =
            !gone && enumerationSignature(*it) != enumerationSignature(m_known.value(path));
        if (gone || changed) {
            m_known.remove(path);
            qCDebug(hidcenterHid) << "Device removed:" << path;
            Q_EMIT deviceRemoved(path);
        }
    }

    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        if (m_known.contains(it.key()))
            continue;
        m_known.insert(it.key(), it.value());
        qCDebug(hidcenterHid) << "Device added:" << it.key() << it->id.toString()
                           << it->interfaces.size() << "interface(s)";
        Q_EMIT deviceAdded(it.value());
    }
}

} // namespace hidcenter
