// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>

#include "core/usb/IUsbBackend.h"

class QSocketNotifier;
class QTimer;

struct udev;
struct udev_device;
struct udev_monitor;

namespace hidcenter {

/// Device access on Linux: libudev for enumeration and hotplug, /dev/hidraw and
/// /dev/bus/usb for I/O.
///
/// Enumeration walks the USB tree rather than the hidraw nodes hanging off it.
/// Walking the nodes only ever finds interfaces the kernel bound a HID driver
/// to, and an interface with no driver — which has endpoints, and is exactly
/// what a vendor protocol may be waiting on — would never appear at all.
class LinuxUsbBackend final : public IUsbBackend
{
    Q_OBJECT

public:
    explicit LinuxUsbBackend(QObject *parent = nullptr);
    ~LinuxUsbBackend() override;

    QString name() const override { return QStringLiteral("udev"); }

    QVector<UsbDeviceInfo> enumerate() override;
    std::unique_ptr<IUsbDevice> openDevice(const UsbDeviceInfo &info) override;

    bool startMonitoring() override;
    void stopMonitoring() override;
    bool isMonitoring() const override { return m_monitor != nullptr; }

private Q_SLOTS:
    void onMonitorReadable();
    void rescan();

private:
    /// Walk every usb_interface node, grouping them under the device above them.
    void enumerateUsb(QHash<QString, UsbDeviceInfo> &devices);

    /// One interface: its numbers, its endpoints, and the hidraw node under it
    /// when the kernel made one.
    ///
    /// `usbfsNode` is the device's /dev/bus/usb node, which an interface with no
    /// hidraw node is opened through and so has its access checked against.
    UsbInterfaceInfo readInterface(udev_device *usbInterface, const char *sysPath,
                                   const QString &usbfsNode);

    /// Pick up HID devices with no USB parent — Bluetooth, I2C — which the walk
    /// above cannot see. One device, one interface, no endpoints.
    void enumerateNonUsbHid(QHash<QString, UsbDeviceInfo> &devices);

    udev *m_udev = nullptr;
    udev_monitor *m_monitor = nullptr;
    QSocketNotifier *m_notifier = nullptr;

    /// udev delivers one event per interface, so plugging in a single device
    /// produces a burst. Coalesce the burst and re-enumerate once, rather than
    /// announcing a device and then correcting it as its other interfaces
    /// appear.
    QTimer *m_rescanTimer = nullptr;

    /// Last published view of the world, keyed by UsbDeviceInfo::devicePath, so
    /// a rescan can be diffed into add/remove signals.
    QHash<QString, UsbDeviceInfo> m_known;
};

} // namespace hidcenter
