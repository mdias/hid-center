// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoAnimator.h"

#include <QTimer>

#include "LedModeStore.h"
#include "core/telemetry/TelemetryHub.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

/// The wheel's rpm LEDs appear to follow ten previously programmed presets, each
/// of which programs every LED on or off. Preset 0 is all on and preset 10 is
/// all off by default, so this maps a percentage onto that range: dark below
/// `kLedsMinRpm`, full above `kLedsMaxRpm`, and stepping through the presets in
/// between. It assumes the LEDs are still on the factory presets.
constexpr int kLedsMinRpm = 80;
constexpr int kLedsMaxRpm = 95;

quint8 rpmLedPresetFor(int rpmPercent)
{
    const int preset = (rpmPercent - kLedsMinRpm) * 9 / (kLedsMaxRpm - kLedsMinRpm);
    return quint8(qBound(0, 10 - preset, 10));
}

} // namespace

AlphaEvoAnimator::AlphaEvoAnimator(QObject *parent)
    : QObject(parent)
    , m_modes(defaultLedModes())
{
}

void AlphaEvoAnimator::start(int rateHz)
{
    if (m_timer)
        return;

    // Created here rather than in the constructor, for the reason
    // AlphaEvoIo::startPolling() creates its own: a timer belongs to the thread
    // that started it, and the constructor runs on the GUI thread.
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &AlphaEvoAnimator::tick);

    setRateHz(rateHz);
    m_phase.start();
    m_timer->start();

    // Do not make the first reading wait a whole period.
    tick();
}

void AlphaEvoAnimator::setRateHz(int rateHz)
{
    m_rateHz = qMax(1, rateHz);
    if (m_timer)
        m_timer->setInterval(1000 / m_rateHz);
}

void AlphaEvoAnimator::setTelemetryEnabled(bool enabled)
{
    if (m_telemetryEnabled == enabled)
        return;

    m_telemetryEnabled = enabled;
    if (m_telemetryEnabled)
        return;

    // The same falling edge a game quitting produces, and deliberately a report
    // of nothing rather than the last one: the page going blank while the base
    // keeps its shift lights lit at the last revs would be the worst of both.
    if (m_wasLive)
        Q_EMIT telemetryReady(SimagicTelemetry{});
    m_wasLive = false;

    // Fall back to zero rather than freezing at the last value: a stale reading
    // that looks live is worse than an obvious nothing.
    if (m_data != SimagicTelemetry{}) {
        m_data = {};
        Q_EMIT telemetrySampled(m_data);
    }
}

void AlphaEvoAnimator::setDeviceAttached(bool attached)
{
    if (m_deviceAttached == attached)
        return;

    m_deviceAttached = attached;

    // Whatever was outstanding will never be acknowledged now, and whatever the
    // wheel was showing is not ours to assume any more. Both are cleared so a
    // reattached device gets the current frame rather than waiting on a gate
    // that nothing will ever open.
    m_outstanding = 0;
    m_haveSent = false;
}

void AlphaEvoAnimator::setModes(const LedModeSet &modes)
{
    if (m_modes == modes)
        return;

    m_modes = modes;

    // The mode being shown may look different now, so let the next tick post it
    // even if the colours happen to work out the same at this instant.
    m_haveSent = false;
}

void AlphaEvoAnimator::setPreview(bool previewing, LedMode mode)
{
    if (m_previewing == previewing && m_previewMode == mode)
        return;

    m_previewing = previewing;
    m_previewMode = mode;
}

void AlphaEvoAnimator::frameConsumed()
{
    m_outstanding = qMax(0, m_outstanding - 1);
}

void AlphaEvoAnimator::tick()
{
    // **One sample, both outputs.** The whole reason these share a clock: the
    // shift lights and the ring describe the same instant, and reading the hub
    // twice would let them disagree.
    const TelemetryHub *hub = TelemetryHub::instance();
    const TelemetryFrame frame = hub->frame();

    // Only while something real is behind the values. With no game running and
    // no test mode there is no car to describe, and a base should not be told
    // sixty times a second about one that does not exist.
    const bool live = hub->isReceiving() || hub->isTestMode();

    if (m_telemetryEnabled)
        pumpTelemetry(frame, live);

    pumpLeds(frame);
}

void AlphaEvoAnimator::pumpTelemetry(const TelemetryFrame &frame, bool live)
{
    SimagicTelemetry sampled;

    // Clamped to the width of the field that carries it, not to the car's
    // limiter: the frame keeps true revs through an overrev, and 65535 is where
    // this stops being representable rather than where it stops being true.
    sampled.rpm = quint16(qBound(0, frame.rpm, 65535));
    sampled.rpmPercent = quint8(frame.rpmPercent());
    sampled.tractionControl = frame.tractionControl;
    sampled.abs = frame.abs;
    sampled.pitLimiter = frame.pitLimiter;
    sampled.flag = frame.flag;
    sampled.rpmLeds = rpmLedPresetFor(sampled.rpmPercent);

    if (live) {
        // Every tick, changed or not, and before the comparison below: the base
        // is being fed a stream rather than told about edges.
        Q_EMIT telemetryReady(sampled);
    } else if (m_wasLive) {
        // The falling edge, and deliberately a default report rather than
        // `sampled`. Nothing is behind the values any more, so the only honest
        // last word is nothing — and saying it literally means this does not
        // depend on whether the tick landed before or after the router reset the
        // frame. A stale `sampled` would leave the shift lights on.
        Q_EMIT telemetryReady(SimagicTelemetry{});
    }

    m_wasLive = live;

    // The dedupe is for the page. Repainting an unchanged row sixty times a
    // second is waste; not sending an unchanged report is a gap.
    if (sampled == m_data)
        return;

    m_data = sampled;
    Q_EMIT telemetrySampled(m_data);
}

void AlphaEvoAnimator::pumpLeds(const TelemetryFrame &frame)
{
    const LedMode fromCar = ledModeForFrame(frame);

    // Telemetry wins. The previewed mode shows only while the car is asking for
    // nothing, which is what makes a preview safe to leave running: a real event
    // takes the wheel back on this very tick.
    const LedMode active = fromCar != LedMode::Idle
        ? fromCar
        : (m_previewing ? m_previewMode : LedMode::Idle);

    if (active != m_active) {
        m_active = active;

        // From the beginning of its cycle, so a mode that has just become active
        // starts lit rather than partway through a blink that happens to be dark.
        m_phase.restart();

        // Said even with nothing attached: the page's live marker is right
        // whether or not there is a wheel to light.
        Q_EMIT activeModeChanged(m_active);
    }

    if (!m_deviceAttached)
        return;

    if (!m_phase.isValid())
        m_phase.start();

    // The clock is the telemetry rate, which the user may have put as high as
    // ninety a second. The ring neither needs nor wants that; see the constant.
    if (m_lastFrame.isValid() && m_lastFrame.elapsed() < kLedMinFrameGapMs)
        return;

    const LedRingColours colours = ledFrameAt(m_modes.at(m_active), m_phase.elapsed());

    // Most of a blink is spent on one of two frames, and all of a static mode on
    // one, so this is what keeps a still wheel free.
    if (m_haveSent && colours == m_sent)
        return;

    // Never more than one in flight. See ledFrameReady().
    if (m_outstanding > 0)
        return;

    m_lastFrame.restart();
    m_sent = colours;
    m_haveSent = true;
    ++m_outstanding;
    Q_EMIT ledFrameReady(colours);
}

} // namespace hidcenter::simagic_alpha_evo
