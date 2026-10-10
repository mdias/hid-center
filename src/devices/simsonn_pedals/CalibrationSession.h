// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <array>
#include <optional>
#include <utility>

#include "ConfigRead.h"
#include "Pedals.h"
#include "core/usb/UsbDeviceInfo.h"

namespace hidcenter {
class IUsbDevice;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

class ParamsPoll;

/// One run of "press every pedal through its travel, then keep it".
///
/// Calibrating a pedal is only ever measuring where its sensor ends: the device
/// stores a raw count for each end of the travel and maps everything between
/// them onto its output. So this watches the live raw values while it is running
/// and remembers the furthest each pedal got in either direction. Nothing is
/// written until save(), and cancel() leaves the device exactly as it was.
///
/// **What save() writes, and does not.** One `<letter>_pf` command per pedal,
/// which carries a pedal's whole state — curve, calibration, deadzones — in a
/// single report. The curve points and both deadzones are copied back out of the
/// blob unchanged, so only the five calibration values move. The effective trio
/// is derived here rather than left to the device, because the device does not
/// derive it: it trusts whatever range it is handed.
///
/// **Two ways in, and they differ in what they leave alone.** start() is the
/// measuring run: it watches every pedal and every one of them ends up
/// calibrated to what it was seen to do. adjustEndpoint() is the other way —
/// somebody dragged a thumb — and it deliberately does *not* start watching, so
/// the pedals nobody touched keep exactly what the device already holds and are
/// not written at all. Both end the same way, at save() or cancel().
///
/// A dragged endpoint also outranks the measurement for its own pedal, for the
/// life of the run. Someone who has taken hold of a thumb is saying where they
/// want it, and a value that then slid out from under them the next time the
/// pedal moved would make the gesture useless.
///
/// **A pedal that never moved is not calibrated to a point.** A disconnected
/// pedal still reports, and reports noise, so a run that took its two or three
/// counts of drift as a travel would leave it pinned — every reading past that
/// sliver would saturate. Anything that moved less than kMovementThreshold is
/// therefore written with the full sensor range instead, which is what the
/// device ships with for an axis nothing is attached to.
///
/// **The write is checked.** A `<letter>_pf` is not acknowledged — the device
/// says nothing back at all — so the only honest way to report success is to
/// read the configuration again and look. That is what happens after the three
/// reports go out, and until it agrees the save is not reported as done.
class CalibrationSession : public QObject
{
    Q_OBJECT

public:
    /// Idle covers both "never run" and "finished"; message() says which.
    enum class State { Idle, Measuring, Saving, Verifying };

    CalibrationSession(IUsbDevice *device, ParamsPoll *params, ConfigRead *config,
                       QObject *parent = nullptr);

    State state() const { return m_state; }
    bool isRunning() const { return m_state != State::Idle; }

    /// What just happened, in one sentence, for the line above the bars. Empty
    /// when there is nothing to say.
    QString message() const { return m_message; }

    /// True when the last thing that happened was a failure, so message() should
    /// be read as a complaint rather than as progress.
    bool failed() const { return m_failed; }

    /// Where each pedal's endpoints are coming from, so the page can say so.
    enum class Source {
        Stored,    ///< Untouched: what the device holds, and what save() leaves alone.
        Measured,  ///< Watched through a travel worth keeping.
        Unmoved,   ///< Watched, but barely moved; save() stores it as unused.
        Edited,    ///< Set by hand, by dragging a thumb.
    };

    /// Begin watching. Needs the stored configuration already read, since a save
    /// has to put the pedal's curve and deadzones back unchanged and they can
    /// only come from there.
    void start(const UsbInterfaceInfo &interface);

    /// Take one pedal's endpoints as given — a thumb was dragged to them.
    ///
    /// Starts a run if none is going, without watching the pedals and without
    /// disturbing the other two, which go on showing what the device holds. The
    /// values are used as they arrive: the bar has already kept them in range and
    /// apart, and second-guessing a deliberate gesture would only make the thumb
    /// fight the mouse.
    void adjustEndpoint(const UsbInterfaceInfo &interface, int axis, int low, int high);

    /// Stop watching and forget everything measured or set by hand. Writes
    /// nothing.
    void cancel();

    /// Write what was measured, then read the configuration back and check it.
    void save(const UsbInterfaceInfo &interface);

    /// The travel measured for one pedal so far, low then high, or nothing
    /// before its first reading.
    std::optional<std::pair<int, int>> measured(int axis) const;

    /// Whether that pedal has moved far enough for the measurement to be used.
    bool moved(int axis) const;

    /// What save() would store for one pedal: what was dragged, or the measured
    /// travel, or the whole sensor range when it did not move, or what the device
    /// already holds when nothing has touched it. sourceFor() says which.
    std::pair<qint16, qint16> endpointsFor(int axis) const;

    /// Which of those four it is.
    Source sourceFor(int axis) const;

Q_SIGNALS:
    /// Anything visible changed — the state, the message, or a measurement.
    void changed();

private Q_SLOTS:
    /// One more look at where the pedals are.
    void observe();

    /// The read-back this save is waiting on has got somewhere.
    void onConfigRead();

private:
    /// One pedal's travel so far.
    struct Travel
    {
        int lowest = 0;
        int highest = 0;
        bool seen = false;
    };

    void finish(const QString &message, bool failed);

    /// Which slot of m_travel and m_edited an axis number belongs to, or -1.
    static int indexOf(int axis);

    /// The physical endpoints the device currently holds for one pedal.
    std::optional<std::pair<int, int>> storedEndpoints(int axis) const;

    /// Send one pedal's `<letter>_pf`. False if the write itself was refused.
    bool writePedal(const PedalSpec &pedal);

    /// Whether the device now holds what save() asked it to.
    bool storedMatches() const;

    QPointer<IUsbDevice> m_device;
    QPointer<ParamsPoll> m_params;
    QPointer<ConfigRead> m_config;

    State m_state = State::Idle;
    QString m_message;
    bool m_failed = false;

    /// Whether this run is watching the pedals. False for one begun by dragging a
    /// thumb: see the note about the two ways in, above.
    bool m_watching = false;

    std::array<Travel, kPedalCount> m_travel;

    /// The endpoints dragged onto a pedal by hand, which outrank both the
    /// measurement and what the device holds.
    std::array<std::optional<std::pair<int, int>>, kPedalCount> m_edited {};

    /// What save() wrote, kept so the read-back can be compared against it.
    std::array<std::pair<qint16, qint16>, kPedalCount> m_written {};

    std::optional<UsbInterfaceInfo> m_interfaceInfo;
};

} // namespace hidcenter::simsonn_pedals
