// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/telemetry/TelemetryRouter.h"

#include "core/Logging.h"
#include "core/telemetry/TelemetryHub.h"
#include "core/telemetry/TelemetrySourceModule.h"
#include "core/telemetry/plugin/ITelemetrySourcePlugin.h"
#include "core/telemetry/plugin/TelemetryPluginManager.h"

#include <QCoreApplication>
#include <QStringList>
#include <QTimer>

namespace hidcenter {

TelemetryRouter::TelemetryRouter(const QVector<ITelemetrySourcePlugin *> &plugins, QObject *parent)
    : QObject(parent)
{
    m_idle = new QTimer(this);
    m_idle->setSingleShot(true);
    m_idle->setInterval(kSourceIdleTimeoutMs);
    connect(m_idle, &QTimer::timeout, this, &TelemetryRouter::onIdleTimeout);

    for (ITelemetrySourcePlugin *plugin : plugins) {
        if (!plugin)
            continue;

        auto *module = new TelemetrySourceModule(plugin, this);

        // Captured rather than taken from sender(): the router needs to know
        // *which* source spoke, and that is the whole of the arbitration.
        connect(module, &TelemetrySourceModule::frameReady, this,
                [this, module](const TelemetryFrame &frame) { deliver(module, frame); });
        connect(module, &TelemetrySourceModule::availabilityChanged, this,
                [this] { refreshListening(); });

        m_modules.append(module);
    }

    TelemetryHub *hub = TelemetryHub::instance();
    connect(hub, &TelemetryHub::testModeChanged, this, &TelemetryRouter::onTestModeChanged);

    // Stop before the application tears its widgets down rather than at static
    // destruction time, so a frame is never published into a half-destroyed UI.
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this] { shutdown(); });
}

TelemetryRouter::~TelemetryRouter()
{
    shutdown();
}

void TelemetryRouter::start()
{
    if (m_started)
        return;
    m_started = true;

    for (TelemetrySourceModule *module : m_modules)
        module->start();

    // Said once up front, so the arrow is grey with a reason rather than grey
    // with none while every source is still binding.
    refreshListening();
}

void TelemetryRouter::shutdown()
{
    // Before the threads go: a frame that arrives after this point has nothing
    // left to arrive at, and the idle timer would fire against a dead module.
    m_idle->stop();
    m_active = nullptr;

    for (TelemetrySourceModule *module : m_modules)
        module->shutdown();
}

void TelemetryRouter::deliver(TelemetrySourceModule *from, const TelemetryFrame &frame)
{
    TelemetryHub *hub = TelemetryHub::instance();

    // Test mode owns the values. Sources are suspended when it comes on, but a
    // frame can already be in flight from a source's thread, and hand-set values
    // being overwritten one time in a hundred is exactly the kind of bug nobody
    // ever reproduces.
    if (hub->isTestMode())
        return;

    // The floor is taken and still warm. Not an error and not worth a log line:
    // two senders running at once is a perfectly ordinary thing to have.
    if (m_active && m_active != from)
        return;

    if (!m_active) {
        m_active = from;
        hub->setActiveSource(from->displayName());
        qCInfo(hidcenterTelemetry) << "Telemetry is now coming from" << from->displayName();
        Q_EMIT activeSourceChanged();
    }

    m_idle->start();
    hub->setReceiving(true);

    // Before the frame, as the hub did when it decoded these itself: this says a
    // packet landed, which is true whatever the numbers turn out to be —
    // including when they match the last ones and setFrame() publishes nothing.
    hub->notePacketReceived();
    hub->setFrame(frame);
}

void TelemetryRouter::onIdleTimeout()
{
    if (m_active)
        qCInfo(hidcenterTelemetry) << m_active->displayName()
                                << "stopped sending telemetry; values reset.";

    releaseFloor();

    TelemetryHub *hub = TelemetryHub::instance();

    // Declining to overwrite hand-set values is cheaper than reasoning about
    // whether test mode could have come on between the last frame and this.
    if (!hub->isTestMode())
        hub->resetFrame();
}

void TelemetryRouter::releaseFloor()
{
    m_idle->stop();

    if (!m_active)
        return;

    m_active = nullptr;

    TelemetryHub *hub = TelemetryHub::instance();
    hub->setActiveSource(QString());
    hub->setReceiving(false);

    Q_EMIT activeSourceChanged();
}

void TelemetryRouter::refreshListening()
{
    bool anyListening = false;
    QStringList problems;

    for (const TelemetrySourceModule *module : m_modules) {
        if (module->isAvailable()) {
            anyListening = true;
            continue;
        }

        if (!module->unavailabilityReason().isEmpty()) {
            problems << QStringLiteral("%1: %2").arg(module->displayName(),
                                                     module->unavailabilityReason());
        }
    }

    QString problem;
    if (!anyListening) {
        if (m_modules.isEmpty()) {
            problem = tr("No telemetry source modules were loaded, so nothing can be listened "
                         "for. Searched:\n%1")
                          .arg(TelemetryPluginManager::searchPaths().join(QLatin1Char('\n')));
        } else if (problems.isEmpty()) {
            // Every source is still starting: it has a thread and has not yet
            // reached its bind. Momentary, and not worth inventing a reason for.
            problem = tr("No telemetry source is listening yet.");
        } else {
            problem = problems.join(QLatin1Char('\n'));
        }
    }

    TelemetryHub::instance()->setListening(anyListening, problem);
}

void TelemetryRouter::onTestModeChanged(bool testMode)
{
    // Hand the values over completely rather than merely ignoring what arrives:
    // with every socket closed, a running game cannot fight what is being set by
    // hand, and the ports are free for whatever else might want them.
    for (TelemetrySourceModule *module : m_modules) {
        if (testMode)
            module->suspend();
        else
            module->resume();
    }

    if (testMode) {
        // Nothing is arriving the moment the sockets close, so nothing holds the
        // floor either. The next real frame after test mode ends starts the
        // arbitration over, which is what should happen: two minutes of hand-set
        // values is not a reason for a since-quit game to keep its claim.
        releaseFloor();
    }
}

} // namespace hidcenter
