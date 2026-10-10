// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QSlider;
class QSpinBox;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoSettings;
class AlphaEvoTelemetry;

/// What this wheelbase is being told about the car.
///
/// Presentation only, like MainSettingsPage: every value shown here comes from
/// the AlphaEvoTelemetry it is given, and this page never converts anything
/// itself. The *Data* group is the report's own contents, field for field, so a
/// conversion can be checked against the sidebar's Telemetry page before a base
/// ever acts on it.
class TelemetryPage : public QWidget
{
    Q_OBJECT

public:
    /// `settings` is read only to say where the reports are going — offline, held
    /// by the dry run, or actually reaching the base. It may be null.
    explicit TelemetryPage(AlphaEvoTelemetry *telemetry, AlphaEvoSettings *settings,
                           QWidget *parent = nullptr);

private Q_SLOTS:
    /// Redraw the Data group from the current values.
    void refreshData();

    void refreshStatus();

private:
    AlphaEvoTelemetry *m_telemetry = nullptr;
    AlphaEvoSettings *m_settings = nullptr;

    QCheckBox *m_enabled = nullptr;
    QLabel *m_status = nullptr;
    QSlider *m_rate = nullptr;
    QSpinBox *m_rateSpin = nullptr;

    /// One per member of SimagicTelemetry.
    QLabel *m_rpm = nullptr;
    QLabel *m_rpmPercent = nullptr;
    QLabel *m_tractionControl = nullptr;
    QLabel *m_abs = nullptr;
    QLabel *m_pitLimiter = nullptr;
    QLabel *m_flag = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
