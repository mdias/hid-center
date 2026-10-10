// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <array>
#include <optional>

#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

/// Reads the device's whole 1084-byte configuration blob, once.
///
/// The blob is everything the pedals are configured with — calibration
/// endpoints, deadzones, response curves, pin and LED assignments — and it comes
/// back in eighteen packets of report `0x03`, the host asking for each one by
/// number.
///
/// **Host-paced, so strictly one packet at a time.** The device answers the
/// packet it was asked for and nothing else, which is what makes a dropped
/// answer recoverable: the same request goes out again until it is answered or
/// the whole read times out. Running the eighteen in sequence takes about a
/// third of a second on a Plus X, including while the params stream is flooding
/// the same interface at 500 reports a second — the two do not disturb each
/// other, since every reply carries the report id it belongs to.
///
/// This is deliberately *not* FirmwareProbe, which reads packet 1 alone for the
/// version and is started for every device at detection. Keeping them apart
/// keeps the device header's firmware row from depending on seventeen further
/// round trips that only the Calibration page needs.
///
/// **Read-only.** Writing the blob back is report `0x04`, and is ConfigWrite's
/// job; patchAxis() is how the settings that only live in the blob get into it.
class ConfigRead : public QObject
{
    Q_OBJECT

public:
    /// The configuration blob is 1084 bytes — `0x43C`.
    static constexpr int kBlobSize = 0x43C;

    /// How many axes the blob carries. The Plus X uses the first three.
    static constexpr int kAxisCount = 8;

    /// The largest filter strength: `Filter` is three bits.
    static constexpr int kFilterMaximum = 7;

    /// How many points an axis's response curve has: points 0 to 10, at
    /// evenly spaced inputs across the travel.
    static constexpr int kCurvePoints = 11;

    /// One axis's stored calibration, as the device holds it.
    ///
    /// Two ranges, and the difference matters. The **physical** pair is what a
    /// calibration measures: the raw counts the sensor gives at each end of its
    /// travel. The **effective** trio is that range with the deadzones already
    /// subtracted, and is what the device maps to 0 % and 100 % output — the
    /// host derives it and the device trusts it, so the two can disagree on a
    /// device whose deadzones were changed by something that did not recompute.
    struct AxisCalibration
    {
        qint16 physicalMinimum = 0;   ///< physical minimum.
        qint16 physicalMaximum = 0;   ///< physical maximum.
        qint16 effectiveMinimum = 0;  ///< effective minimum, where the device reports 0 %.
        qint16 effectiveCentre = 0;   ///< effective centre.
        qint16 effectiveMaximum = 0;  ///< effective maximum, where the device reports 100 %.
        quint8 startDeadzone = 0;     ///< start deadzone size, percent of the physical span.
        quint8 endDeadzone = 0;       ///< end deadzone size, likewise, measured inward.
    };

    explicit ConfigRead(IUsbDevice *device, QObject *parent = nullptr);

    /// Ask for the blob. Does nothing once a read has been started, so a page
    /// that is shown and hidden and shown again does not re-read: nothing here
    /// writes to the device, so what it holds cannot go stale underneath us.
    void start(const UsbInterfaceInfo &interface);

    /// Read it again, keeping the interface the first read used.
    ///
    /// Only for after something has been written: nothing this application does
    /// changes the blob otherwise, and re-reading on every glance would be
    /// eighteen round trips for an answer that cannot have moved.
    void refresh();

    /// Idle covers both "not started" and "there was nothing to ask"; error()
    /// separates the two.
    enum class State { Idle, Reading, Ready, Failed };

    State state() const { return m_state; }

    /// Empty unless something went wrong.
    QString error() const { return m_error; }

    /// How far the read has got, as a packet count out of eighteen. For saying
    /// "still reading" with something behind it.
    int packetsRead() const { return m_nextPacket - 1; }
    int packetCount() const;

    /// The blob itself, valid only in the Ready state.
    const QByteArray &blob() const { return m_blob; }

    /// One axis's calibration, or nothing before the blob has been read.
    std::optional<AxisCalibration> axis(int index) const;

    /// One axis's response curve: eleven values in -100..100, at inputs evenly
    /// spaced across the travel. Nothing before the blob has been read.
    std::optional<std::array<qint8, kCurvePoints>> curve(int index) const;

    /// One axis's input filter strength, 0..kFilterMaximum: `Filter`, the top
    /// three bits of the axis flag byte. 0 is no filtering. Nothing before the blob has been read.
    std::optional<int> filter(int index) const;

    /// Replace one axis's curve, calibration, deadzones and filter in `blob`,
    /// which must be a whole blob. Everything else in it — the rest of the axis
    /// flag byte included — is left as it was.
    static void patchAxis(QByteArray &blob, int index,
                          const std::array<qint8, kCurvePoints> &curve,
                          const AxisCalibration &calibration, int filter);

Q_SIGNALS:
    /// The state changed, or another packet arrived. Fires often enough during a
    /// read for a progress line to follow it.
    void stateChanged();

private Q_SLOTS:
    void onInputReport(const QByteArray &report);
    void resend();
    void expire();

private:
    void request(int packet);
    void finish(State state, const QString &error);

    IUsbDevice *m_device = nullptr;
    IUsbDeviceHidInterface *m_interface = nullptr;
    std::optional<UsbInterfaceInfo> m_interfaceInfo;

    State m_state = State::Idle;
    QString m_error;

    QByteArray m_blob;

    /// The packet being waited for. Packets are numbered from one.
    int m_nextPacket = 1;

    QTimer *m_resend = nullptr;
    QTimer *m_deadline = nullptr;
};

} // namespace hidcenter::simsonn_pedals
