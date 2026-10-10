// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include "core/usb/UsbDeviceInfo.h"

namespace hidcenter {

class IUsbDevice;
class IDevice;
class IDevicePlugin;
class IUsbBackend;

/// One device's module and the hardware it drives.
///
/// **Alive for as long as the device is, not for as long as anyone is looking at
/// it.** A module polls its device's status, runs an I/O thread and feeds it
/// telemetry, none of which is about being on screen — so it is created when the
/// device is detected and destroyed when the device goes away. A DeviceView is a
/// window onto one of these, and owns none of it.
///
/// Offline placeholders are the exception, and only because there is nothing
/// there to drive: one gets its module when its preview is first opened.
class DeviceModule : public QObject
{
    Q_OBJECT

public:
    /// Withholding the backend is the whole of what makes a module offline: it
    /// then gets an OfflineUsbDevice, which refuses every open — exactly the
    /// truth about a device that is not plugged in.
    DeviceModule(IUsbBackend *backend, IDevicePlugin *plugin, const UsbDeviceInfo &info,
                 QObject *parent = nullptr);
    ~DeviceModule() override;

    /// Create the module and bring it up. Idempotent.
    ///
    /// Separate from the constructor so the owner can connect to statusMessage()
    /// first: initialize() is where a module says what it found, and with no view
    /// in the picture that message would otherwise be emitted into nothing.
    void start();

    const UsbDeviceInfo &info() const { return m_info; }
    IUsbDevice *usbDevice() const { return m_usb; }

    /// What the module's family calls this device, falling back to the raw
    /// description when there is no plugin to ask.
    QString displayName() const;

    /// The module itself. Null when the plugin declined to make one, or when
    /// start() has not run yet.
    IDevice *device() const { return m_device; }

    /// Empty when the module came up. Held rather than shown, because whoever
    /// builds a view later is what turns it into the "Unavailable" tab — and
    /// there may never be one.
    QString startError() const { return m_startError; }

Q_SIGNALS:
    void statusMessage(const QString &message);
    void infoFieldsChanged();

private:
    UsbDeviceInfo m_info;
    IDevicePlugin *m_plugin = nullptr;
    IUsbDevice *m_usb = nullptr;
    IDevice *m_device = nullptr;
    QString m_startError;
    bool m_started = false;
};

} // namespace hidcenter
