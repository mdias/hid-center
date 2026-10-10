// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

#include "AlphaEvoWheelConfig.h"
#include "WheelRpmLights.h"
#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoSettings;
struct WheelControl;
enum class WheelModel;

/// The steering wheel's configuration as this app sets it: the brightness of
/// its LEDs, each button's colour, what lights it and the joystick button it
/// presses, and the RPM lights.
///
/// GUI thread. Holds the block, keeps what the user chose between runs, and
/// hands each change to AlphaEvoSettings as the reports that carry it.
///
/// **Lives with the device, not with the page**, as WheelLedController does. The
/// wheel's configuration cannot be read back, so the only way the wheel and the
/// app agree is for the app to say it: once when the base is detected, whether
/// or not the tab is ever opened, and again on every change.
///
/// **Only the brightness and the clutch paddles go to every wheel.** The block
/// this module sends around what it sets is a GTC's (see defaultWheelConfig()),
/// and which byte means what differs from wheel to wheel. So everything else is
/// sent only while the base reports a GTC. For any other wheel the colours are
/// kept and drawn but not sent, and the rest is not offered at all. The
/// brightness and the clutch bytes are in the last report, which goes to every
/// wheel, since it is the one that applies the block.
class WheelConfigController : public QObject
{
    Q_OBJECT

public:
    /// `info` is only used to work out where the settings are kept, the same
    /// place WheelLedController keeps the ring's.
    WheelConfigController(AlphaEvoSettings *settings, UsbDeviceInfo info,
                          QObject *parent = nullptr);
    ~WheelConfigController() override;

    /// How bright the wheel's LEDs are set, 0 to 100.
    int ledBrightness() const { return m_config.ledBrightness(); }

    /// Set the brightness, clamped to 0–100.
    ///
    /// Sent straight away if the wheel has not been updated for a second,
    /// otherwise held and sent as the latest value once that second is over.
    /// The wheel loses an update that follows another too closely, so a dragged
    /// slider moves it once a second. Saved shortly after.
    void setLedBrightness(int percent);

    /// Whether the clutch paddles are synced, and the ratio they are synced at,
    /// 0 to 100. See kWheelConfigClutchModeOffset.
    bool clutchSynced() const { return m_config.clutchSynced(); }
    int clutchRatio() const { return m_config.clutchRatio(); }

    /// Set either, the ratio clamped to 0–100. Sent and saved as the brightness
    /// is.
    void setClutchSynced(bool synced);
    void setClutchRatio(int percent);

    /// What each control of a wheel is lit in, by control key, invalid for off.
    ///
    /// For a GTC, every button's colour as it is in the block, which is the
    /// colour the block gives it until the user picks another, and every RPM
    /// LED's, by the LED's key, as the top pattern has it. For other wheels only
    /// what the user has picked, so the layout's own colours show for the rest.
    QHash<QString, QColor> controlColours(WheelModel model) const;

    /// Set what a control is lit in, invalid for off, and keep it. On a GTC
    /// with the base reporting one, it is also sent, paced as the brightness is.
    void setControlColour(WheelModel model, const QString &key, const QColor &colour);

    /// What lights a control's LEDs, as one of the kWheelLedTrigger… numbers.
    /// Only for a GTC control with an LED slot, and nothing for any other: no
    /// other wheel's block is held to keep it in.
    ///
    /// A button never set here is lit all the time, whatever the block gave
    /// it, since the schematic draws every button lit. Anything else lit, such
    /// as a GTC's ABS and TC windows, keeps what the block gives it.
    std::optional<int> controlTrigger(WheelModel model, const QString &key) const;

    /// Set what lights a control's LEDs, and keep it. Ignored for a control
    /// controlTrigger() has nothing for. Sent as the colours are.
    void setControlTrigger(WheelModel model, const QString &key, int trigger);

    /// The joystick button one action of a control presses, from 1: `action` is
    /// an index into wheelControlActions(), 0 for a button. Only for a GTC
    /// control whose input for that action is known, and nothing for any other.
    std::optional<int> controlButton(WheelModel model, const QString &key, int action) const;

