// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;

namespace hidcenter {

class RpmBar;
class WheelTempDisplay;

/// The live telemetry values, in detail.
///
/// Several of these can exist at once — the Telemetry page keeps one and the
/// pop-out window another — because they are only views onto the single
/// TelemetryHub. Editing either one moves the other, since neither holds a value
/// of its own.
class TelemetryDetailsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TelemetryDetailsPanel(QWidget *parent = nullptr);

    /// Show the "Pop out" button. Off by default, so the popped-out window does
    /// not offer to pop itself out again.
    void setPopOutVisible(bool visible);

Q_SIGNALS:
    void popOutRequested();

private Q_SLOTS:
    /// Take the hub's current frame. Called for every change, including the ones
    /// this panel caused.
    void refreshFromHub();

    /// Apply the editable state that test mode implies.
    void refreshEditable();

private:
    void buildUi();

    /// Publish what the controls now say. Ignored unless test mode is on: a
    /// read-only panel must not be able to overwrite a measured frame.
    void publish();

    RpmBar *m_rpm = nullptr;
    WheelTempDisplay *m_wheels = nullptr;
    QLabel *m_rpmValue = nullptr;
    QCheckBox *m_testMode = nullptr;
    QCheckBox *m_tractionControl = nullptr;
    QCheckBox *m_abs = nullptr;
    QCheckBox *m_pitLimiter = nullptr;
    QComboBox *m_flag = nullptr;
    QLabel *m_mode = nullptr;
    QWidget *m_popOut = nullptr;
};

} // namespace hidcenter
