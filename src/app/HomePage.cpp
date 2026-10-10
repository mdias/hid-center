// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/HomePage.h"

#include "app/LogPanel.h"
#include "app/LogWindow.h"
#include "core/DeveloperMode.h"
#include "core/usb/IUsbBackend.h"
#include "core/plugin/PluginManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace hidcenter {

HomePage::HomePage(PluginManager *plugins, IUsbBackend *backend, QWidget *parent)
    : QWidget(parent)
    , m_plugins(plugins)
    , m_backend(backend)
{
    auto *layout = new QVBoxLayout(this);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter);

    auto *top = new QWidget(splitter);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);

    // Switches for working on the application itself. They live on the landing
    // page, above the diagnostics they change the meaning of, rather than behind
    // a menu the application does not have.
    auto *debugBox = new QGroupBox(tr("Debug options"), top);
    auto *debugLayout = new QVBoxLayout(debugBox);

    auto *showDisconnected = new QCheckBox(tr("View disconnected devices"), debugBox);
    showDisconnected->setToolTip(
        tr("List every device our modules support, plugged in or not, so their pages can be "
           "worked on without the hardware. A device that is not there is never written to "
           "or read from; its settings can still be changed, and the changes are kept here "
           "only."));
    connect(showDisconnected, &QCheckBox::toggled, this, &HomePage::showDisconnectedChanged);
    debugLayout->addWidget(showDisconnected);

    // Built either way, so the toggle's signal stays wired; only shown with --dev.
    debugBox->setVisible(developerMode());
    topLayout->addWidget(debugBox);

    m_status = new QLabel(top);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    // The scroll area stretches its widget to fill; without this the content
    // sits vertically centred with a gap above it.
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *scroll = new QScrollArea(top);
    scroll->setWidget(m_status);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    topLayout->addWidget(scroll, 1);

    splitter->addWidget(top);

    // The log lives here rather than behind a menu because the most important
    // thing it carries — what a dry-run write would have sent to a device — is
    // useless if the user has to go looking for it.
    auto *logBox = new QGroupBox(tr("Log"), splitter);
    auto *logLayout = new QVBoxLayout(logBox);
    auto *logPanel = new LogPanel(logBox);
    logPanel->setPopOutVisible(true);
    connect(logPanel, &LogPanel::popOutRequested, this, &HomePage::popOutLog);
    logLayout->addWidget(logPanel);
    splitter->addWidget(logBox);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    setDeviceCount(0);
}

void HomePage::popOutLog()
{
    if (!m_logWindow) {
        m_logWindow = new LogWindow(this);
        // Free it on close; the QPointer above then reads null and the next
        // press builds a fresh one.
        m_logWindow->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_logWindow->showAndRaise();
}

void HomePage::setDeviceCount(int connected)
{
    m_deviceCount = connected;
    m_status->setText(statusHtml());
}

QString HomePage::statusHtml() const
{
    QString html;

    html += QStringLiteral("<h3>%1</h3>").arg(tr("Devices"));
    if (m_deviceCount > 0) {
        html += QStringLiteral("<p>%1</p>")
                    .arg(tr("%1 supported device(s) connected.").arg(m_deviceCount));
    } else {
        html += QStringLiteral("<p>%1</p>").arg(tr("No supported device is connected."));
        html += QStringLiteral("<p>%1</p>")
                    .arg(tr("If a device is plugged in but not listed, it may not be readable "
                            "by your user. Install the udev rules and replug it:"));
        html += QStringLiteral(
            "<pre>sudo cp udev/70-hid-center.rules /etc/udev/rules.d/\n"
            "sudo udevadm control --reload-rules</pre>");
    }

    html += QStringLiteral("<h3>%1</h3>").arg(tr("HID backend"));
    if (m_backend) {
        html += QStringLiteral("<p>%1</p>")
                    .arg(tr("Using the <b>%1</b> backend, hotplug detection %2.")
                             .arg(m_backend->name(),
                                  m_backend->isMonitoring() ? tr("active") : tr("unavailable")));
    } else {
        html += QStringLiteral("<p>%1</p>")
                    .arg(tr("No HID backend is available for this platform, so no devices can "
                            "be detected."));
    }

    html += QStringLiteral("<h3>%1</h3>").arg(tr("Device modules"));
    if (m_plugins && !m_plugins->plugins().isEmpty()) {
        html += QStringLiteral("<ul>");
        for (const PluginManager::LoadedPlugin &plugin : m_plugins->plugins()) {
            QStringList ids;
            for (const UsbDeviceId &id : plugin.supportedIds)
                ids << id.toString();
            html += QStringLiteral("<li><b>%1</b> — %2</li>")
                        .arg(plugin.familyName.toHtmlEscaped(),
                             ids.join(QStringLiteral(", ")).toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
    } else {
        html += QStringLiteral("<p>%1</p>")
                    .arg(tr("No device modules were loaded, so no device can be configured. "
                            "Searched:"));
        html += QStringLiteral("<pre>%1</pre>")
                    .arg(PluginManager::searchPaths().join(QLatin1Char('\n')).toHtmlEscaped());
    }

    if (m_plugins && !m_plugins->rejected().isEmpty()) {
        html += QStringLiteral("<h3>%1</h3>").arg(tr("Modules that were rejected"));
        html += QStringLiteral("<ul>");
        for (const PluginManager::RejectedPlugin &rejected : m_plugins->rejected()) {
            html += QStringLiteral("<li><code>%1</code> — %2</li>")
                        .arg(rejected.fileName.toHtmlEscaped(), rejected.reason.toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
    }

    return html;
}

} // namespace hidcenter
