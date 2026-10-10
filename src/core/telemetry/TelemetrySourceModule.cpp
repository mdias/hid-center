// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/telemetry/TelemetrySourceModule.h"

#include "core/Logging.h"
#include "core/telemetry/plugin/ITelemetrySourcePlugin.h"

#include <QMetaObject>
#include <QThread>

namespace hidcenter {

TelemetrySourceModule::TelemetrySourceModule(ITelemetrySourcePlugin *plugin, QObject *parent)
    : QObject(parent)
    , m_plugin(plugin)
{
}

TelemetrySourceModule::~TelemetrySourceModule()
{
    shutdown();
}

QString TelemetrySourceModule::sourceId() const
{
    return m_plugin ? m_plugin->sourceId() : QString();
}

QString TelemetrySourceModule::displayName() const
{
    return m_plugin ? m_plugin->displayName() : QString();
}

void TelemetrySourceModule::start()
{
    if (m_started)
        return;
    m_started = true;

    if (!m_plugin)
        return;

    m_source = m_plugin->createSource();
    if (!m_source) {
        qCWarning(hidcenterTelemetry) << m_plugin->displayName() << "declined to create a source";
        return;
    }

    // Queued both ways by construction: the source is about to move to another
    // thread, and this module stays on the one that built it.
    connect(m_source, &ITelemetrySource::frameReady, this, &TelemetrySourceModule::frameReady);
    connect(m_source, &ITelemetrySource::availabilityChanged, this,
            &TelemetrySourceModule::onAvailabilityChanged);

    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("telemetry-%1").arg(m_plugin->sourceId()));

    // The source has no parent, and must not: a QObject cannot be moved to
    // another thread while it has one. This module owns it by hand instead, and
    // destroys it in shutdown().
    m_source->moveToThread(m_thread);
    m_thread->start();

    resume();
}

void TelemetrySourceModule::suspend()
{
    if (!m_source)
        return;

    QMetaObject::invokeMethod(
        m_source, [source = m_source] { source->stop(); }, Qt::QueuedConnection);
}

void TelemetrySourceModule::resume()
{
    if (!m_source)
        return;

    QMetaObject::invokeMethod(
        m_source, [source = m_source] { source->start(); }, Qt::QueuedConnection);
}

void TelemetrySourceModule::shutdown()
{
    if (!m_thread) {
        // start() may have failed before there was a thread, but there can still
        // be a source to drop.
        delete m_source;
        m_source = nullptr;
        return;
    }

    ITelemetrySource *source = m_source;
    m_source = nullptr;

    // Before anything else: a frame already queued from the source's thread must
    // not land on a router that has stopped expecting one.
    if (source)
        source->disconnect(this);

    // Close the socket and destroy the source on the thread that owns them,
    // while that thread is still running, rather than deleting it from here once
    // it has stopped. Both are the same rule: the descriptor this thread would
    // close is one the source was last seen in recv() on, and the object it
    // would destroy is one the source was last seen reading. A join in between
    // is not the same thing as the work happening where it belongs, and a thread
    // sanitizer is right to say so.
    if (source && m_thread->isRunning()) {
        QMetaObject::invokeMethod(
            source,
            [source] {
                source->stop();
                delete source;
            },
            Qt::BlockingQueuedConnection);
    } else {
        delete source;
    }

    m_thread->quit();
    m_thread->wait();

    delete m_thread;
    m_thread = nullptr;

    // Nothing is listening now, and something may still ask.
    m_available = false;
}

void TelemetrySourceModule::onAvailabilityChanged(bool available, const QString &reason)
{
    m_available = available;
    m_reason = available ? QString() : reason;

    Q_EMIT availabilityChanged(m_available, m_reason);
}

} // namespace hidcenter
