// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include "hidcenter/core_export.h"

namespace hidcenter {

struct UsbDeviceInfo;

/// Which kernel driver is bound to a HID node, and whether it gives that node
/// force feedback.
///
/// Some wheelbases do not declare force feedback through the standard HID PID
/// descriptors, so the kernel offers none unless an out-of-tree driver is bound.
/// Whether one is loaded is invisible from the protocol itself — the base still
/// answers everything else perfectly — so the only way to know is to ask the OS.
struct HIDCENTER_CORE_EXPORT HidDriverInfo
{
    /// False when there was nothing to look at: no sysfs path, an offline
    /// preview, or a platform without this layout.
    ///
    /// Deliberately distinct from "looked, and found nothing bound", which is a
    /// real answer and an alarming one. A caller that conflates the two tells a
    /// user with no device that their driver is missing.
    bool known = false;

    /// The driver bound to the node — "simagic-ff", "hid-generic", and so on.
    /// Empty while `known` is true means the node exists and nothing is bound.
    ///
    /// Note this is the name the driver registers on the HID bus, which is not
    /// the module name: `hid_simagic_ff` registers as `simagic-ff`.
    QString driver;

    /// Whether the kernel actually exposes force-feedback effects here.
    ///
    /// This is the honest question, and it is why nothing in here keeps a list
    /// of driver names that count: a driver can be bound and still deliver no
    /// force feedback, and a name alone cannot tell you which happened.
    bool forceFeedback = false;

    friend bool operator==(const HidDriverInfo &a, const HidDriverInfo &b)
    {
        return a.known == b.known && a.driver == b.driver
            && a.forceFeedback == b.forceFeedback;
    }
    friend bool operator!=(const HidDriverInfo &a, const HidDriverInfo &b) { return !(a == b); }
};

/// The driver giving one physical device force feedback: the capable interface
/// if there is one, and otherwise whatever is bound instead, so a caller can
/// name the driver that is there rather than only say what is not.
///
/// Pure — it reads UsbInterfaceInfo::driver, which the backend filled in during
/// enumeration, and touches nothing else. A backend that fills in nothing leaves
/// every interface `known == false` and this returns the same, which is how a
/// platform with no answer says so rather than reporting a missing driver.
HIDCENTER_CORE_EXPORT HidDriverInfo forceFeedbackDriverFor(const UsbDeviceInfo &device);

} // namespace hidcenter
