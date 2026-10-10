// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelLedController.h"

#include <QTimer>

#include "AlphaEvoSettings.h"
#include "LedModeStore.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Long enough to swallow a run of edits — a drag on the colour count, a slider
/// being moved — short enough that closing the application straight after a
/// change still keeps it. The destructor saves too, so this is about how often
/// the file is touched rather than about whether a change survives.
constexpr int kSaveDebounceMs = 400;

} // namespace

WheelLedController::WheelLedController(AlphaEvoSettings *settings, UsbDeviceInfo info,
                                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_info(std::move(info))
    , m_modes(loadLedModes(m_info))
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, &WheelLedController::save);

    m_phase.start();

    if (m_settings) {
        connect(m_settings, &AlphaEvoSettings::ledModeChanged, this,
                &WheelLedController::onActiveModeChanged);
    }

    // Straight away rather than when the page is built. The base forgets LED
    // colours within seconds of being left alone, so there is no state on it to
    // preserve — only the question of whether this application is driving the
    // ring or nothing is — and pushing here is what starts it driving from the
    // moment the device is detected.
    push();
}

WheelLedController::~WheelLedController()
{
    // A change made in the last fraction of a second before the window closed is
    // still the user's change.
    if (m_saveTimer && m_saveTimer->isActive())
        save();
}

void WheelLedController::setMode(LedMode mode, const LedModeSettings &settings)
{
    if (m_modes.at(mode) == settings)
        return;

    m_modes.at(mode) = settings;
    push();
    m_saveTimer->start();
    Q_EMIT modesChanged();
}

void WheelLedController::setEditingMode(LedMode mode)
{
    if (m_editing == mode)
        return;

    m_editing = mode;

    // From the start of its cycle, so a blinking mode is shown lit rather than
    // partway into its dark half.
    m_phase.restart();

    if (m_settings)
        m_settings->setLedPreviewAsync(m_previewing, m_editing);

    Q_EMIT editingModeChanged(m_editing);
}

void WheelLedController::setPreviewing(bool previewing)
{
    if (m_previewing == previewing)
        return;

    m_previewing = previewing;
    if (m_settings)
        m_settings->setLedPreviewAsync(m_previewing, m_editing);
}

LedRingColours WheelLedController::previewFrame() const
{
    return ledFrameAt(m_modes.at(m_editing), m_phase.isValid() ? m_phase.elapsed() : 0);
}

void WheelLedController::onActiveModeChanged(LedMode mode)
{
    if (m_active == mode)
        return;

    m_active = mode;
    Q_EMIT activeModeChanged(m_active);
}

void WheelLedController::push()
{
    if (m_settings)
        m_settings->setLedModesAsync(m_modes);
}

void WheelLedController::save()
{
    m_saveTimer->stop();
    saveLedModes(m_info, m_modes);
}

} // namespace hidcenter::simagic_alpha_evo
