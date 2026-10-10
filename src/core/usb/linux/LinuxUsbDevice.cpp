// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/linux/LinuxUsbDevice.h"

#include "core/usb/linux/LinuxUsbHidInterface.h"
#include "core/usb/linux/LinuxUsbInterface.h"

namespace hidcenter {

std::unique_ptr<IUsbDeviceInterface> LinuxUsbDevice::createInterface(
    const UsbInterfaceInfo &interfaceInfo, QString *error)
{
    if (interfaceInfo.isHid()) {
        auto handle = std::make_unique<LinuxUsbHidInterface>(interfaceInfo);
        if (!handle->isOpen()) {
            if (error)
                *error = handle->lastError();
            return {};
        }
        return handle;
    }

    auto handle = std::make_unique<LinuxUsbInterface>(interfaceInfo, info().busNumber,
                                                      info().deviceNumber);
    if (!handle->isOpen()) {
        if (error)
            *error = handle->lastError();
        return {};
    }
    return handle;
}

} // namespace hidcenter
