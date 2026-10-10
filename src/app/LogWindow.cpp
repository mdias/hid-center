// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/LogWindow.h"

#include "app/LogPanel.h"

#include <QVBoxLayout>

namespace hidcenter {

LogWindow::LogWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("HID Center — Log"));
    resize(900, 460);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new LogPanel(this));
}

void LogWindow::showAndRaise()
{
    show();
    // A window that is open but minimised or behind the main window would
    // otherwise appear to do nothing when "Pop out" is pressed again.
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

} // namespace hidcenter
