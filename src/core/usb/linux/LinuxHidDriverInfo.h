// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include "core/usb/HidDriverInfo.h"

namespace hidcenter {

/// Read what sysfs says about the driver bound to one HID node.
///
/// `hidSysPath` is the HID device's own directory — the one holding the `driver`
/// symlink and the `input/` subtree, e.g.
/// ".../1-9.1.3.1.1:1.0/0003:3670:0500.01D7". The enumerator already has it in
/// hand for the report descriptor, which is why this takes the path rather than
/// deriving it from a hidraw node.
///
/// Internal to the Linux backend: everything above it reads
/// UsbInterfaceInfo::driver, which is where this ends up.
HidDriverInfo readHidDriver(const QString &hidSysPath);

} // namespace hidcenter
