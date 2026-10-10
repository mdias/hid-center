// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <array>
#include <optional>

class QTimer;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
class ReportDescriptor;
struct UsbInterfaceInfo;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

/// The input report the pedal positions arrive in, on the generic-desktop
/// interface rather than the vendor one: three 16-bit axes, about 500 times a
/// second.
inline constexpr quint8 kAxisReportId = 0x01;

/// Where the pedals are, right now.
///
/// A listener and a decode, nothing more: it claims the axis interface while
/// enabled, converts each axis to a percentage of its declared travel, and says
/// so. It writes nothing and asks the device for nothing — the pedals send this
/// report unprompted.
///
/// **Off until asked for.** The report arrives at about 500 Hz, which is far
/// more often than a graph can usefully be redrawn, so positionsChanged() is
/// coalesced to kUpdateIntervalMs. Enabling this is the whole cost of reading
/// the pedals, which is why the settings page only does it while it is visible.
class PedalInput : public QObject
{
    Q_OBJECT

public:
    /// Axes in the report, in the order it carries them. The descriptor declares
    /// three and names none of them.
    static constexpr int kAxisCount = 3;

    /// At most one update per this many milliseconds, which is about 60 Hz.
    static constexpr int kUpdateIntervalMs = 16;

    /// `device` may be offline, in which case this reads nothing, for ever.
    explicit PedalInput(IUsbDevice *device, QObject *parent = nullptr);

    bool isEnabled() const { return m_enabled; }

    /// On claims the axis interface and starts reading; off lets go, and forgets
    /// the positions rather than leaving the last ones on screen as though they
    /// were still true.
    void setEnabled(bool enabled);

    /// Whether this device has an axis report to read at all. False makes every
    /// position() empty whatever is done to it.
    bool isAvailable() const;

    /// One axis's position as a percentage of its full declared travel (0..100),
    /// or nothing when that axis has not been read.
    std::optional<double> position(int axis) const;

    /// One sentence saying why there are no positions. Empty when there are.
    QString unavailableReason() const;

Q_SIGNALS:
    /// At least one axis moved. Coalesced; see kUpdateIntervalMs.
    void positionsChanged();

private Q_SLOTS:
    void onInputReport(const QByteArray &report);
    void onInterfaceGone();

private:
    bool attach();
    void detach();

    /// The HID interface whose input report 0x01 carries the axes, or nullptr.
    const UsbInterfaceInfo *axisInterface() const;

    /// The axis field's logical range, read from `descriptor` when it declares
    /// one. Leaves the current range alone when it does not.
    void readRange(const ReportDescriptor &descriptor);

    QPointer<IUsbDevice> m_usbDevice;
    QPointer<IUsbDeviceHidInterface> m_interface;
    QMetaObject::Connection m_reportConnection;
    QMetaObject::Connection m_goneConnection;

    bool m_enabled = false;
    bool m_openFailed = false;
    bool m_interfaceLost = false;

    /// The declared logical range of an axis. The Plus X declares
    /// -32767..32767 and rests at the minimum, so a released pedal reads 0 %.
    double m_logicalMin = -32767.0;
    double m_logicalMax = 32767.0;

    std::array<std::optional<double>, kAxisCount> m_positions;

    /// Set by a report, cleared by the emit: the coalescing that keeps a 500 Hz
    /// report from asking for 500 repaints a second.
    bool m_pending = false;
    QTimer *m_throttle = nullptr;
};

} // namespace hidcenter::simsonn_pedals
