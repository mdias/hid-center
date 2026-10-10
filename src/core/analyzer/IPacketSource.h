// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include "core/analyzer/UsbPacket.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// Where captured packets come from.
///
/// The analyzer UI knows only this interface. UsbPacketSource captures what
/// this application exchanges with a device plus every input report the device
/// emits — which is everything hidraw can see, because the kernel does not echo
/// output reports written by other processes.
///
/// Capturing traffic from a *different* application (another program in a VM, say)
/// needs a usbmon-based source reading /dev/usbmon*. That is why this interface
/// exists: such a source drops in here, and the log model, the filters and the
/// UI stay exactly as they are.
class HIDCENTER_CORE_EXPORT IPacketSource : public QObject
{
    Q_OBJECT

public:
    explicit IPacketSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IPacketSource() override = default;

    /// Human-readable name of what is being captured, for the UI.
    virtual QString description() const = 0;

    /// Begin emitting packets. Returns false on failure, with reason available
    /// through lastError().
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual QString lastError() const { return {}; }

Q_SIGNALS:
    void packetCaptured(const hidcenter::UsbPacket &packet);
};

} // namespace hidcenter
