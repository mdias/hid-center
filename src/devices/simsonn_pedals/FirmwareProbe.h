// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>

#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

/// Asks the pedals for their firmware version, and nothing else.
///
/// The version is the first field of the device's 1084-byte configuration blob,
/// which is read with report `0x03` in eighteen packets — but the two `uint16`s
/// wanted here are at offsets `0x000` and `0x002`, so they arrive in **packet 1**
/// and the remaining seventeen are never asked for.
///
/// That one round trip is also the only thing in this module that establishes
/// whether the configuration protocol answers at all, which is why the Status
/// page shows the outcome as a condition rather than only as a version number.
///
/// **Asynchronous, and on the GUI thread.** A HID interface delivers its IN
/// traffic through IUsbDeviceHidInterface::inputReport() and refuses a blocking
/// read, so the reply cannot be waited for — hence the signal. The request
/// itself is a synchronous two-byte write, which is short enough not to be worth
/// a worker thread of its own; the moment this module writes anything larger, or
/// writes it repeatedly, it wants one (see AlphaEvoIo for what that looks like).
///
/// The interface is claimed only for as long as the exchange takes. Holding it
/// open costs every input report the kernel delivers on it, and once the version
/// is known there is nothing more to hear.
class FirmwareProbe : public QObject
{
    Q_OBJECT

public:
    explicit FirmwareProbe(IUsbDevice *device, QObject *parent = nullptr);

    /// Send the request. Does nothing when the device is offline or `interface`
    /// has no hidraw node, in which case the probe stays Idle and says why
    /// through error().
    void start(const UsbInterfaceInfo &interface);

    /// Where the exchange got to. Idle covers both "not started" and "there was
    /// nothing to ask": either way no answer is coming, which is what the UI
    /// needs to know, and error() separates the two.
    enum class State { Idle, Waiting, Ready, Failed };

    State state() const { return m_state; }

    /// Empty unless something went wrong.
    QString error() const { return m_error; }

    /// the firmware version and sub-version words, valid only in the Ready state.
    quint16 version() const { return m_version; }
    quint16 versionSub() const { return m_versionSub; }

    /// "2070.204" — the two fields as one string, or an empty string when the
    /// version is not known.
    ///
    /// **Rendered in base 16**, because that is what spells the version out: a
    /// device answering 0x2070 is firmware 2070, not 8304. See the .cpp.
    QString versionString() const;

    /// "0x2070 / 0x0204" — both fields as the raw little-endian words they are,
    /// for a tooltip beside the spelled version. Valid in any state; zero before
    /// an answer.
    QString rawVersion() const;

Q_SIGNALS:
    /// The state changed. Fires on every transition, including a failure, so a
    /// page can simply re-read everything.
    void stateChanged();

private Q_SLOTS:
    /// One IN report on the interface. Ignores everything that is not the
    /// report-`0x03` packet this probe asked for, which is most of it.
    void onInputReport(const QByteArray &report);

    /// Re-send the request, every 250 ms.
    void resend();

    /// Give up: the device never answered within the deadline.
    void expire();

private:
    void finish(State state, const QString &error);

    IUsbDevice *m_device = nullptr;
    IUsbDeviceHidInterface *m_interface = nullptr;
    std::optional<UsbInterfaceInfo> m_interfaceInfo;

    State m_state = State::Idle;
    QString m_error;
    quint16 m_version = 0;
    quint16 m_versionSub = 0;

    QTimer *m_resend = nullptr;
    QTimer *m_deadline = nullptr;
};

} // namespace hidcenter::simsonn_pedals
