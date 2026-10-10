// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

// Not HidDriverInfo.h: the definition of UsbDeviceInfo is what is needed here,
// and that header includes this one.
#include "core/usb/UsbDeviceInfo.h"

#include <QSet>

namespace hidcenter {

HidDriverInfo forceFeedbackDriverFor(const UsbDeviceInfo &device)
{
    HidDriverInfo fallback;
    QSet<QString> seen;

    for (const UsbInterfaceInfo &interface : device.interfaces) {
        if (!interface.driver.known)
            continue;

        // Several hidraw interfaces can hang off one HID node, and that node
        // gives the same answer however many times it is asked.
        const QString node = interface.hid.hidSysPath;
        if (!node.isEmpty()) {
            if (seen.contains(node))
                continue;
            seen.insert(node);
        }

        if (interface.driver.forceFeedback)
            return interface.driver;

        // Keep the first real answer, so a caller can say *what* is bound
        // instead — "hid-generic is bound and does not provide force feedback"
        // is a great deal more useful than "none".
        if (!fallback.known)
            fallback = interface.driver;
    }

    return fallback;
}

} // namespace hidcenter
