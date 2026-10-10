// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/IUsbBackend.h"

#include "core/Logging.h"

#ifdef HIDCENTER_BACKEND_LINUX
#    include "core/usb/linux/LinuxUsbBackend.h"
#endif

namespace hidcenter {

std::unique_ptr<IUsbBackend> createUsbBackend(QObject *parent)
{
#ifdef HIDCENTER_BACKEND_LINUX
    return std::make_unique<LinuxUsbBackend>(parent);
#else
    // No backend for this platform yet. The app stays usable — it simply lists
    // no devices — and the Home page explains why.
    Q_UNUSED(parent);
    qCWarning(hidcenterHid, "No USB backend is implemented for this platform.");
    return {};
#endif
}

} // namespace hidcenter
