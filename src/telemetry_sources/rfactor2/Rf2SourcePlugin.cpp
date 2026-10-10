// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Rf2SourcePlugin.h"

#include "Rf2Source.h"

namespace hidcenter::rfactor2 {

QString Rf2SourcePlugin::sourceId() const
{
    return QStringLiteral("rfactor2");
}

QString Rf2SourcePlugin::displayName() const
{
    return tr("rFactor 2 / Le Mans Ultimate");
}

ITelemetrySource *Rf2SourcePlugin::createSource()
{
    // No parent, as the interface requires: the caller moves this to a thread of
    // its own, and a parented QObject cannot go.
    return new Rf2Source;
}

} // namespace hidcenter::rfactor2
