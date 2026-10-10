// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>

#include "core/usb/UsbDeviceInfo.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// Which way a transfer travelled, from the host's point of view.
enum class TrafficDirection {
    In,          ///< Report received from the device.
    Out,         ///< Report written to the device.
    FeatureGet,  ///< Feature report read from the device.
    FeatureSet,  ///< Feature report written to the device.
};

HIDCENTER_CORE_EXPORT QString trafficDirectionName(TrafficDirection direction);

/// How long a transfer waits for the device before giving up.
///
/// Chosen against the base: it answers a 64-byte interrupt OUT in well under a
/// millisecond, and an LED frame that has not landed in a quarter of a second is
/// not going to. Long enough that a busy bus does not manufacture failures,
/// short enough that a device which has stopped answering does not hold the I/O
/// thread through a whole run of frames.
inline constexpr int kDefaultTransferTimeoutMs = 250;

/// An open handle on one interface of a USB device.
///
/// This is the generic half: the endpoints the interface declares, and the
/// transfers that address them directly. HID's report machinery lives one level
/// down, in IUsbDeviceHidInterface, because an interface can perfectly well have
/// endpoints and no hidraw node — the Alpha Evo's interface 1 is exactly that.
///
/// Every operation reports itself through a signal, so the protocol analyzer can
/// observe all traffic without the device module cooperating or even knowing it
/// exists. Implementations must emit these regardless of whether anything is
/// listening — capture must never change how the device is driven.
///
/// **Threading.** The transfer calls — writeInterrupt(), readInterrupt(), and
/// everything IUsbDeviceHidInterface adds — may be issued from one thread other
/// than the one the object lives on. They are synchronous, and a device that is
/// slow to answer can hold them for a long time, so a module with a worker thread
/// should run them there rather than stalling the GUI. Everything else stays on
/// the owning thread: constructing, destroying and the signals below, which are
/// delivered by queued connection when a transfer is issued from elsewhere.
///
/// One worker at a time. These calls serialise against each other and against the
/// handle being closed, but the interface promises nothing about two threads
/// driving the same device at once, and nothing in hid-center needs it to.
class HIDCENTER_CORE_EXPORT IUsbDeviceInterface : public QObject
{
    Q_OBJECT

public:
    explicit IUsbDeviceInterface(QObject *parent = nullptr) : QObject(parent) {}
    ~IUsbDeviceInterface() override = default;

    virtual bool isOpen() const = 0;

    /// What enumeration said about this interface, endpoints included.
    virtual const UsbInterfaceInfo &interfaceInfo() const = 0;

    /// Send `data` to one interrupt OUT endpoint of this interface.
    ///
    /// `endpoint` is a bEndpointAddress with its direction bit, so 0x02 rather
    /// than 2. The bytes go on the wire exactly as given: this is below the
    /// report layer, and nothing is prepended, stripped or padded.
    ///
    /// Fails when the endpoint is not one of this interface's, is not interrupt
    /// OUT, or cannot be reached by whatever route the implementation has — see
    /// LinuxUsbHidInterface, which can only reach the endpoint the kernel already
    /// sends its output reports on.
    virtual bool writeInterrupt(quint8 endpoint, const QByteArray &data,
                                int timeoutMs = kDefaultTransferTimeoutMs) = 0;

    /// Read one packet from an interrupt IN endpoint of this interface.
    ///
    /// Blocking, up to `timeoutMs`. A handle that delivers its IN traffic through
    /// inputReport() instead — every HID interface does — refuses this rather
    /// than racing its own reader for the same packets.
    virtual bool readInterrupt(quint8 endpoint, QByteArray &out,
                               int timeoutMs = kDefaultTransferTimeoutMs) = 0;

    /// Human-readable reason for the most recent failure.
    virtual QString lastError() const = 0;

Q_SIGNALS:
    /// A transfer went out or came in on one endpoint of this interface.
    ///
    /// What the analyzer taps for traffic that has no report layer to describe
    /// it. `endpoint` carries its direction bit, so the address alone says which
    /// way the bytes went and matches the endpoint list on the Status page.
    void transferred(hidcenter::TrafficDirection direction, quint8 endpoint,
                     const QByteArray &data);

    /// The device went away, or the handle hit an unrecoverable error.
    void disconnected();

    /// A recoverable I/O error, for the status bar and the log.
    void errorOccurred(const QString &message);
};

} // namespace hidcenter

Q_DECLARE_METATYPE(hidcenter::TrafficDirection)
