// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/UsbDeviceInfo.h"

#include <QCoreApplication>
#include <QStringList>

namespace hidcenter {

QString UsbDeviceId::toString() const
{
    return QStringLiteral("%1:%2")
        .arg(vendorId, 4, 16, QLatin1Char('0'))
        .arg(productId, 4, 16, QLatin1Char('0'));
}

QString usbTransferTypeName(UsbTransferType type)
{
    switch (type) {
    case UsbTransferType::Control:
        return QCoreApplication::translate("hidcenter", "control");
    case UsbTransferType::Isochronous:
        return QCoreApplication::translate("hidcenter", "isochronous");
    case UsbTransferType::Bulk:
        return QCoreApplication::translate("hidcenter", "bulk");
    case UsbTransferType::Interrupt:
        return QCoreApplication::translate("hidcenter", "interrupt");
    }
    return {};
}

QString usbInterfaceClassName(quint8 interfaceClass)
{
    switch (interfaceClass) {
    case 0x01: return QCoreApplication::translate("hidcenter", "audio");
    case 0x02: return QCoreApplication::translate("hidcenter", "communications");
    case 0x03: return QCoreApplication::translate("hidcenter", "HID");
    case 0x08: return QCoreApplication::translate("hidcenter", "mass storage");
    case 0x09: return QCoreApplication::translate("hidcenter", "hub");
    case 0x0E: return QCoreApplication::translate("hidcenter", "video");
    case 0xFF: return QCoreApplication::translate("hidcenter", "vendor");
    default:
        return QStringLiteral("0x%1").arg(interfaceClass, 2, 16, QLatin1Char('0'));
    }
}

QString UsbEndpointInfo::describe() const
{
    return QStringLiteral("0x%1 %2 %3, %4 bytes")
        .arg(address, 2, 16, QLatin1Char('0'))
        .arg(isIn() ? QStringLiteral("IN") : QStringLiteral("OUT"),
             usbTransferTypeName(type))
        .arg(maxPacketSize);
}

QString UsbInterfaceInfo::displayName() const
{
    const QString node = hid.devicePath.section(QLatin1Char('/'), -1);
    if (interfaceNumber < 0)
        return node.isEmpty() ? sysPath.section(QLatin1Char('/'), -1) : node;
    if (node.isEmpty())
        return QStringLiteral("if %1").arg(interfaceNumber);
    return QStringLiteral("if %1 (%2)").arg(interfaceNumber).arg(node);
}

const UsbEndpointInfo *UsbInterfaceInfo::endpointByAddress(quint8 address) const
{
    for (const UsbEndpointInfo &endpoint : endpoints) {
        if (endpoint.address == address)
            return &endpoint;
    }
    return nullptr;
}

const UsbEndpointInfo *UsbInterfaceInfo::interruptOutEndpoint() const
{
    for (const UsbEndpointInfo &endpoint : endpoints) {
        if (endpoint.isOut() && endpoint.type == UsbTransferType::Interrupt)
            return &endpoint;
    }
    return nullptr;
}

QString UsbDeviceInfo::describe() const
{
    if (!product.isEmpty() && !manufacturer.isEmpty())
        return QStringLiteral("%1 %2").arg(manufacturer, product);
    if (!product.isEmpty())
        return product;
    if (!manufacturer.isEmpty())
        return manufacturer;
    return id.toString();
}

const UsbInterfaceInfo *UsbDeviceInfo::interfaceByNumber(int number) const
{
    for (const UsbInterfaceInfo &iface : interfaces) {
        if (iface.interfaceNumber == number)
            return &iface;
    }
    return nullptr;
}

const UsbInterfaceInfo *UsbDeviceInfo::interfaceByUsage(quint16 usagePage, quint16 usage) const
{
    for (const UsbInterfaceInfo &iface : interfaces) {
        // An interface with no hidraw node has no parsed descriptor, so its usage
        // is zero rather than unknown — matching on it would hand back the wrong
        // interface for a 0:0 query.
        if (!iface.isHid())
            continue;
        if (iface.hid.usagePage == usagePage && iface.hid.usage == usage)
            return &iface;
    }
    return nullptr;
}

const UsbInterfaceInfo *UsbDeviceInfo::interfaceForEndpoint(quint8 address) const
{
    for (const UsbInterfaceInfo &iface : interfaces) {
        if (iface.endpointByAddress(address))
            return &iface;
    }
    return nullptr;
}

QString enumerationSignature(const UsbDeviceInfo &device)
{
    QStringList parts;
    parts.reserve(device.interfaces.size());

    for (const UsbInterfaceInfo &iface : device.interfaces) {
        // The endpoints are in here because an interface that came back with a
        // different set of them is not the interface we last saw — an altsetting
        // change, or a device that re-enumerated with different descriptors.
        QStringList endpoints;
        endpoints.reserve(iface.endpoints.size());
        for (const UsbEndpointInfo &endpoint : iface.endpoints) {
            endpoints << QStringLiteral("%1:%2")
                             .arg(endpoint.address, 2, 16, QLatin1Char('0'))
                             .arg(endpoint.attributes);
        }

        // Access is in here so that granting it re-enumerates the device: install
        // the udev rules, run `udevadm trigger`, and the module is rebuilt and
        // reopens a handle that was refused a moment ago. Left out, the device
        // would stay unusable until it was physically replugged.
        parts << QStringLiteral("%1|%2|%3|%4|%5|%6%7%8|%9|%10")
                     .arg(iface.sysPath, iface.hid.devicePath, iface.driver.driver,
                          iface.driver.known ? QStringLiteral("known") : QString(),
                          iface.driver.forceFeedback ? QStringLiteral("ff") : QString(),
                          iface.access.known ? QStringLiteral("a") : QString(),
                          iface.access.readable ? QStringLiteral("r") : QString(),
                          iface.access.writable ? QStringLiteral("w") : QString(),
                          endpoints.join(QLatin1Char(',')), iface.kernelDriver);
    }

    // Enumeration order is udev's, and a device that came back in a different
    // order is the same device. Sorting is what keeps this a statement about the
    // device rather than about the walk that found it.
    parts.sort();
    return parts.join(QLatin1Char('\n'));
}

} // namespace hidcenter