    /// Make one action of a control press joystick button `button`, 1 to
    /// kWheelButtonMax, and keep it. Ignored for an action controlButton() has
    /// nothing for, or a button out of that range. Sent as the colours are.
    void setControlButton(WheelModel model, const QString &key, int action, int button);

    /// One action of one of the layout's controls.
    struct SharedAction
    {
        QString controlKey;
        int action = 0;
    };

    /// What else on a wheel presses a joystick button.
    struct ButtonSharing
    {
        /// The actions of the layout's controls that press it, in input order.
        QList<SharedAction> controls;

        /// How many of the wheel's inputs with no control in the layout press
        /// it: on a GTC, the shift paddles, which the layout gives no input.
        int otherInputs = 0;

        bool isEmpty() const { return controls.isEmpty() && otherInputs == 0; }
    };

    /// Every input of a GTC but the one of control `key`'s `action` that presses
    /// `button`, as the block has them now, the same control's other actions
    /// included. Empty for an action controlButton() has nothing for.
    ButtonSharing buttonSharing(WheelModel model, const QString &key, int action,
                                int button) const;

    /// A GTC's RPM lights as the block has them, lowest percentage first, with
    /// as many LEDs as its layout draws. Nothing for any other wheel.
    std::optional<RpmLights> rpmLights(WheelModel model) const;

    /// Set a GTC's RPM lights, and keep them. Each percentage is held to 0–100
    /// and to no less than the one before, and LEDs past the layout's are
    /// ignored, as is anything past the tenth pattern. Ignored for any other
    /// wheel. Sent as the colours are.
    void setRpmLights(WheelModel model, const RpmLights &lights);

    /// The wheel id the base last reported, 0 for none or before it has said.
    int attachedWheelId() const { return m_attachedWheelId; }

    /// Whether a change to a GTC's settings, everything but the brightness,
    /// reaches the wheel now: while the base reports one, or offline, where it
    /// is logged instead of sent.
    bool sendsWheelSettings() const;

public Q_SLOTS:
    /// Look at which wheel the base reports, and when that has changed, say so
    /// and send a GTC its colours. Called on every base status refresh, and once
    /// by the device after the probe, which fills in the status without saying.
    void refreshAttachedWheel();

Q_SIGNALS:
    /// The base reports another wheel than it did, or none.
    void attachedWheelChanged();

private Q_SLOTS:
    void flushPending();
    void save();

private:
    /// A control of a model's wheel, when that wheel is the one the block is
    /// laid out for. Null for another wheel, or a key it does not have.
    const WheelControl *blockControl(WheelModel model, const QString &key) const;

    /// The wheel input of one of a blockControl()'s actions, or -1 when there is
    /// no such control, action, or known input.
    int controlInput(WheelModel model, const QString &key, int action) const;

    /// Mark every report where the block now differs from `before`, and send
    /// them now if the wheel has not been updated for the pacing interval, or
    /// else when that is over.
    void scheduleChanges(const WheelConfig &before);

    /// Send the reports in `chunks`, a bit per report, followed by the one that
    /// makes the wheel apply them. See kWheelConfigApplyChunk.
    void push(quint8 chunks);

    AlphaEvoSettings *m_settings = nullptr;
    UsbDeviceInfo m_info;
    WheelConfig m_config;

    /// What the user has picked, by wheel id and then control key. A GTC's
    /// picks are in m_config as well, which is what is drawn and sent.
    QHash<int, QHash<QString, QColor>> m_colours;
    bool m_coloursChanged = false;

    /// A GTC's triggers, mappings and RPM lights are only in m_config. These
    /// say they need saving.
    bool m_triggersChanged = false;
    bool m_mappingsChanged = false;
    bool m_rpmLightsChanged = false;

    int m_attachedWheelId = 0;
    bool m_attachedWheelKnown = false;

    /// Runs for a second after each update. A change while it runs is left
    /// to flushPending(), which sends the reports it touched as they are by then,
    /// as one update however many changes there were.
    QTimer *m_throttle = nullptr;

    /// The reports changed and not yet sent, a bit each. Starts with the
    /// brightness, held until refreshAttachedWheel() first runs.
    quint8 m_pendingChunks = 0;

    QTimer *m_saveTimer = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
