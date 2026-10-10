// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/telemetry/TelemetryFrame.h"

#include <QtGlobal>

namespace hidcenter {

QString telemetryFlagName(TelemetryFlag flag)
{
    switch (flag) {
    case TelemetryFlag::None:
        return QStringLiteral("None");
    case TelemetryFlag::Green:
        return QStringLiteral("Green");
    case TelemetryFlag::Yellow:
        return QStringLiteral("Yellow");
    case TelemetryFlag::Blue:
        return QStringLiteral("Blue");
    case TelemetryFlag::White:
        return QStringLiteral("White");
    case TelemetryFlag::Black:
        return QStringLiteral("Black");
    case TelemetryFlag::Checkered:
        return QStringLiteral("Checkered");
    case TelemetryFlag::Orange:
        return QStringLiteral("Orange");
    case TelemetryFlag::Red:
        return QStringLiteral("Red");
    }
    return QStringLiteral("None");
}

int TelemetryFrame::rpmPercent() const
{
    // Guarded here as well as at ingest: this is a plain struct, anyone may
    // assign to rpmMax, and the division would be by zero.
    const int scale = rpmMax > 0 ? rpmMax : kTelemetryRpmMax;
    return qBound(0, int(qint64(rpm) * 100 / scale), 100);
}

} // namespace hidcenter
