// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/MainWindow.h"

#include "app/DeviceListModel.h"
#include "app/DeviceModule.h"
#include "app/DeviceView.h"
#include "app/HomePage.h"
#include "app/telemetry_page/TelemetryPage.h"
#include "core/Logging.h"
#include "core/usb/IUsbBackend.h"
#include "core/plugin/PluginManager.h"

#include <QItemSelectionModel>
#include <QListView>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>

namespace hidcenter {

MainWindow::MainWindow(PluginManager *plugins, IUsbBackend *backend, TelemetryRouter *telemetry,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_plugins(plugins)
    , m_backend(backend)
    , m_telemetryRouter(telemetry)
{
    buildUi();

    if (m_backend) {
        connect(m_backend, &IUsbBackend::deviceAdded, this, &MainWindow::onDeviceAdded);
        connect(m_backend, &IUsbBackend::deviceRemoved, this, &MainWindow::onDeviceRemoved);

        const QVector<UsbDeviceInfo> devices = m_backend->enumerate();
        m_model->setDevices(devices);
        m_home->setDeviceCount(m_model->deviceCount());

        // Bring every device up now rather than when someone looks at it. This
        // is what makes a wheelbase get its telemetry from the moment it is
        // plugged in — and it costs a probe per device at start-up, on this
        // thread, which is the price of the modules being real rather than
        // waiting to be summoned.
        for (const UsbDeviceInfo &device : devices)
            moduleFor(device, false);
    }

    selectHome();
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("HID Center"));
    resize(1100, 720);

    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_model = new DeviceListModel(m_plugins, this);

    m_sidebar = new QListView(splitter);
    m_sidebar->setModel(m_model);
    m_sidebar->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sidebar->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sidebar->setUniformItemSizes(true);
    m_sidebar->setMinimumWidth(220);
    connect(m_sidebar->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onSelectionChanged);

    m_stack = new QStackedWidget(splitter);

    m_home = new HomePage(m_plugins, m_backend, m_stack);
    connect(m_home, &HomePage::showDisconnectedChanged, this,
            &MainWindow::onShowDisconnectedChanged);
    m_stack->addWidget(m_home);

    m_telemetry = new TelemetryPage(m_telemetryRouter, m_stack);
    m_stack->addWidget(m_telemetry);

    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({240, 860});

    setCentralWidget(splitter);
    statusBar()->showMessage(tr("Ready"));
}

void MainWindow::selectHome()
{
    m_sidebar->setCurrentIndex(m_model->index(DeviceListModel::HomeRow));
}

DeviceModule *MainWindow::moduleFor(const UsbDeviceInfo &device, bool offline)
{
    if (DeviceModule *existing = m_modules.value(device.devicePath))
        return existing;

    IDevicePlugin *plugin = m_plugins ? m_plugins->pluginFor(device.id) : nullptr;
    if (!plugin)
        return nullptr;

    // Withholding the backend is the whole of what makes a module offline: its
    // context then has nothing to open with, which is exactly the truth.
    auto *module = new DeviceModule(offline ? nullptr : m_backend, plugin, device, this);

    // Connected before start(), which is why start() is not the constructor: a
    // module says what it found while initialising, and with no view in the
    // picture there would otherwise be nothing listening.
    connect(module, &DeviceModule::statusMessage, this,
            [this](const QString &message) { statusBar()->showMessage(message, 10000); });

    m_modules.insert(device.devicePath, module);
    module->start();
    return module;
}

DeviceView *MainWindow::viewFor(const UsbDeviceInfo &device, bool offline)
{
    if (DeviceView *existing = m_views.value(device.devicePath))
        return existing;

    // Already there for anything connected; this is what brings up an offline
    // placeholder's module, which is deliberately left until someone looks.
    DeviceModule *module = moduleFor(device, offline);
    if (!module) {
        // The sidebar only lists devices with a module, so this should not
        // happen; log it rather than crashing if it ever does.
        qCWarning(hidcenterApp) << "No module for listed device" << device.id.toString();
        return nullptr;
    }

    auto *view = new DeviceView(module, m_stack);

    m_stack->addWidget(view);
    m_views.insert(device.devicePath, view);
    return view;
}

void MainWindow::onSelectionChanged()
{
    const QModelIndex current = m_sidebar->currentIndex();
    if (!current.isValid()) {
        m_stack->setCurrentWidget(m_home);
        return;
    }

    switch (m_model->kindAt(current.row())) {
    case DeviceListModel::RowKind::Home:
        m_stack->setCurrentWidget(m_home);
        return;
    case DeviceListModel::RowKind::Telemetry:
        m_stack->setCurrentWidget(m_telemetry);
        return;
    case DeviceListModel::RowKind::Device:
        break;
    }

    const UsbDeviceInfo *device = m_model->deviceAt(current.row());
    if (!device) {
        m_stack->setCurrentWidget(m_home);
        return;
    }

    if (DeviceView *view = viewFor(*device, m_model->isOfflineRow(current.row())))
        m_stack->setCurrentWidget(view);
    else
        m_stack->setCurrentWidget(m_home);
}

void MainWindow::onShowDisconnectedChanged(bool show)
{
    m_model->setShowDisconnected(show);

    if (show)
        return;

    // Withdrawing the placeholders leaves their views orphaned, and one of them
    // may be on screen. Tear them down the way an unplug does, and fall back to
    // Home if that is what was showing.
    // Over the modules rather than the views: a placeholder's module is created
    // with its view, but reading the wider set is what keeps this correct if that
    // ever stops being so.
    bool wasShowing = false;
    const QStringList paths = m_modules.keys();
    for (const QString &path : paths) {
        if (!DeviceListModel::isOfflinePath(path))
            continue;
        wasShowing = wasShowing || m_stack->currentWidget() == m_views.value(path);
        discardDevice(path);
    }

    if (wasShowing)
        selectHome();
}

void MainWindow::discardDevice(const QString &devicePath)
{
    if (DeviceView *view = m_views.take(devicePath)) {
        if (m_stack->currentWidget() == view)
            m_stack->setCurrentWidget(m_home);

        m_stack->removeWidget(view);
        // Tear the view down now: its capture would otherwise keep running on a
        // device that may no longer exist.
        view->deleteLater();
    }

    // The module goes with it, always. It holds the open file descriptors and an
    // I/O thread, and keeping it would also mean pages() being called a second
    // time if a view were ever rebuilt — which its contract forbids.
    if (DeviceModule *module = m_modules.take(devicePath))
        module->deleteLater();
}

void MainWindow::onDeviceAdded(const UsbDeviceInfo &device)
{
    m_model->addDevice(device);
    m_home->setDeviceCount(m_model->deviceCount());

    // The real thing has just replaced its placeholder in the sidebar; drop the
    // placeholder's page too, so a stale preview cannot be left on screen beside
    // a device that is now there to be configured.
    const QString placeholder = DeviceListModel::offlinePathFor(device.id);
    const bool wasShowingPlaceholder =
        m_views.contains(placeholder) && m_stack->currentWidget() == m_views.value(placeholder);
    discardDevice(placeholder);

    // Bring it up straight away, exactly as at start-up: a device that has just
    // been plugged in is no less connected than one that already was.
    moduleFor(device, false);

    if (m_plugins && m_plugins->handles(device.id))
        statusBar()->showMessage(tr("Connected: %1").arg(device.describe()), 5000);

    if (wasShowingPlaceholder) {
        const int row = m_model->rowForDevicePath(device.devicePath);
        m_sidebar->setCurrentIndex(row >= 0 ? m_model->index(row)
                                            : m_model->index(DeviceListModel::HomeRow));
    }
}

void MainWindow::onDeviceRemoved(const QString &devicePath)
{
    const bool wasShowing =
        m_views.contains(devicePath) && m_stack->currentWidget() == m_views.value(devicePath);

    // From the module, not the view: most devices now have one without ever
    // having been looked at, and they should still announce themselves leaving.
    if (DeviceModule *module = m_modules.value(devicePath))
        statusBar()->showMessage(tr("Disconnected: %1").arg(module->info().describe()), 5000);

    discardDevice(devicePath);

    m_model->removeDevice(devicePath);
    m_home->setDeviceCount(m_model->deviceCount());

    if (wasShowing)
        selectHome();
}

} // namespace hidcenter
