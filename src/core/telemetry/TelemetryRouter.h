// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVector>

#include "core/telemetry/TelemetryFrame.h"
#include "hidcenter/core_export.h"

class QTimer;

namespace hidcenter {

class ITelemetrySourcePlugin;
class TelemetrySourceModule;

/// How long a source may stay silent before it gives up the floor.
///
/// Generous next to the ~90 Hz a sim delivers at, because the cost of being late
/// to say "no longer receiving" is nothing, and the cost of being early is a
/// readout that flickers between live and idle during an ordinary hitch.
///
/// It is also how long a wheelbase keeps showing the last frame after a game
/// quits, which is the more visible of its two jobs: the values reset when this
/// fires.
inline constexpr int kSourceIdleTimeoutMs = 2000;

/// Decides which loaded telemetry source gets to speak, and publishes it.
///
/// There is one telemetry frame and one Telemetry page, and there may be several
/// sims installed with their senders all running. The rule is first-come and
/// keep-it-while-it-lasts:
///
/// - the first source to deliver a frame becomes the active one;
/// - while it is active, every other source's frames are dropped — they keep
///   listening, they simply do not publish;
/// - after kSourceIdleTimeoutMs of silence it gives up the floor, the values
///   reset, and the next source to deliver takes over.
///
/// That last clause is the whole point of leaving pre-empted sources listening:
/// quitting one game and starting another is meant to hand the wheelbase over
/// within a couple of seconds and without anyone touching a setting.
///
/// **Lives on the GUI thread**, so all of the above is single-threaded and
/// obviously correct. Frames arrive queued from each source's own thread; the
/// only cross-thread hop is that one, and TelemetrySourceModule owns it.
///
/// It is built from the plugin interfaces rather than from
/// TelemetryPluginManager, so nothing here depends on where a source came from —
/// which is also what lets a test hand it two sources built in-process.
class HIDCENTER_CORE_EXPORT TelemetryRouter : public QObject
{
    Q_OBJECT

public:
    explicit TelemetryRouter(const QVector<ITelemetrySourcePlugin *> &plugins,
                             QObject *parent = nullptr);
    ~TelemetryRouter() override;

    /// Bring every source up. Idempotent.
    void start();

    /// Stop and join every source's thread. Idempotent; runs from aboutToQuit
    /// and again from the destructor.
    void shutdown();

    const QVector<TelemetrySourceModule *> &modules() const { return m_modules; }

    /// The source currently feeding the hub, or null when none is.
    TelemetrySourceModule *activeModule() const { return m_active; }

Q_SIGNALS:
    /// The floor changed hands, in either direction. The hub carries the name;
    /// this says when to look again at the rest.
    void activeSourceChanged();

private:
    /// One frame from one source. The arbitration lives here.
    void deliver(TelemetrySourceModule *from, const TelemetryFrame &frame);

    /// The active source has been quiet for too long.
    void onIdleTimeout();

    /// Give up the floor. Says nothing about *why*, which is the caller's to log.
    void releaseFloor();

    /// Recompute "is anything listening at all" and tell the hub.
    void refreshListening();

    void onTestModeChanged(bool testMode);

    QVector<TelemetrySourceModule *> m_modules;
    TelemetrySourceModule *m_active = nullptr;
    QTimer *m_idle = nullptr;
    bool m_started = false;
};

} // namespace hidcenter
