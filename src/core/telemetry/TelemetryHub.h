// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMutex>
#include <QObject>
#include <QString>

#include <atomic>

#include "core/telemetry/TelemetryFrame.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// The one telemetry frame the whole application shares.
///
/// A singleton on the LogModel precedent, and for the same reason: both the
/// application and every device module need it, and threading it through
/// IUsbDevice would put a sim-racing concept into the interface documented as
/// "a device module's only handle on hardware". It works across the .so boundary
/// only because hidcenter-core is a shared library — the same reason the interfaces
/// do.
///
/// **It knows nothing about any sim, and owns no threads.** Frames arrive from
/// telemetry source modules, each a plugin with its own socket and its own
/// thread, and TelemetryRouter decides which of them is the one being listened
/// to. Everything below is either read by consumers or written by the router.
///
/// **It is still safe to read from any thread.** The router publishes from the
/// GUI thread today, but a device module may well sample from its own I/O
/// thread, so the frame stays behind a mutex and the flags stay atomic. A
/// consumer needs to know none of this: signals are delivered to GUI objects by
/// queued connection whichever thread emitted them.
class HIDCENTER_CORE_EXPORT TelemetryHub : public QObject
{
    Q_OBJECT

public:
    /// Never null, unlike LogModel::instance(): it is a function-local static,
    /// so a module can use it without knowing anything about start-up order.
    static TelemetryHub *instance();

    /// A copy, not a reference into the hub. Another thread may be writing the
    /// frame, so handing out a pointer to it would be handing out a race.
    TelemetryFrame frame() const;

    /// Publish a frame. Emits nothing when it matches the current one, so a
    /// source that repeats itself costs the UI nothing.
    void setFrame(const TelemetryFrame &frame);

    /// Put the frame back to its defaults, because nothing is driving the values
    /// any more. Zero is the honest reading once a game has quit — a stale one
    /// that still looks live would leave a wheelbase's shift lights burning.
    void resetFrame();

    /// While set, the frame is whatever a user dialled in by hand rather than
    /// anything measured. It exists so the telemetry-driven parts of the UI can
    /// be worked on — and demonstrated — with no game running.
    ///
    /// It also silences every source: the router closes their sockets while this
    /// holds, so a running game cannot fight the values being set by hand.
    bool isTestMode() const { return m_testMode; }
    void setTestMode(bool testMode);

    /// Whether frames are arriving from a real source. Set by the router when a
    /// source claims the floor, and cleared when it falls silent.
    bool isReceiving() const { return m_receiving; }
    void setReceiving(bool receiving);

    /// Whether *any* loaded source is listening.
    ///
    /// Deliberately not the same question as isReceiving(): this one is "we are
    /// listening", that one is "something is actually arriving". A quiet
    /// listener and a port we never got hold of look identical from the frame
    /// alone, and they mean very different things.
    bool isListening() const { return m_listening; }

    /// Why nothing is listening, when nothing is; empty while something is.
    ///
    /// It comes from the router because only the router can compose it: with
    /// several sources loaded there is no single reason, and the page showing
    /// this cannot know which of them failed or how.
    QString listeningProblem() const;

    void setListening(bool listening, const QString &problem = QString());

    /// The name of the source currently feeding the frame — "rFactor 2 / Le Mans
    /// Ultimate" — or empty when none is.
    QString activeSource() const;
    void setActiveSource(const QString &name);

    /// Say that one telemetry packet was accepted.
    ///
    /// The router is not the hub and cannot emit for it, and this is worth a
    /// signal of its own rather than a use of frameChanged(), because that one
    /// is deduped: a car parked at a steady idle publishes nothing at all while
    /// packets keep arriving ninety times a second. Anything wanting to show
    /// *traffic* rather than *values* has to watch packetReceived() instead.
    void notePacketReceived();

Q_SIGNALS:
    void frameChanged();
    void testModeChanged(bool testMode);
    void receivingChanged(bool receiving);
    void listeningChanged(bool listening);
    void activeSourceChanged(const QString &name);

    /// One telemetry packet was accepted. See notePacketReceived().
    void packetReceived();

private:
    explicit TelemetryHub(QObject *parent = nullptr);
    ~TelemetryHub() override;

    mutable QMutex m_mutex;  ///< Guards m_frame, m_listeningProblem and m_activeSource.
    TelemetryFrame m_frame;
    QString m_listeningProblem;
    QString m_activeSource;

    // Atomic rather than under the mutex: these are read far more often than
    // they are written — a device module checks isReceiving() on every tick —
    // and keeping them out of the lock means that costs nothing.
    std::atomic<bool> m_testMode = false;
    std::atomic<bool> m_receiving = false;
    std::atomic<bool> m_listening = false;
};

} // namespace hidcenter
