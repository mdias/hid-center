// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include "core/telemetry/TelemetryFrame.h"
#include "hidcenter/core_export.h"

class QThread;

namespace hidcenter {

class ITelemetrySource;
class ITelemetrySourcePlugin;

/// One loaded telemetry source, alive from start-up to quit.
///
/// The analogue of DeviceModule: it owns the object a plugin built, keeps it
/// running, and re-emits what it says so nothing upstream has to reach through
/// to a plugin's object. Where DeviceModule owns an IUsbDevice, this one owns
/// a **thread**.
///
/// **One thread per source, not one shared between them.** A source blocks on a
/// socket, and a module that blocked badly would take every other sim's feed
/// down with it if they shared an event loop. The cost is one idle thread per
/// loaded source, which is nothing next to that.
class HIDCENTER_CORE_EXPORT TelemetrySourceModule : public QObject
{
    Q_OBJECT

public:
    explicit TelemetrySourceModule(ITelemetrySourcePlugin *plugin, QObject *parent = nullptr);
    ~TelemetrySourceModule() override;

    QString sourceId() const;
    QString displayName() const;

    /// Build the source, give it a thread, and ask it to listen. Idempotent.
    ///
    /// Not the constructor, for DeviceModule's reason: the owner connects to the
    /// signals below in between, and a source that reported a bad port from its
    /// constructor would say it to nobody.
    void start();

    /// Stop listening without giving up the thread — test mode came on, and the
    /// port should be free for whatever else might want it.
    void suspend();

    /// Listen again. Paired with suspend(); does nothing if start() has not run.
    void resume();

    /// Stop the source, join its thread and destroy both. Idempotent, because it
    /// runs from the router's shutdown and again from this destructor.
    void shutdown();

    /// Whether the source says it is listening. False until it says otherwise,
    /// which is the honest reading before start() has got that far.
    bool isAvailable() const { return m_available; }

    /// Why it is not, when it is not. Empty while it is.
    QString unavailabilityReason() const { return m_reason; }

Q_SIGNALS:
    /// The source's own signal, forwarded. Arrives here queued from the source's
    /// thread and leaves on the thread this module lives on, which is what makes
    /// the router's arbitration single-threaded.
    void frameReady(const hidcenter::TelemetryFrame &frame);

    void availabilityChanged(bool available, const QString &reason);

private:
    void onAvailabilityChanged(bool available, const QString &reason);

    ITelemetrySourcePlugin *m_plugin = nullptr;
    ITelemetrySource *m_source = nullptr;
    QThread *m_thread = nullptr;

    bool m_started = false;
    bool m_available = false;
    QString m_reason;
};

} // namespace hidcenter
