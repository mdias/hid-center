// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QMainWindow>

#include "core/usb/UsbDeviceInfo.h"

class QListView;
class QStackedWidget;

namespace hidcenter {

class DeviceListModel;
class DeviceModule;
class DeviceView;
class HomePage;
class IUsbBackend;
class PluginManager;
class TelemetryPage;
class TelemetryRouter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// `telemetry` is passed straight through to the Telemetry page and used for
    /// nothing else here. Null is tolerated, as it is for the other two.
    MainWindow(PluginManager *plugins, IUsbBackend *backend, TelemetryRouter *telemetry,
               QWidget *parent = nullptr);

private Q_SLOTS:
    void onSelectionChanged();
    void onDeviceAdded(const hidcenter::UsbDeviceInfo &device);
    void onDeviceRemoved(const QString &devicePath);

    /// Show or hide the placeholders for devices we support but cannot see.
    void onShowDisconnectedChanged(bool show);

private:
    void buildUi();
    void selectHome();

    /// Drop a device's view *and its module* and forget both. Used when hardware
    /// is unplugged and when a placeholder is withdrawn.
    ///
    /// The two go together deliberately: IDevice::pages() is contracted to be
    /// called once, so a module that outlived its view would have it called
    /// again the next time one was built.
    void discardDevice(const QString &devicePath);

    /// Bring up the module for a device we can see, and remember it. Idempotent.
    ///
    /// Called as soon as a device is detected, not when it is looked at: a module
    /// polls its device, runs an I/O thread and feeds it telemetry, none of which
    /// waits on a window.
    ///
    /// `offline` means there is no hardware: the module gets no backend, which is
    /// what makes its context refuse every transfer.
    DeviceModule *moduleFor(const UsbDeviceInfo &device, bool offline);

    /// Build the view for a device the first time it is selected, and reuse it
    /// afterwards so a running capture survives switching away and back.
    DeviceView *viewFor(const UsbDeviceInfo &device, bool offline);

    PluginManager *m_plugins = nullptr;
    IUsbBackend *m_backend = nullptr;
    TelemetryRouter *m_telemetryRouter = nullptr;

    DeviceListModel *m_model = nullptr;
    QListView *m_sidebar = nullptr;
    QStackedWidget *m_stack = nullptr;
    HomePage *m_home = nullptr;
    TelemetryPage *m_telemetry = nullptr;

    /// One per device we can see, alive from detection to unplug. Views come and
    /// go against these, never the other way round.
    QHash<QString, DeviceModule *> m_modules;

    QHash<QString, DeviceView *> m_views;
};

} // namespace hidcenter
