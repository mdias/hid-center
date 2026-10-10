// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QWidget>

#include "WheelRpmLights.h"

class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedLayout;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoButtonInput;
class AlphaEvoSettings;
class ClutchPaddlesView;
class PressedButtonsView;
class WheelConfigController;
class WheelSchematic;
struct WheelControl;
struct WheelLayout;
enum class WheelModel;

/// The *Wheel* tab: the steering wheel on the base, and in time its settings.
///
/// A picture, and beside the picture a column for the wheel as a whole: its LEDs,
/// its clutch paddles, if it has any, and which of its buttons are pressed. The wheel is the one the base reports.
/// With none on the base, or one this module has no released layout for, the page
/// says so instead. A developer also gets a picker, to look at any wheel by hand
/// and at the GT Neo, which is not released.
/// The wheel's controls can be pointed at. Clicking a button opens a window of
/// its settings. On a GTC, so does clicking the RPM lights, an encoder or a
/// 7-way switch.
///
/// **The page only edits.** The brightness, the buttons' settings and the RPM
/// lights belong to WheelConfigController, which sends them to the wheel and
/// keeps them between runs. The lines under each say whether a change reaches
/// the wheel, and if not, why.
class WheelPage : public QWidget
{
    Q_OBJECT

public:
    /// `probeError` is why the base did not answer, empty when it did. Any
    /// pointer may be null, which leaves a page that draws and sends nothing,
    /// and shows every button released.
    WheelPage(WheelConfigController *wheel, AlphaEvoButtonInput *buttons,
              AlphaEvoSettings *settings, const QString &probeError, QWidget *parent = nullptr);
    ~WheelPage() override;

protected:
    /// The buttons are read only while the page is on screen.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    /// Show the wheel to show, or the page for there being none.
    void showChosenWheel();

    /// Open the settings window for a control of the wheel shown, as an index
    /// into its WheelLayout::controls.
    void editControl(int index);

    void onWheelConfigSent(bool ok, const QString &error, bool dryRun);
    /// Show what is wrong with sending to the wheel, at the top of the page.
    void refreshStatus();

    /// Show the buttons pressed, and why none can be read, if they cannot.
    void showPressedButtons();

    /// Show where the clutch paddles are, as the sync setting has them
    /// reported.
    void showClutch();

private:
    /// The wheel to draw: the one picked by hand, else the one the base reports
    /// when it is one the user is offered. Null when there is none to draw.
    const WheelLayout *wheelToShow() const;

    /// A page with a heading and a line under it, for a state with no wheel to
    /// draw. `detail` is where the line goes.
    QWidget *makeNoticePage(const QString &objectName, const QString &title, QLabel **detail);

    /// Set what a control of a model's wheel is lit in, invalid for off, and
    /// show it if that wheel is the one on screen.
    void setControlColour(WheelModel model, const QString &key, const QColor &colour);

    /// Set the joystick button one action of a control presses, and mark the
    /// controls that now share one. Only with a controller.
    void setControlButton(WheelModel model, const QString &key, int action, int button);

    /// Set a model's RPM lights, and show them if that wheel is the one on
    /// screen. Only with a controller.
    void setRpmLights(WheelModel model, const RpmLights &lights);

    /// What to warn about one action of a control pressing `button`: what else
    /// on the wheel presses it already, the control's other actions included.
    /// Empty when nothing does.
    QString sharedButtonWarning(WheelModel model, const QString &key, int action, int button) const;

    /// Mark every control on the wheel shown with an action that presses a
    /// joystick button something else presses too.
    void refreshWarnings();

    /// Open the settings window for a control whose only settings are the
    /// joystick buttons it presses. Only with a controller.
    void editInputs(const WheelControl &control, WheelModel model);

    /// The colours to draw for a model's wheel.
    QHash<QString, QColor> coloursFor(WheelModel model) const;

    WheelConfigController *m_wheel = nullptr;
    AlphaEvoSettings *m_settings = nullptr;

    /// The device owns it, and may be gone before the page is.
    QPointer<AlphaEvoButtonInput> m_buttons;

    QComboBox *m_model = nullptr;
    QStackedLayout *m_pages = nullptr;
    QWidget *m_noWheelPage = nullptr;
    QWidget *m_unknownWheelPage = nullptr;
    QLabel *m_noWheelDetail = nullptr;
    QLabel *m_unknownWheelDetail = nullptr;
    WheelSchematic *m_schematic = nullptr;
    QLabel *m_problem = nullptr;
    QGroupBox *m_clutch = nullptr;
    QPushButton *m_clutchSync = nullptr;
    QSpinBox *m_clutchRatio = nullptr;
    ClutchPaddlesView *m_clutchView = nullptr;

    /// What the clutch paddles are set to when there is no controller to keep
    /// it.
    bool m_previewClutchSynced = false;
    int m_previewClutchRatio = 0;

    PressedButtonsView *m_pressed = nullptr;
    QLabel *m_pressedStatus = nullptr;

    /// What each control has been set to, by control key, for each wheel, when
    /// there is no controller to keep it. Keyed by int(WheelModel).
    QHash<int, QHash<QString, QColor>> m_previewColours;

    /// See WheelbaseLedPage: a base that would not answer, and a write failure
    /// that is still standing, each outrank everything else the lines could say.
    QString m_probeError;
    QString m_writeError;
};

} // namespace hidcenter::simagic_alpha_evo
