// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/telemetry_page/TelemetryPage.h"

#include "app/TrafficIndicator.h"
#include "app/telemetry_page/TelemetryDetailsPanel.h"
#include "app/telemetry_page/TelemetrySourceList.h"
#include "app/telemetry_page/TelemetryWindow.h"
#include "core/telemetry/TelemetryHub.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace hidcenter {

TelemetryPage::TelemetryPage(TelemetryRouter *router, QWidget *parent)
    : QWidget(parent)
    , m_router(router)
{
    auto *layout = new QVBoxLayout(this);

    buildHeader(layout);

    auto *tabs = new QTabWidget(this);

    // Sources come first because they are the one a glance wants when something
    // is wrong: what is arriving, and from where.
    tabs->addTab(new TelemetrySourceList(m_router, tabs), tr("Sources"));

    auto *details = new TelemetryDetailsPanel(tabs);
    details->setPopOutVisible(true);
    connect(details, &TelemetryDetailsPanel::popOutRequested, this,
            &TelemetryPage::popOutDetails);
    tabs->addTab(details, tr("Details"));

    tabs->setCurrentWidget(details);

    layout->addWidget(tabs);
}

void TelemetryPage::buildHeader(QVBoxLayout *layout)
{
    // Shaped after DeviceView's header, down to the margins and the 1.3× title,
    // so a device page and this one read as the same application.
    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 6, 8, 6);

    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Telemetry")), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    title->setFont(titleFont);
    header->addWidget(title);

    // One arrow, not two: telemetry arrives and nothing is ever sent back to a
    // sim. It is also the only thing on this page that can tell a parked car
    // from a closed game — the values are identical, the traffic is not.
    m_indicator = new TrafficIndicator(TrafficIndicator::Kind::TelemetryPackets, this);
    header->addSpacing(8);
    header->addWidget(m_indicator, 0, Qt::AlignVCenter);

    // Which of the loaded sources the arrow is counting. Empty rather than
    // "none" when nothing is arriving: the greyed-out arrow beside it already
    // says that, and a permanent label saying nothing is happening is noise.
    m_activeSource = new QLabel(this);
    m_activeSource->setEnabled(false);
    header->addSpacing(8);
    header->addWidget(m_activeSource, 0, Qt::AlignVCenter);

    header->addStretch(1);
    layout->addLayout(header);

    TelemetryHub *hub = TelemetryHub::instance();

    // Per accepted packet, and possibly from a source's own thread, so this
    // arrives queued.
    connect(hub, &TelemetryHub::packetReceived, this,
            [this] { m_indicator->noteTraffic(TrafficDirection::In); });

    connect(hub, &TelemetryHub::listeningChanged, this,
            &TelemetryPage::refreshTrafficAvailability);
    connect(hub, &TelemetryHub::testModeChanged, this,
            &TelemetryPage::refreshTrafficAvailability);
    connect(hub, &TelemetryHub::activeSourceChanged, this, &TelemetryPage::refreshActiveSource);

    refreshTrafficAvailability();
    refreshActiveSource();
}

void TelemetryPage::refreshTrafficAvailability()
{
    const TelemetryHub *hub = TelemetryHub::instance();

    if (hub->isListening()) {
        m_indicator->setAvailable(true);
        return;
    }

    // Test mode first: it is the reason we are not listening whenever it is on,
    // and it is the one the user just caused themselves.
    if (hub->isTestMode()) {
        m_indicator->setAvailable(
            false, tr("Test mode is on, so nothing is being listened for. The values below "
                      "are yours rather than a game's."));
        return;
    }

    // The reason comes from the router, which is the only thing that can compose
    // one: with several sources loaded there is no single failure, and this page
    // knows neither which of them broke nor how.
    const QString problem = hub->listeningProblem();
    m_indicator->setAvailable(
        false,
        problem.isEmpty() ? tr("Nothing is listening for telemetry. The log says why.") : problem);
}

void TelemetryPage::refreshActiveSource()
{
    m_activeSource->setText(TelemetryHub::instance()->activeSource());
}

void TelemetryPage::popOutDetails()
{
    if (!m_window) {
        m_window = new TelemetryWindow(this);
        // Free it on close; the QPointer above then reads null and the next
        // press builds a fresh one.
        m_window->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_window->showAndRaise();
}

} // namespace hidcenter
