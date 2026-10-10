// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/usb/UsbDeviceBase.h"

namespace hidcenter {

/// One physical USB device on Linux.
///
/// Which of the two routes an interface gets is decided by what the kernel has
/// already done with it, never by what the caller asks for:
///
/// - an interface with a hidraw node opens as a LinuxUsbHidInterface over
///   /dev/hidrawN, and reaches its interrupt OUT endpoint through the HID stack
///   that is already bound to it;
/// - an interface with no node opens as a LinuxUsbInterface over
///   /dev/bus/usb/BBB/DDD, which has to claim the interface and so detaches
///   whatever kernel driver holds it.
///
/// The split is what makes the second safe. A driver that gave the kernel a
/// hidraw node is a driver something may be using — simagic-ff, giving a
/// wheelbase its force feedback — and it is never the one detached, because an
/// interface it is bound to takes the first route by construction.
class LinuxUsbDevice final : public UsbDeviceBase
{
    Q_OBJECT

public:
    explicit LinuxUsbDevice(const UsbDeviceInfo &info, QObject *parent = nullptr)
        : UsbDeviceBase(info, parent)
    {
    }

    bool isOffline() const override { return false; }

protected:
    std::unique_ptr<IUsbDeviceInterface> createInterface(const UsbInterfaceInfo &interfaceInfo,
                                                         QString *error) override;
};

} // namespace hidcenter
