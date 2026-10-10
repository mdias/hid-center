// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

namespace hidcenter {

/// The telemetry details in a window of their own, so they can sit beside a
/// device page instead of taking its place.
///
/// It holds a second TelemetryDetailsPanel rather than borrowing the page's:
/// both are views onto the same TelemetryHub, so nothing is duplicated, and no
/// live widget has to be reparented between a layout and a top-level window —
/// which is where this kind of feature usually goes wrong. Same reasoning as
/// LogWindow.
class TelemetryWindow : public QWidget
{
    Q_OBJECT

public:
    explicit TelemetryWindow(QWidget *parent = nullptr);

    /// Show, raise and focus. Used for every press of "Pop out", so a second
    /// press on an already-open window brings it forward instead of doing
    /// nothing visible.
    void showAndRaise();
};

} // namespace hidcenter
