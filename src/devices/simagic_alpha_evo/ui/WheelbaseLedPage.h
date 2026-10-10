// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "AlphaEvoSpecs.h"
#include "LedModes.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QSlider;
class QTimer;
class QToolBox;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoSettings;
class LedRing;
class WheelLedController;

/// The strip of LEDs around the steering wheel, and what it does when.
///
/// Presentation only, as every page here is: the settings are the controller's,
/// the bytes are AlphaEvoLedProtocol's, and everything that leaves the host does
/// so from a worker thread, so the dry-run gate cannot be bypassed from the UI.
///
/// **One ring, an accordion of modes.** The wheel can only show one look at a
/// time and each mode is a look, so the modes are a QToolBox down the right and
/// the ring — which is a tall control — sits beside it with the vertical space
/// it wants. A tab bar inside a tab would have read as navigation, and there is
/// already a sidebar and a row of tabs on screen saying where you are.
///
/// The ring shows **the mode being edited**, not the one the wheel is wearing.
/// It is an editor: while traction control is cutting in, the wheel goes amber
/// and the ring keeps showing whatever is being built. The accordion marks which
/// mode is live and the status line says so in words.
///
/// Editing a mode also shows it on the wheel, while telemetry is asking for
/// nothing — that is the only way to see a mode with no car running. The preview
/// starts and stops with the page being visible, so it cannot outlive the tab
/// being looked at, and a real event always takes the wheel back.
///
/// There is no LED state in the wheelbase's status report and nothing to read
/// back, so a failed write is not undone here — reverting would throw away what
/// the user asked for and replace it with a guess.
class WheelbaseLedPage : public QWidget
{
    Q_OBJECT

public:
    WheelbaseLedPage(WheelLedController *controller, AlphaEvoSettings *settings,
                     const QString &probeError, QWidget *parent = nullptr);

protected:
    /// The preview follows the page being looked at, in both directions.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    void onRingColoursChanged();
    void onSectionChanged(int index);
    void onActiveModeChanged(hidcenter::simagic_alpha_evo::LedMode mode);
    void onLedColoursSent(bool ok, const QString &error, int framesSent, int framesTotal,
                          bool dryRun);
    void onDryRunChanged(bool dryRun);

    /// The ring light's on/off switch and brightness are wheelbase settings
    /// rather than LED colours, so they go through the settings block write like
    /// every control on the FFB Settings page, and are read back from the status
    /// poll.
    void queueRingLight(hidcenter::simagic_alpha_evo::Setting setting, int value);
    void commitRingLight();
    void loadRingLight();
    void onApplyFinished(bool ok, const QString &error, int blocksWritten, bool dryRun);

    /// Paint the next frame of the edited mode onto the ring's band. On its own
    /// clock, running only while the page is visible.
    void animate();

private:
    /// The per-mode controls, built from ledModeSpecs() rather than written out
    /// once per mode.
    struct Section
    {
        QComboBox *animation = nullptr;
        QSlider *speed = nullptr;
        QLabel *speedValue = nullptr;
        QWidget *speedRow = nullptr;
        int index = -1;  ///< Position in the tool box.
    };

    void buildUi();
    QWidget *buildSection(const LedModeSpec &spec);

    /// Push a section's controls into the controller. One mode at a time, which
    /// is what the page edits.
    void commitSection(LedMode mode);

    /// Move the controls to match the controller, without them answering back.
    void loadSection(LedMode mode);
    void loadRing();

    /// Titles, live markers and swatches, for every section.
    void refreshSectionHeaders();

    QString statusText() const;
    void setStatus(const QString &text, bool warning = false);

    WheelLedController *m_controller = nullptr;
    AlphaEvoSettings *m_settings = nullptr;

    LedRing *m_ring = nullptr;
    QGroupBox *m_ringBox = nullptr;
    QToolBox *m_modes = nullptr;
    QCheckBox *m_ringLight = nullptr;
    QSlider *m_ringBrightness = nullptr;
    QLabel *m_ringBrightnessValue = nullptr;
    /// Debounces keyboard and mouse-wheel brightness changes, and retries a write
    /// that arrived while another was in flight.
    QTimer *m_ringLightCommit = nullptr;
    /// Ring-light edits not yet written. Also keeps the status poll from
    /// overwriting a control before its write has gone.
    SettingValues m_ringLightPending;
    QLabel *m_status = nullptr;
    QTimer *m_animation = nullptr;

    QHash<int, Section> m_sections;  ///< Keyed by int(LedMode).

    /// Set while the page is writing into its own controls, so the signals that
    /// causes are not read back as edits.
    bool m_loading = false;

    /// Why the base would not answer, empty when it did. Outranks everything in
    /// the status line: a base that will not open has nothing else worth saying
    /// about it, and the line is rebuilt from scratch on every change.
    QString m_probeError;

    /// The last write failure, if there is one standing. Kept because the status
    /// line is rebuilt from scratch whenever anything else changes it.
    QString m_writeError;
};

} // namespace hidcenter::simagic_alpha_evo
