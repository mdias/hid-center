// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/telemetry_page/TelemetryWindow.h"

#include "app/telemetry_page/TelemetryDetailsPanel.h"

#include <QVBoxLayout>

namespace hidcenter {

TelemetryWindow::TelemetryWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("HID Center — Telemetry"));
    resize(480, 360);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new TelemetryDetailsPanel(this));
}

void TelemetryWindow::showAndRaise()
{
    show();
    // A window that is open but minimised or behind the main window would
    // otherwise appear to do nothing when "Pop out" is pressed again.
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

} // namespace hidcenter
