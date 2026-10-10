// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include "core/telemetry/TelemetryFrame.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// One sim's telemetry feed: a socket, a wire format, and the decoding between
/// them.
///
/// **Lives on a thread of its own.** TelemetrySourceModule creates one of these,
/// moves it to a private thread and drives it from there, so start() and stop()
/// never run on the GUI thread and a source is free to block on its own event
/// loop without anyone else noticing.
///
/// **It never touches TelemetryHub.** A source says what it heard and nothing
/// more; which of several loaded sources actually gets to publish is
/// TelemetryRouter's decision, and a source that wrote to the hub directly would
/// walk straight past it. That is also why this header includes TelemetryFrame.h
/// rather than TelemetryHub.h — the hub is not reachable from here at all.
///
/// **It does no idle detection either.** Falling silent is not something a
/// source has to notice, let alone report: the router times the silence, for
/// every source, with one timer and one rule. A source only ever speaks up when
/// it has something.
class HIDCENTER_CORE_EXPORT ITelemetrySource : public QObject
{
    Q_OBJECT

public:
    explicit ITelemetrySource(QObject *parent = nullptr) : QObject(parent) {}
    ~ITelemetrySource() override = default;

    /// Bind, and begin listening. **Runs on this object's own thread**, never on
    /// the GUI thread — a socket has to belong to the thread that will service
    /// its notifier, which is why this exists at all rather than the constructor
    /// doing the work.
    ///
    /// Failure is reported through availabilityChanged(), not a return value: a
    /// port held by something else is a reason to have no telemetry, not a
    /// reason to refuse to load. Calling start() on an already-started source
    /// must do nothing.
    virtual void start() = 0;

    /// Release whatever start() took, freeing the port. Listening resumes on the
    /// next start(); nothing need be remembered in between. Idempotent, and runs
    /// on the same thread as start().
    virtual void stop() = 0;

Q_SIGNALS:
    /// One reading, ready to publish.
    ///
    /// **Emit once per accepted packet, even when the values match the last
    /// ones.** The hub dedupes for the UI's sake, and the router counts these as
    /// traffic: a car parked at a steady idle publishes nothing at all while
    /// packets keep arriving ninety times a second, and a source that deduped
    /// here would look to the router exactly like a game that had quit.
    void frameReady(const hidcenter::TelemetryFrame &frame);

    /// Whether this source holds what it needs to listen, and why not when it
    /// does not.
    ///
    /// `reason` is shown to the user, so name the endpoint and the failure —
    /// "Could not listen on 127.0.0.1:56789 — the address is already in use" is
    /// a sentence someone can act on, and "unavailable" is not.
    void availabilityChanged(bool available, const QString &reason);
};

} // namespace hidcenter
