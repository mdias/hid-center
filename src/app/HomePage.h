// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include <QPointer>

class QLabel;

namespace hidcenter {

class IUsbBackend;
class LogWindow;
class PluginManager;

/// The landing page: what the application found, what to do when it found
/// nothing, and the switches that only make sense while developing.
class HomePage : public QWidget
{
    Q_OBJECT

public:
    HomePage(PluginManager *plugins, IUsbBackend *backend, QWidget *parent = nullptr);

public Q_SLOTS:
    /// Refresh the counts after a hotplug event.
    void setDeviceCount(int connected);

Q_SIGNALS:
    /// The debug option of the same name was toggled. The sidebar acts on it;
    /// this page only offers the switch.
    void showDisconnectedChanged(bool show);

private Q_SLOTS:
    void popOutLog();

private:
    QString statusHtml() const;

    PluginManager *m_plugins = nullptr;
    IUsbBackend *m_backend = nullptr;
    QLabel *m_status = nullptr;
    int m_deviceCount = 0;

    // QPointer so closing the window (which deletes it) leaves nothing dangling
    // for the next press of "Pop out".
    QPointer<LogWindow> m_logWindow;
};

} // namespace hidcenter
