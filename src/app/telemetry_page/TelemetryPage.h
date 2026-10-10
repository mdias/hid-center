// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace hidcenter {

class TelemetryRouter;
class TelemetryWindow;
class TrafficIndicator;

/// The Telemetry entry in the sidebar: the sim data every device module can
/// read, on two tabs.
///
/// It is application-level rather than per-device because the data is: one car
/// feeds every wheelbase, pedal set and dash plugged into the machine. There is
/// one of these however many telemetry sources are loaded — which is why the
/// header names the one currently being listened to.
class TelemetryPage : public QWidget
{
    Q_OBJECT

public:
    explicit TelemetryPage(TelemetryRouter *router, QWidget *parent = nullptr);

private Q_SLOTS:
    void popOutDetails();

    /// Decide whether the arrow is watching anything at all, and say why when it
    /// is not. Test mode and a port we could not take look the same from the
    /// values alone, and neither should look like a quiet game.
    void refreshTrafficAvailability();

    /// Name the source feeding the frame, or say nothing when none is.
    void refreshActiveSource();

private:
    void buildHeader(QVBoxLayout *layout);

    TelemetryRouter *m_router = nullptr;

    // QPointer so closing the window (which deletes it) leaves nothing dangling
    // for the next press of "Pop out".
    QPointer<TelemetryWindow> m_window;

    TrafficIndicator *m_indicator = nullptr;
    QLabel *m_activeSource = nullptr;
};

} // namespace hidcenter
