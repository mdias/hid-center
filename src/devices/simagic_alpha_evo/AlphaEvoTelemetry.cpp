// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoTelemetry.h"

#include "AlphaEvoAnimator.h"
#include "AlphaEvoSettings.h"

namespace hidcenter::simagic_alpha_evo {

AlphaEvoTelemetry::AlphaEvoTelemetry(AlphaEvoSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    AlphaEvoAnimator *animator = m_settings ? m_settings->animator() : nullptr;
    if (!animator)
        return;

    // Queued, because the animator lives on its own thread. Readings arrive
    // already deduplicated: the worker compares before it speaks, so a car
    // sitting at a steady idle costs this nothing.
    connect(animator, &AlphaEvoAnimator::telemetrySampled, this, &AlphaEvoTelemetry::onSampled);

    QMetaObject::invokeMethod(animator, "setTelemetryEnabled", Qt::QueuedConnection,
                              Q_ARG(bool, m_enabled));
    QMetaObject::invokeMethod(animator, "setRateHz", Qt::QueuedConnection, Q_ARG(int, m_rateHz));
}

void AlphaEvoTelemetry::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;

    if (AlphaEvoAnimator *animator = m_settings ? m_settings->animator() : nullptr) {
        QMetaObject::invokeMethod(animator, "setTelemetryEnabled", Qt::QueuedConnection,
                                  Q_ARG(bool, m_enabled));
    }

    if (!m_enabled) {
        // Cleared here as well as on the worker, and synchronously. The worker
        // will say the same thing a tick later, but a page that reads data()
        // straight after unticking the box must not be shown the last live
        // value: a stale reading that looks live is worse than an obvious
        // nothing.
        if (m_data != SimagicTelemetry{}) {
            m_data = {};
            Q_EMIT dataChanged();
        }
    }

    Q_EMIT enabledChanged(m_enabled);
}

void AlphaEvoTelemetry::setRateHz(int hz)
{
    const int clamped = qBound(kMinimumRateHz, hz, kMaximumRateHz);
    if (m_rateHz == clamped)
        return;

    m_rateHz = clamped;

    if (AlphaEvoAnimator *animator = m_settings ? m_settings->animator() : nullptr) {
        QMetaObject::invokeMethod(animator, "setRateHz", Qt::QueuedConnection,
                                  Q_ARG(int, m_rateHz));
    }

    Q_EMIT rateChanged(m_rateHz);
}

void AlphaEvoTelemetry::onSampled(const SimagicTelemetry &data)
{
    // A reading can be in flight when the box is unticked, and it would arrive
    // after the zeroing above and put the last live value back on the page.
    if (!m_enabled)
        return;

    if (m_data == data)
        return;

    m_data = data;
    Q_EMIT dataChanged();
}

} // namespace hidcenter::simagic_alpha_evo
