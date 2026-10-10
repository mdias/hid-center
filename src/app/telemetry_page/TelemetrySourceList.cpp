// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/telemetry_page/TelemetrySourceList.h"

#include "core/telemetry/TelemetryHub.h"
#include "core/telemetry/TelemetryRouter.h"
#include "core/telemetry/TelemetrySourceModule.h"
#include "core/telemetry/plugin/TelemetryPluginManager.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace hidcenter {

TelemetrySourceList::TelemetrySourceList(TelemetryRouter *router, QWidget *parent)
    : QWidget(parent)
    , m_router(router)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *heading = new QLabel(tr("<b>Telemetry sources</b>"), this);
    layout->addWidget(heading);

    const QVector<TelemetrySourceModule *> modules =
        m_router ? m_router->modules() : QVector<TelemetrySourceModule *>();

    if (modules.isEmpty()) {
        // Not a failure worth an error colour: a build with no source module is
        // a perfectly coherent thing, it just cannot hear anything.
        auto *empty = new QLabel(
            tr("No telemetry source modules are loaded, so nothing can be listened for. "
               "They are looked for in:\n%1")
                .arg(TelemetryPluginManager::searchPaths().join(QLatin1Char('\n'))),
            this);
        empty->setWordWrap(true);
        empty->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(empty);
        layout->addStretch(1);
        return;
    }

    for (TelemetrySourceModule *module : modules)
        addRow(layout, module);

    layout->addStretch(1);

    auto *note = new QLabel(
        tr("Every source listens at once. The first to send takes over, keeps the feed "
           "while it is sending, and gives it up after a couple of seconds of silence — "
           "so quitting one game and starting another hands the wheelbase over on its own."),
        this);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    // The floor changing hands is the router's news; whether a source can listen
    // at all is each module's.
    connect(m_router, &TelemetryRouter::activeSourceChanged, this, &TelemetrySourceList::refresh);
    for (TelemetrySourceModule *module : modules) {
        connect(module, &TelemetrySourceModule::availabilityChanged, this,
                &TelemetrySourceList::refresh);
    }

    // Test mode stops every source, which changes what each row should say
    // without any of them reporting anything.
    connect(TelemetryHub::instance(), &TelemetryHub::testModeChanged, this,
            &TelemetrySourceList::refresh);

    refresh();
}

void TelemetrySourceList::addRow(QVBoxLayout *layout, TelemetrySourceModule *module)
{
    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);

    auto *rowLayout = new QVBoxLayout(frame);
    rowLayout->setContentsMargins(12, 10, 12, 10);
    rowLayout->setSpacing(4);

    auto *name = new QLabel(QStringLiteral("<b>%1</b>").arg(module->displayName().toHtmlEscaped()),
                            frame);
    rowLayout->addWidget(name);

    auto *state = new QLabel(frame);
    state->setWordWrap(true);
    rowLayout->addWidget(state);

    m_states.insert(module, state);
    layout->addWidget(frame);
}

void TelemetrySourceList::refresh()
{
    if (!m_router)
        return;

    const bool testMode = TelemetryHub::instance()->isTestMode();

    for (TelemetrySourceModule *module : m_router->modules()) {
        QLabel *state = m_states.value(module);
        if (!state)
            continue;

        if (testMode) {
            // Said before availability, because it is the reason for it: the
            // router closed these sockets, and a source reporting itself
            // unavailable here would be reporting the user's own doing back at
            // them as a fault.
            state->setText(tr("Stopped while test mode is on."));
            continue;
        }

        if (m_router->activeModule() == module)
            state->setText(tr("Active — this is the feed shown below."));
        else if (module->isAvailable())
            state->setText(tr("Listening. Waiting for something to arrive."));
        else if (!module->unavailabilityReason().isEmpty())
            state->setText(module->unavailabilityReason());
        else
            state->setText(tr("Not listening."));
    }
}

} // namespace hidcenter
