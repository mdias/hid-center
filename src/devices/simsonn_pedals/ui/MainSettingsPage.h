// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QWidget>

#include <optional>

#include "../SettingsSave.h"
#include "core/usb/UsbDeviceInfo.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace hidcenter::simsonn_pedals {

class BrakeOptionsRead;
class ConfigRead;
class CurveEditor;
class PedalInput;
class RangeSlider;

/// What each pedal does with its travel: the response curve, the deadzones at
/// either end of it and how much its input is filtered — and, on the brake, its
/// maximum force, the elastomer fitted and the point at which it cuts the
/// throttle.
///
/// **What is shown is what the pedals hold, until it is edited.** Both controls
/// are loaded from the device's own configuration blob, and follow it whenever
/// it is read again — but only while nothing on the page has been changed.
/// Until the blob arrives the controls show neutral defaults and say so.
///
/// **Edits are local until saved.** The moment the controls stop matching the
/// device, *Save Settings* and *Cancel* come alive: saving writes every pedal
/// that differs through SettingsSave and checks the result, cancelling puts the
/// controls back to what the device holds. With nothing changed there is nothing
/// to save or throw away, and both stay disabled.
class MainSettingsPage : public QWidget
{
    Q_OBJECT

public:
    /// `pedals`, `config`, `brakeOptions` and `save` may each be null, and may have nothing to
    /// read; either way the page builds and says what it is missing. None is
    /// owned here — all outlive this page, so closing and reopening the tab does
    /// not disturb them or make the blob be read again.
    MainSettingsPage(const QString &modelName,
                     const std::optional<UsbInterfaceInfo> &vendorInterface,
                     PedalInput *pedals, ConfigRead *config,
                     BrakeOptionsRead *brakeOptions, SettingsSave *save,
                     QWidget *parent = nullptr);

    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// A response curve was edited. Nothing is written to the device until
    /// *Save Settings* is pressed; this is for surfacing the change.
    void curveChanged(const QString &pedalName);

protected:
    void resizeEvent(QResizeEvent *event) override;

    /// The axis report arrives about 500 times a second, so it is read only
    /// while this page is actually on screen. A tab the user switched away from
    /// has no business holding an interface open.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refreshPositions();

    /// Take what the device holds into the controls.
    ///
    /// Only while the controls are unedited, unless `force`d. A re-read — the
    /// Calibration page asks for one after it writes — must not stamp over edits
    /// the user is in the middle of, and the part of the blob it would be
    /// overwriting them with is the part calibration does not touch anyway.
    /// Cancel and a finished save are what force it.
    ///
    /// The brake's elastomer and throttle cut have a read of their own, which
    /// can land after the blob. They are taken whenever they arrive for the
    /// first time, edits or not: until then their controls could not be touched.
    void loadFromDevice(bool force = false);

    /// Put `settings` into the controls, silently.
    void apply(const SettingsSave::Settings &settings);

    /// The brake's limit on force follows the elastomer.
    void refreshForceRange();

    /// What the controls hold, in the device's own units.
    SettingsSave::Settings current() const;

    /// Whether the controls differ from what the device holds.
    bool isDirty() const;

    /// Enable the buttons and the controls to match, and say how the last save
    /// went.
    void refreshButtons();

    /// Whether the controls hold the device's settings, and if not, why not.
    void refreshConfigNotice();

    PedalInput *m_pedals = nullptr;
    ConfigRead *m_config = nullptr;
    BrakeOptionsRead *m_brakeOptions = nullptr;
    SettingsSave *m_save = nullptr;
    std::optional<UsbInterfaceInfo> m_vendorInterface;

    /// One pedal's controls, in the order of the pedal table in Pedals.h, which
    /// is what maps each one to its axis.
    struct Row
    {
        CurveEditor *curve = nullptr;
        RangeSlider *deadzones = nullptr;
        QCheckBox *filterEnabled = nullptr;
        QSlider *filter = nullptr;  ///< The level, 1 and up; the box being clear is 0.
        QLabel *filterText = nullptr;  ///< The level as a figure beside the slider.
        QGroupBox *box = nullptr;
    };
    QList<Row> m_rows;

    /// Build one pedal's box, and say through `row` what is in it. A member
    /// rather than a free function only because Row is one.
    QGroupBox *pedalBox(const QString &title, bool brake, Row *row);

    /// The brake's own rows, in its box.
    void addBrakeRows(QFormLayout *form, QWidget *box);

    QSpinBox *m_force = nullptr;
    QComboBox *m_elastomer = nullptr;
    QCheckBox *m_cutEnabled = nullptr;
    QSlider *m_cutPoint = nullptr;  ///< Tenths of a percent, as the device stores it.
    QLabel *m_cutPointText = nullptr;

    /// Whether the blob has been read into the controls at least once.
    bool m_loaded = false;

    /// What was last loaded into the controls. The controls are edited exactly
    /// when they no longer match it — compared against this rather than against
    /// the blob itself, which is briefly unreadable whenever it is being read
    /// again and would make the buttons flicker off and on with it.
    SettingsSave::Settings m_shown {};

    /// The save's state when last looked at, so the end of a read-back can be
    /// told from every other change.
    SettingsSave::State m_lastSaveState = SettingsSave::State::Idle;

    /// Says why no live positions are shown. Hidden when they are.
    QLabel *m_positionNotice = nullptr;

    /// Says where the values in the controls came from.
    QLabel *m_configNotice = nullptr;

    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    /// How the last save went. Below the buttons, for the reason the
    /// Calibration page gives: a line that appears above them would move them.
    QLabel *m_saveMessage = nullptr;
};

} // namespace hidcenter::simsonn_pedals
