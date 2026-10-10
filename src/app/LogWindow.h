// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

namespace hidcenter {

/// The log in a window of its own.
///
/// It holds a second LogPanel rather than borrowing the Home page's: both are
/// views onto the same LogModel, so nothing is duplicated, and no live widget
/// has to be reparented between a layout and a top-level window — which is
/// where this kind of feature usually goes wrong.
class LogWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LogWindow(QWidget *parent = nullptr);

    /// Show, raise and focus. Used for every press of "Pop out", so a second
    /// press on an already-open window brings it forward instead of doing
    /// nothing visible.
    void showAndRaise();
};

} // namespace hidcenter
