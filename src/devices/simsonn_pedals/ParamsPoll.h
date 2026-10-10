// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

/// The report-`0x02` exchange: ask for the params, and read what comes back.
///
/// This is the **params request**: it asks for the params report, and the device answers with a 70-byte area whose live
/// raw axis values are what a calibration screen reads. Requesting it is the
/// only way to see the sensors as the device sees them — the axis report gives
/// positions after the device's own calibration has been applied, this gives the
/// counts underneath.
///
/// Three things about it are worth knowing, all established against a Plus X:
///
/// * **The vendor interface is silent until it is asked.** Before the first
///   request it sends nothing at all; after one it streams `02 00` / `02 01`
///   pairs at about 250 Hz — 500 input reports a second — and keeps doing so.
/// * **One request buys exactly 30 seconds of it**, measured to the tenth of a
///   second, and each further request restarts those 30 seconds from scratch.
///   That is the whole reason this class keeps a timer: the params stop unless a
///   host keeps asking, so anything that wants them must resend inside every
///   30 s window. Closing the interface does not stop the stream either — it
///   simply lapses 30 s after whatever request came last.
/// * **The raw values are live without `read_raw`.** calibration.md reads the
///   host's calibration routine as sending the `read_raw` command first, to put
///   the device into a raw mode. It does not appear to be a mode: the values at
///   `+0x440` track the pedals from the first reply, with nothing but this
///   request ever written.
///
/// The request must be padded out to the declared 64-byte report. A
/// bare single byte `0x02` is refused outright by Linux hidraw.
class ParamsPoll : public QObject
{
    Q_OBJECT

public:
    /// How often the request goes out. It only has to beat the device's 30 second
    /// cutoff, so this has a wide margin: one second is used. The cost of a shorter interval is the write itself — the reply
    /// stream runs at its own rate regardless.
    static constexpr int kIntervalMs = 1000;

    /// The params area, in bytes: the 62 of the first reply packet and the 8 of
    /// the second. Byte 0 is config blob offset `0x43C`.
    static constexpr int kParamsSize = 70;

    /// At most one paramsChanged() per this many milliseconds, which is about
    /// 60 Hz. The replies arrive some eight times faster than that and no screen
    /// can use them.
    static constexpr int kUpdateIntervalMs = 16;

    explicit ParamsPoll(IUsbDevice *device, QObject *parent = nullptr);

    /// Claim `interface` and start asking. Does nothing if the device is
    /// offline or the interface is not HID; error() then says which.
    void start(const UsbInterfaceInfo &interface);

    /// Stop asking, let the claim go, and forget the last params rather than
    /// leaving them on screen as though they were still true.
    ///
    /// **The device keeps streaming for up to 30 more seconds**: closing the
    /// interface does not cut it off, it just runs out the window left by the
    /// last request.
    void stop();

    bool isRunning() const { return m_interface != nullptr; }

    /// Why the params are not arriving, or empty when they are (or when nothing
    /// has asked for them).
    QString error() const { return m_error; }

    /// True once a complete reply pair has been assembled. Everything below is
    /// empty until it is.
    bool hasParams() const { return m_haveParams; }

    /// One pedal's live raw sensor count — params `+0x440 + 2*axis`, signed and
    /// little-endian. This is the number the device's own calibration works in.
    std::optional<qint16> rawAxis(int axis) const;

    /// The whole params area, for anything wanting a field this class does not
    /// name. Empty until the first complete pair. Most of it is still undecoded.
    QByteArray params() const;

Q_SIGNALS:
    /// Running or not-running changed, or the reason did.
    void stateChanged();

    /// A fresh params area arrived. Coalesced; see kUpdateIntervalMs.
    void paramsChanged();

private Q_SLOTS:
    void send();
    void onInputReport(const QByteArray &report);
    void onInterfaceGone();

private:
    /// Throw away whatever was assembled, and say so if anything was.
    void forgetParams();

    QPointer<IUsbDevice> m_device;
    QPointer<IUsbDeviceHidInterface> m_interface;
    std::optional<UsbInterfaceInfo> m_interfaceInfo;
    QMetaObject::Connection m_reportConnection;
    QMetaObject::Connection m_goneConnection;

    QTimer *m_timer = nullptr;
    QString m_error;

    /// So a device that refuses every write does not fill the log once a second
    /// for as long as the application runs.
    bool m_writeFailureLogged = false;

    /// The area being assembled, and the one last completed. Kept apart so a
    /// half-arrived pair never shows: readers only ever see whole ones.
    QByteArray m_assembling;
    QByteArray m_params;
    bool m_sawFirstPacket = false;
    bool m_haveParams = false;

    /// Set by a completed pair, cleared by the emit: the coalescing that keeps a
    /// 500 Hz stream from asking for 500 repaints a second.
    bool m_pending = false;
    QTimer *m_throttle = nullptr;
};

} // namespace hidcenter::simsonn_pedals
