// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/telemetry/TelemetryHub.h"

#include <QMutexLocker>

namespace hidcenter {

TelemetryHub::TelemetryHub(QObject *parent)
    : QObject(parent)
{
}

TelemetryHub::~TelemetryHub() = default;

TelemetryHub *TelemetryHub::instance()
{
    static TelemetryHub hub;
    return &hub;
}

TelemetryFrame TelemetryHub::frame() const
{
    const QMutexLocker locker(&m_mutex);
    return m_frame;
}

void TelemetryHub::setFrame(const TelemetryFrame &frame)
{
    {
        const QMutexLocker locker(&m_mutex);
        if (m_frame == frame)
            return;

        m_frame = frame;
    }

    // Emitted outside the lock. A queued slot cannot reach back in from another
    // thread, but a direct one on this thread can, and holding the mutex across
    // an arbitrary callback is how that becomes a deadlock.
    Q_EMIT frameChanged();
}

void TelemetryHub::resetFrame()
{
    // Through setFrame() rather than assigning: it is the one publish path, so
    // this gets the dedupe and the emit-outside-the-lock rule for free.
    setFrame(TelemetryFrame());
}

void TelemetryHub::setTestMode(bool testMode)
{
    if (m_testMode == testMode)
        return;

    m_testMode = testMode;

    if (testMode) {
        // Said here rather than left to the router, which is about to close the
        // sockets: nothing is arriving the moment they go.
        setReceiving(false);
    } else {
        // On the way out there is nothing holding the values up. The hand that
        // was setting them has stopped, and no packet has arrived yet.
        //
        // Nothing on the way *in*, on purpose: the details panel composes from
        // the current frame, so hand-editing starts from the car that was just
        // live rather than snapping to the fallback scale the moment the box is
        // ticked.
        resetFrame();
    }

    // Emitted after the reset, so nothing ever sees the new mode beside the old
    // values. The router is on the other end of this, and stops or restarts
    // every source when it arrives.
    Q_EMIT testModeChanged(m_testMode);
}

void TelemetryHub::setReceiving(bool receiving)
{
    if (m_receiving == receiving)
        return;

    m_receiving = receiving;
    Q_EMIT receivingChanged(m_receiving);
}

QString TelemetryHub::listeningProblem() const
{
    const QMutexLocker locker(&m_mutex);
    return m_listeningProblem;
}

void TelemetryHub::setListening(bool listening, const QString &problem)
{
    const QString effective = listening ? QString() : problem;

    {
        const QMutexLocker locker(&m_mutex);
        if (m_listening == listening && m_listeningProblem == effective)
            return;

        m_listeningProblem = effective;
    }

    // Outside the lock, and last: a reader woken by the signal must find the new
    // reason already in place, not the one it replaced.
    m_listening = listening;
    Q_EMIT listeningChanged(m_listening);
}

QString TelemetryHub::activeSource() const
{
    const QMutexLocker locker(&m_mutex);
    return m_activeSource;
}

void TelemetryHub::setActiveSource(const QString &name)
{
    {
        const QMutexLocker locker(&m_mutex);
        if (m_activeSource == name)
            return;

        m_activeSource = name;
    }

    Q_EMIT activeSourceChanged(name);
}

void TelemetryHub::notePacketReceived()
{
    Q_EMIT packetReceived();
}

} // namespace hidcenter
