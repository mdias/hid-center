// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

#include "SimagicTelemetry.h"

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoSettings;

/// The telemetry this wheelbase is being sent, as the UI sees it.
///
/// **The GUI half of a worker**, exactly the shape AlphaEvoSettings has over
/// AlphaEvoIo. It holds the last reading and the two settings the Telemetry page
/// offers, and nothing else: the sampling, the conversion into the units the
/// hardware wants, and the sending all happen on AlphaEvoAnimator, one thread
/// away.
///
/// The pacing is not cosmetic. Frames arrive as fast as their source produces
/// them, and a wheelbase does not want every one of them — nor does the page
/// want to repaint at that rate. A report goes out on exactly this rate.
///
/// That rate is also the clock the wheel's LED animations run on, because the
/// shift lights and the ring are both telemetry-driven LED output and reading
/// the hub twice would let them describe different instants. So turning
/// telemetry off stops the reports and not the clock — see
/// AlphaEvoAnimator::setTelemetryEnabled.
class AlphaEvoTelemetry : public QObject
{
    Q_OBJECT

public:
    /// Slowest and fastest the base is offered, and where it starts. 60 is the
    /// rate a dash refreshes at; below 20 the shift lights visibly step.
    static constexpr int kMinimumRateHz = 20;
    static constexpr int kMaximumRateHz = 90;
    static constexpr int kDefaultRateHz = 60;

    /// `settings` is what owns the clock. It may be offline or have failed its
    /// probe; the clock runs either way, and only the sending stops.
    explicit AlphaEvoTelemetry(AlphaEvoSettings *settings, QObject *parent = nullptr);

    const SimagicTelemetry &data() const { return m_data; }

    /// Whether telemetry is being tracked and sent. On by default, because a
    /// connected base wanting its telemetry is the ordinary case.
    ///
    /// Enabled is not the same as sending: reports go out only while the hub has
    /// something real behind it — a source feeding it, or test mode. Turning this
    /// off sends one last report of nothing, so the base does not sit lit at the
    /// last value it was given.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    int rateHz() const { return m_rateHz; }
    void setRateHz(int hz);

Q_SIGNALS:
    /// The data changed, at most rateHz times a second.
    void dataChanged();

    void enabledChanged(bool enabled);
    void rateChanged(int hz);

private Q_SLOTS:
    /// A reading from the worker. Already deduplicated there, so this is only
    /// ever a change.
    void onSampled(const hidcenter::simagic_alpha_evo::SimagicTelemetry &data);

private:
    AlphaEvoSettings *m_settings = nullptr;
    SimagicTelemetry m_data;

    // Both are pushed to the worker at construction rather than left to agree by
    // default, for the reason AlphaEvoSettings pushes the dry-run flag: two
    // initialisers in different files that have to match is a bug waiting for
    // someone to change one of them.
    bool m_enabled = true;
    int m_rateHz = kDefaultRateHz;
};

} // namespace hidcenter::simagic_alpha_evo
