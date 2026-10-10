// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
struct UsbInterfaceInfo;
}

namespace hidcenter::simagic_alpha_evo {

/// The input report the wheelbase's applied force rides on.
inline constexpr quint8 kForceReportId = 0x01;

/// Where the force field starts, counted from the first byte after the report ID.
///
/// Firmware newer than the first this was written against moved the field: it
/// used to be the last two bytes of the report, and the report has since grown
/// trailing bytes. Found by watching which byte of the report moves constantly
/// on an idle wheel.
inline constexpr int kForceFieldOffset = 38;

/// What the signed 16-bit field is divided by to reach -1.0 … +1.0.
///
/// 32768 rather than 32767, so the negative end lands exactly on -1.0. The
/// positive end then stops a hair inside +1.0, which is the right way round: a
/// scale that could exceed 1.0 would draw outside the graph.
inline constexpr int kForceFullScale = 32768;

/// The force field of input report 0x01, scaled to -1.0 … +1.0.
///
/// **Two bytes at a fixed offset** (kForceFieldOffset), little-endian and signed,
/// rather than an offset worked out from the report descriptor. The field is a
/// device-specific addition to a gamepad report and the descriptor is very unlikely to
/// name it, so there is nothing to look it up by.
///
/// Both the position and the sign are asserted here rather than derived from
/// anything. If the trace comes out mirrored, or reading some other field, **this
/// function is the whole of what has to change** — which is why it is free, pure
/// and on its own.
///
/// `usesReportIds` says whether byte 0 is the report ID or the first byte of the
/// payload; a descriptor that declares no IDs puts payload there. Returns false
/// when the report is not 0x01, or is too short to reach the field.
bool decodeForceSample(const QByteArray &report, bool usesReportIds, qreal *out);

/// Why the force trace has, or has not, got something to read.
enum class ForceTraceSource {
    Available,     ///< An interface declares report 0x01 and can be read when asked.
    NoSuchReport,  ///< No interface declares an input report 0x01.
    Offline,       ///< No hardware behind this page at all.
    DeviceGone,    ///< There was a wheel and it was unplugged.
    OpenFailed,    ///< The interface is there and would not open. Usually permissions.
};

/// The wheelbase's applied force, as the UI sees it.
///
/// A listener and a decode, and nothing else. It writes nothing and starts no
/// thread: input reports are read off the hidraw descriptor by a QSocketNotifier
/// on this very thread the moment an interface is open — see LinuxHidDevice — so
/// the whole cost of the feature is one connection and two bytes of arithmetic
/// per report.
///
/// **It opens the interface itself, and closes it again.** setEnabled() claims
/// the interface carrying report 0x01 while it is reading and lets go when it
/// stops, so nothing here depends on the Protocol Analyzer having been started
/// first. Letting go matters as much as claiming: while an interface is open the
/// kernel delivers every report on it whether or not anyone decodes them, so
/// merely disconnecting the signal would leave a kilohertz stream running for the
/// rest of the session.
///
/// The claim is shared, not exclusive. The analyzer may be reading the same
/// interface, and the configuration protocol may *be* on it; whoever else holds
/// it keeps it, and this never takes it away from them. See
/// IUsbDevice::openFor().
class AlphaEvoForceTrace : public QObject
{
    Q_OBJECT

public:
    /// `device` may be offline, in which case this reports Offline for ever and
    /// does nothing at all.
    explicit AlphaEvoForceTrace(IUsbDevice *device, QObject *parent = nullptr);

    /// Whether the interface is being held open and read. **Off by default**,
    /// because a kilohertz of anything deserves to be asked for.
    ///
    /// On, this claims the interface and connects. Off, it disconnects and lets
    /// go, which is what actually stops the reading rather than merely stopping
    /// the decoding.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    ForceTraceSource sourceState() const { return m_state; }

    /// Whether the page should let the trace be switched on at all. False for a
    /// device that has no such report, an offline preview, and a wheel that has
    /// been unplugged.
    bool isPossible() const;

    /// One sentence for the page, saying what the trace is drawing or why it is
    /// not. Empty when there is a source and the graph speaks for itself.
    QString sourceDescription() const;

Q_SIGNALS:
    /// One decoded sample, in -1.0 … +1.0, at whatever rate the device reports.
    ///
    /// Direct-connected into TraceGraph::addSample(), which only buffers — so
    /// nothing downstream of this signal is reached at sample rate.
    void forceSampled(qreal value);

    /// The source appeared, went away, or was never there.
    void sourceStateChanged(hidcenter::simagic_alpha_evo::ForceTraceSource state);

private Q_SLOTS:
    void onInputReport(const QByteArray &report);

    /// The wheel went away. Stops and lets go, and the page unticks its box —
    /// there is nothing left to read and nothing about to bring it back.
    void onDeviceLost();

private:
    /// Claim the interface and connect. Sets the state to OpenFailed and returns
    /// false if it will not open.
    bool attach();

    /// Disconnect and let go of the claim.
    void detach();

    void setState(ForceTraceSource state);

    /// The interface declaring input report 0x01, or nullptr. From enumeration
    /// data, so it needs nothing open.
    const UsbInterfaceInfo *forceInterface() const;

    QPointer<IUsbDevice> m_usbDevice;

    /// The handle being read. Not owned: IUsbDevice owns it and may destroy
    /// it once nothing holds a claim, hence QPointer rather than a bare one.
    QPointer<IUsbDeviceHidInterface> m_device;

    QMetaObject::Connection m_reportConnection;
    QMetaObject::Connection m_deviceGoneConnection;

    ForceTraceSource m_state = ForceTraceSource::NoSuchReport;
    bool m_enabled = false;
    bool m_usesReportIds = false;
};

} // namespace hidcenter::simagic_alpha_evo
