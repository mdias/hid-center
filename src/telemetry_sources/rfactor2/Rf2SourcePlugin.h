// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

#include "core/telemetry/plugin/ITelemetrySourcePlugin.h"

namespace hidcenter::rfactor2 {

/// The telemetry source module for rFactor 2 and Le Mans Ultimate.
///
/// The two share a telemetry struct and a sender, so they are one module rather
/// than two. A sim with a different wire format is a new directory beside this
/// one, and nothing in hidcenter-core changes.
class Rf2SourcePlugin : public QObject, public ITelemetrySourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID HIDCENTER_TELEMETRY_SOURCE_PLUGIN_IID FILE "rfactor2.json")
    Q_INTERFACES(hidcenter::ITelemetrySourcePlugin)

public:
    /// Inline on purpose: it must compile the constant from the headers *this
    /// module* was built against, which is the only thing that makes the
    /// loader's check mean anything.
    int abiVersion() const override { return kTelemetrySourcePluginAbiVersion; }

    QString sourceId() const override;
    QString displayName() const override;
    ITelemetrySource *createSource() override;
};

} // namespace hidcenter::rfactor2
