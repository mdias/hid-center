// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QObject>

#include "LedModes.h"
#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoSettings;

/// What the wheel's LED strip is set to, and which mode is being edited.
///
/// GUI thread, and small: the clock on AlphaEvoAnimator is what decides which
/// mode the wheel is actually in, from the same telemetry frame the shift lights
/// come from, so nothing here watches the hub. This holds the settings, keeps
/// them on disk, and says which mode the page is pointed at.
///
/// **Lives with the device, not with the page.** Built when the base is
/// detected, so the strip follows the car whether or not anyone ever opens the
/// tab — the same reason the module builds its telemetry there.
class WheelLedController : public QObject
{
    Q_OBJECT

public:
    /// `info` is only used to work out where the settings are kept; see
    /// ledSettingsKey().
    WheelLedController(AlphaEvoSettings *settings, UsbDeviceInfo info,
                       QObject *parent = nullptr);
    ~WheelLedController() override;

    const LedModeSet &modes() const { return m_modes; }
    const LedModeSettings &mode(LedMode mode) const { return m_modes.at(mode); }

    /// Replace one mode's settings. Pushes the whole set to the animator and
    /// queues a save; does nothing when the settings have not actually moved,
    /// so a page that writes back on every signal cannot loop.
    void setMode(LedMode mode, const LedModeSettings &settings);

    /// Which mode the page is editing. The on-screen ring animates this one,
    /// because that is what is being built — the wheel may be showing something
    /// else, and activeMode() is what says so.
    LedMode editingMode() const { return m_editing; }
    void setEditingMode(LedMode mode);

    /// Whether the edited mode is being shown on the wheel.
    ///
    /// Driven by the page becoming visible and hidden, so a preview cannot
    /// outlive the tab being looked at. Telemetry always wins over it.
    bool isPreviewing() const { return m_previewing; }
    void setPreviewing(bool previewing);

    /// Which mode the wheel is in, which is not always the one being edited.
    LedMode activeMode() const { return m_active; }

    /// Whether the wheel is showing something other than what is being edited.
    /// The one thing the status line has to explain.
    bool isOverridden() const { return m_active != m_editing; }

    /// The frame the edited mode is on, for the on-screen ring.
    ///
    /// Its own clock, deliberately unsynchronised with the animation thread's:
    /// the two are separate displays of one definition, and matching their
    /// phases would be a synchronisation problem bought for nothing.
    LedRingColours previewFrame() const;

Q_SIGNALS:
    void modesChanged();
    void editingModeChanged(hidcenter::simagic_alpha_evo::LedMode mode);
    void activeModeChanged(hidcenter::simagic_alpha_evo::LedMode mode);

private Q_SLOTS:
    void onActiveModeChanged(hidcenter::simagic_alpha_evo::LedMode mode);
    void save();

private:
    void push();

    AlphaEvoSettings *m_settings = nullptr;
    UsbDeviceInfo m_info;

    LedModeSet m_modes;
    LedMode m_editing = LedMode::Idle;
    LedMode m_active = LedMode::Idle;
    bool m_previewing = false;

    /// Restarted when the edited mode changes, so switching to a blinking mode
    /// shows it lit rather than partway into the dark half.
    QElapsedTimer m_phase;

    /// Coalesces a run of edits into one write to disk. Dragging the colour
    /// count through its range would otherwise be a settings write per step.
    QTimer *m_saveTimer = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
