// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QObject>

#include "LedModes.h"
#include "SimagicLeds.h"
#include "SimagicTelemetry.h"

class QTimer;

namespace hidcenter::simagic_alpha_evo {

/// Everything the base's LEDs are told, on one clock and one thread.
///
/// **Why one.** The shift lights were already an animation: the telemetry
/// sampler read the hub sixty times a second and sent a report whose `rpmLeds`
/// field lights them. The wheel's ring is the same shape of work, so it joins
/// that clock rather than starting a second. Two clocks would mean two samples
/// of the hub per pass, and the shift lights and the ring could then act on
/// different frames — a car that is under ABS for one output and not for the
/// other. One tick, one sample, both outputs, and that cannot happen.
///
/// It samples, converts, decides and composes. It opens nothing and writes
/// nothing: finished values leave as signals, and whoever owns the I/O thread
/// connects them. The hub is built for being read this way — see the note on
/// TelemetryHub about a module sampling from its own thread.
///
/// **The clock runs whether or not there is hardware.** Unlike the I/O thread,
/// which needs an open handle, this one only computes, so the Telemetry page
/// keeps its rows and the LED page keeps its account of the active mode when the
/// base is offline or failed its probe. What stops is only what would have been
/// sent.
///
/// Owned by AlphaEvoSettings, which is the only thing that starts or stops it,
/// and talks to it entirely through queued invocations. Nothing here touches the
/// UI.
class AlphaEvoAnimator : public QObject
{
    Q_OBJECT

public:
    explicit AlphaEvoAnimator(QObject *parent = nullptr);

public Q_SLOTS:
    /// Start ticking. The timer is created here so it belongs to this thread,
    /// the same reason AlphaEvoIo::startPolling() creates its own.
    void start(int rateHz);

    /// Re-pace the clock. This is the telemetry rate, so the ring animates at
    /// whatever that is — between twenty a second and the cap in
    /// kLedMinFrameGapMs, both of which are smooth enough for a diffused strip.
    void setRateHz(int rateHz);

    /// Whether telemetry reports go out. **Not** whether the clock runs: the
    /// ring still has to animate with telemetry switched off, so this gates the
    /// reports and nothing else.
    ///
    /// Turning it off sends one last report of nothing, so the base does not sit
    /// lit at the last revs it was told about.
    void setTelemetryEnabled(bool enabled);

    /// Whether there is anywhere for a ring frame to go.
    ///
    /// False leaves the clock running and the active mode reported, and stops
    /// frames being composed at all — there is no sink to send them to and, more
    /// to the point, nothing to acknowledge them, so the outstanding-frame gate
    /// below would latch shut on the first one. Re-attaching clears it.
    void setDeviceAttached(bool attached);

    void setModes(const hidcenter::simagic_alpha_evo::LedModeSet &modes);

    /// Show `mode` while telemetry is asking for nothing.
    ///
    /// This is the LED page previewing what is being edited. Telemetry always
    /// wins, which is what makes a preview safe to leave running: a real event
    /// takes the wheel back on the very next tick.
    void setPreview(bool previewing, hidcenter::simagic_alpha_evo::LedMode mode);

    /// One composed frame reached the device — or failed to, which counts the
    /// same. See ledFrameReady() for why this exists.
    void frameConsumed();

Q_SIGNALS:
    /// A telemetry report for the device, at the full tick rate while something
    /// real is behind the values, and once more when that stops.
    void telemetryReady(const hidcenter::simagic_alpha_evo::SimagicTelemetry &data);

    /// The same reading for the UI, deduplicated: a car sitting at a steady idle
    /// costs the Telemetry page nothing.
    void telemetrySampled(const hidcenter::simagic_alpha_evo::SimagicTelemetry &data);

    /// The mode the wheel is now in. Emitted on a change only, and emitted with
    /// no device attached too, so the page's marker is right offline.
    void activeModeChanged(hidcenter::simagic_alpha_evo::LedMode mode);

    /// A set of colours for the ring.
    ///
    /// **At most one is outstanding at a time.** The receiver must answer with
    /// frameConsumed(), because the I/O thread blocks for as long as the base
    /// takes to answer and a settings write is several transfers — so without an
    /// acknowledgement a stall would be paid back as a burst of frames that were
    /// already stale by the time they were sent. Skipping is the right answer
    /// for an animation: the next tick composes the frame for the time it is
    /// then, rather than working through a backlog of the past.
    void ledFrameReady(const hidcenter::simagic_alpha_evo::LedRingColours &colours);

private Q_SLOTS:
    void tick();

private:
    /// Sample once, then feed both outputs from that one reading.
    void pumpTelemetry(const TelemetryFrame &frame, bool live);
    void pumpLeds(const TelemetryFrame &frame);

    QTimer *m_timer = nullptr;
    int m_rateHz = 0;

    // --- Telemetry ----------------------------------------------------------

    bool m_telemetryEnabled = true;
    SimagicTelemetry m_data;

    /// Whether the last tick had real telemetry behind it, so the falling edge —
    /// a game quitting, test mode going off — can send one final report instead
    /// of going quiet with the lights still on.
    bool m_wasLive = false;

    // --- LEDs ---------------------------------------------------------------

    LedModeSet m_modes;
    bool m_previewing = false;
    LedMode m_previewMode = LedMode::Idle;
    LedMode m_active = LedMode::Idle;
    bool m_deviceAttached = false;

    /// Where the active mode is in its cycle. Restarted when the mode changes,
    /// so ABS begins lit rather than halfway through a blink.
    QElapsedTimer m_phase;

    /// When a frame last went out, against kLedMinFrameGapMs.
    QElapsedTimer m_lastFrame;

    /// The last frame posted, so an animation that is between edges — which is
    /// most of a blink, and all of a static mode — posts nothing at all.
    LedRingColours m_sent;
    bool m_haveSent = false;

    /// Frames posted and not yet acknowledged. Never more than one; see
    /// ledFrameReady().
    int m_outstanding = 0;
};

} // namespace hidcenter::simagic_alpha_evo
