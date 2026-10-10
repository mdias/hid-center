// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>

#include "core/usb/IUsbDeviceInterface.h"
#include "core/usb/ReportDescriptor.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// An open handle on one HID interface — one /dev/hidrawN node on Linux.
///
/// Everything here is the report layer the kernel's HID stack puts on top of the
/// interface's endpoints: input reports arriving on their own, output reports
/// written, and feature reports exchanged over the control pipe. An interface
/// only gets one of these when a HID driver is bound to it and a node exists;
/// see UsbHidInfo::present.
///
/// The threading contract is IUsbDeviceInterface's and governs every call below.
class HIDCENTER_CORE_EXPORT IUsbDeviceHidInterface : public IUsbDeviceInterface
{
    Q_OBJECT

public:
    explicit IUsbDeviceHidInterface(QObject *parent = nullptr) : IUsbDeviceInterface(parent) {}
    ~IUsbDeviceHidInterface() override = default;

    /// Write an output report. Byte 0 must be the report ID when the descriptor
    /// declares IDs, and 0x00 when it does not.
    ///
    /// The kernel decides how this reaches the device: an interrupt OUT on the
    /// interface's own OUT endpoint when it has one, and a control SET_REPORT
    /// when it does not. writeInterrupt() is how to say which of those you meant.
    virtual bool write(const QByteArray &report) = 0;

    /// Read a feature report, sizing the transfer from the report descriptor.
    /// `out` comes back with the report ID as byte 0.
    virtual bool getFeatureReport(quint8 reportId, QByteArray &out) = 0;

    /// Read a feature report of an explicitly known size.
    ///
    /// Vendor protocols routinely use feature reports the descriptor never
    /// declares, leaving nothing to derive a length from. Pass the size the
    /// protocol specifies. Declared as its own overload rather than a defaulted
    /// argument, because default arguments on virtuals bind to the static type
    /// and would silently differ between base and override.
    virtual bool getFeatureReport(quint8 reportId, int length, QByteArray &out) = 0;

    /// Write a feature report. Byte 0 must be the report ID.
    virtual bool setFeatureReport(const QByteArray &report) = 0;

    virtual const ReportDescriptor &descriptor() const = 0;

Q_SIGNALS:
    /// An input report arrived from the device.
    void inputReport(const QByteArray &report);

    /// This handle successfully wrote an output report.
    void reportWritten(const QByteArray &report);

    /// A feature report was read or written through this handle.
    void featureExchanged(hidcenter::TrafficDirection direction, const QByteArray &report);
};

} // namespace hidcenter
