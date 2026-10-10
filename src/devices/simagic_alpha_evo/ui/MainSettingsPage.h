// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QWidget>

#include "AlphaEvoSettings.h"

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QProgressBar;
class QSlider;
class QSpinBox;
class QTimer;

namespace hidcenter::simagic_alpha_evo {

/// The configuration page for one Alpha Evo.
///
/// Presentation only: every byte of protocol lives in AlphaEvoSettings, and
/// every write goes through its apply(), so the dry-run gate cannot be bypassed
/// from here.
class MainSettingsPage : public QWidget
{
    Q_OBJECT

public:
    /// `probeError` is empty when the device answered. When it is not, the page
    /// explains the failure and disables its controls.
    MainSettingsPage(const QString &modelName, AlphaEvoSettings *settings,
                     const QString &probeError, QWidget *parent = nullptr);

private Q_SLOTS:
    /// Take new values from the device, without disturbing whatever the user is
    /// currently doing.
    void onStatusRefreshed();
    void onReadFailed(const QString &error);

    /// Write everything queued since the last commit.
    void commitPending();

    void onApplyStarted();
    void onApplyFinished(bool ok, const QString &error, int blocksWritten, bool dryRun);

private:
    /// One row. Exactly one of the three shapes is populated, per the spec's
    /// ControlKind. Nothing here enables or disables: the page does that to the
    /// whole content widget at once.
    struct Control
    {
        ControlKind kind = ControlKind::Slider;
        QSlider *slider = nullptr;      ///< Slider only.
        QSpinBox *spin = nullptr;       ///< Slider only.
        QComboBox *combo = nullptr;     ///< Choice only.
        QCheckBox *check = nullptr;     ///< Toggle only.

        /// Current value, whichever shape this is.
        int value() const;
        /// Set without emitting anything.
        void setValueSilently(int value) const;
        /// True while the user is manipulating it, so a poll must not overwrite.
        bool isBusy() const;
    };

    QWidget *buildGroup(const QString &title, const QVector<Setting> &settings);
    void addControl(QFormLayout *form, QWidget *parent, Setting setting);
    void addSlider(QFormLayout *form, QWidget *parent, const SettingSpec &spec);
    void addChoice(QFormLayout *form, QWidget *parent, const SettingSpec &spec);
    void addToggle(QFormLayout *form, QWidget *parent, const SettingSpec &spec);
    void queueChange(Setting setting, int value);
    void setStatus(const QString &message, bool isError = false);

    /// What the status line says when nothing is happening: whether a change
    /// made now would reach the wheelbase.
    ///
    /// Shared by the page's opening state and the dry-run toggle. Two copies of
    /// this sentence is exactly how the page came to open by announcing a dry
    /// run that had stopped being the default.
    QString idleStatusText() const;

    /// Lock the page while a write is in flight, so a second batch cannot be
    /// queued behind the first. Does nothing when the page is not operable at
    /// all, which is how the probe-failure state survives a stray unlock.
    void setBusy(bool busy);

    /// Keep the angle-lock control's upper bound in step with the maximum
    /// rotation, which is what the base clamps it against.
    void updateAngleLockRange();

    AlphaEvoSettings *m_settings = nullptr;
    QHash<Setting, Control> m_controls;

    /// Edits not yet written. Also tells onStatusRefreshed() which controls to
    /// leave alone, so a poll cannot overwrite an edit that is still in flight.
    SettingValues m_pending;

    QTimer *m_commitTimer = nullptr;
    QCheckBox *m_dryRun = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;

    /// Everything inside the scroll area, so all seventeen controls are enabled
    /// and disabled with one call.
    QWidget *m_content = nullptr;

    /// False when the base did not answer, in which case nothing on this page
    /// ever becomes usable.
    bool m_operable = false;
};

} // namespace hidcenter::simagic_alpha_evo
