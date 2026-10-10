// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/DeviceView.h"

#include "app/AnalyzerTab.h"
#include "core/DeveloperMode.h"
#include "app/DeviceModule.h"
#include "app/TrafficIndicator.h"
#include "core/Logging.h"
#include "core/analyzer/UsbPacketSource.h"
#include "core/usb/IUsbDevice.h"
#include "core/plugin/IDevice.h"
#include "core/plugin/IDevicePlugin.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace hidcenter {

DeviceView::DeviceView(DeviceModule *module, QWidget *parent)
    : QWidget(parent)
    , m_module(module)
    , m_context(module ? module->usbDevice() : nullptr)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // The header first, so it exists whatever happens to the module below.
    buildHeader();
    layout->addLayout(m_headerLayout);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);

    if (m_module)
        connect(m_module, &DeviceModule::infoFieldsChanged, this, &DeviceView::refreshInfoFields);

    // No source at all for a device that is not there. One would sit idle
    // anyway, but not starting it is what makes "nothing is being read from this
    // device" a fact about the code rather than a hope about the timing.
    if (m_context && !m_context->isOffline()) {
        // Observe-only: the arrows must report what the application does with
        // this device, not add to it. Opening interfaces is the analyzer's
        // business.
        m_source = new UsbPacketSource(m_context, this);
        m_source->setOpensAllInterfaces(false);
        m_source->start();
        connect(m_source, &UsbPacketSource::packetCaptured, this,
                [this](const UsbPacket &packet) { m_indicator->noteTraffic(packet.direction); });
        connect(m_context, &IUsbDevice::interfaceOpened, this,
                &DeviceView::refreshTrafficAvailability);
        connect(m_context, &IUsbDevice::deviceLost, this,
                &DeviceView::refreshTrafficAvailability);
    }

    addModulePages();

    refreshInfoFields();
    refreshTrafficAvailability();

    // The analyzer goes last, and is added even when the module failed to
    // initialize — a device that will not talk to us is precisely when a
    // protocol analyzer earns its keep.
    if (developerMode())
        m_tabs->addTab(new AnalyzerTab(m_context, m_tabs), tr("Protocol Analyzer"));
}

const UsbDeviceInfo &DeviceView::info() const
{
    static const UsbDeviceInfo kNothing;
    return m_module ? m_module->info() : kNothing;
}

void DeviceView::buildHeader()
{
    m_headerLayout = new QHBoxLayout;
    m_headerLayout->setContentsMargins(8, 6, 8, 6);

    // The name belongs here rather than on a page: it identifies what every tab
    // is acting on, so it has to survive switching between them.
    const QString name = m_module ? m_module->displayName() : QString();
    auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped()), this);
    title->setObjectName(QStringLiteral("deviceName"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    title->setFont(titleFont);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_headerLayout->addWidget(title);

    // Beside the name: whether anything is being said to this device, and by
    // whom. Starts unavailable; refreshTrafficAvailability() decides from here.
    m_indicator = new TrafficIndicator(TrafficIndicator::Kind::HidReports, this);
    m_headerLayout->addSpacing(8);
    m_headerLayout->addWidget(m_indicator, 0, Qt::AlignVCenter);

    m_headerLayout->addStretch(1);

    m_infoLayout = new QHBoxLayout;
    m_infoLayout->setSpacing(16);
    m_headerLayout->addLayout(m_infoLayout);
}

void DeviceView::refreshInfoFields()
{
    if (!m_infoLayout)
        return;

    // Rebuild rather than update in place: a module is free to publish a
    // different set of fields each time, and there are only ever a handful.
    while (QLayoutItem *item = m_infoLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    IDevice *device = m_module ? m_module->device() : nullptr;
    if (!device)
        return;

    for (const DeviceInfoField &field : device->infoFields()) {
        auto *label = new QLabel(this);
        label->setText(QStringLiteral("<span style='color:palette(text);'>%1</span>&nbsp;&nbsp;<b>%2</b>")
                           .arg(field.label.toHtmlEscaped(), field.value.toHtmlEscaped()));
        if (!field.tooltip.isEmpty())
            label->setToolTip(field.tooltip);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_infoLayout->addWidget(label);
    }
}

void DeviceView::refreshTrafficAvailability()
{
    if (!m_indicator)
        return;

    if (!m_context || m_context->isOffline()) {
        m_indicator->setAvailable(
            false, tr("This device is not connected, so nothing is sent to it or read "
                      "from it. Its settings can still be changed; the changes are kept "
                      "here only."));
        return;
    }

    // Two ways to have nothing to show: no source running, or a source with no
    // interface open to observe. The second is the ordinary case for a module
    // whose probe failed, and the tooltip should say so rather than implying the
    // indicator itself is broken.
    if (!m_source || !m_source->isRunning()) {
        m_indicator->setAvailable(false, tr("Traffic cannot be observed on this device: %1")
                                             .arg(m_source ? m_source->lastError() : QString()));
        return;
    }

    if (m_context->openInterfaces().isEmpty()) {
        m_indicator->setAvailable(
            false, tr("No interface of this device is open, so there is no traffic to show."));
        return;
    }

    m_indicator->setAvailable(true);
}

void DeviceView::addModulePages()
{
    IDevice *device = m_module ? m_module->device() : nullptr;

    // The module came up long before this view did, so its failure is something
    // to render rather than to discover. The message was logged and shown in the
    // status bar when it happened; this is where it becomes a tab, for whoever
    // eventually comes looking.
    const QString error = m_module ? m_module->startError() : QString();
    if (!device || !error.isEmpty()) {
        QString text = tr("<b>This device could not be initialised.</b><p>%1</p>")
                           .arg(error.toHtmlEscaped());
        if (developerMode())
            text += tr("<p>The Protocol Analyzer tab still works and may help diagnose the "
                       "problem.</p>");
        auto *failure = new QLabel(text, m_tabs);
        failure->setWordWrap(true);
        failure->setAlignment(Qt::AlignCenter);
        failure->setMargin(24);
        m_tabs->addTab(failure, tr("Unavailable"));
        return;
    }

    const QVector<DevicePage> pages = device->pages();
    for (const DevicePage &page : pages) {
        if (!page.widget) {
            qCWarning(hidcenterApp) << "Module page" << page.title << "has no widget; skipping";
            continue;
        }
        // Ownership of the widget passes to the tab widget here.
        m_tabs->addTab(page.widget, page.title);
    }
}

} // namespace hidcenter
