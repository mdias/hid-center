// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

#include "core/usb/IUsbDevice.h"
#include "core/usb/UsbDeviceInfo.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// Platform-specific device access, behind one interface.
///
/// Everything OS-specific in this project lives behind this class. The Linux
/// implementation uses libudev plus /dev/hidraw and /dev/bus/usb; a Windows one
/// would use SetupAPI plus hid.dll and WinUSB, and nothing above this layer
/// would change.
///
/// Enumeration is at the level of a *physical device*: implementations must
/// group all interfaces belonging to the same device into a single
/// UsbDeviceInfo. Reporting each interface separately would show one sidebar
/// entry per interface for a device that is really one wheel.
class HIDCENTER_CORE_EXPORT IUsbBackend : public QObject
{
    Q_OBJECT

public:
    explicit IUsbBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~IUsbBackend() override = default;

    virtual QString name() const = 0;

    /// Snapshot of every device currently attached that this backend can see.
    virtual QVector<UsbDeviceInfo> enumerate() = 0;

    /// Build the object a module drives one device through. Never null: opening
    /// individual interfaces is the returned device's business and is where a
    /// permissions problem surfaces, not here.
    virtual std::unique_ptr<IUsbDevice> openDevice(const UsbDeviceInfo &info) = 0;

    /// Begin watching for hotplug events. Returns false when unsupported, in
    /// which case the device list is only refreshed on demand.
    virtual bool startMonitoring() = 0;
    virtual void stopMonitoring() = 0;
    virtual bool isMonitoring() const = 0;

Q_SIGNALS:
    void deviceAdded(const hidcenter::UsbDeviceInfo &device);

    /// `devicePath` is UsbDeviceInfo::devicePath of the device that went away.
    void deviceRemoved(const QString &devicePath);
};

/// Create the backend for the platform this build targets. Returns nullptr when
/// no backend is implemented, which the app surfaces on the Home page rather
/// than treating as fatal.
HIDCENTER_CORE_EXPORT std::unique_ptr<IUsbBackend> createUsbBackend(QObject *parent = nullptr);

} // namespace hidcenter

Q_DECLARE_METATYPE(hidcenter::UsbDeviceInfo)
