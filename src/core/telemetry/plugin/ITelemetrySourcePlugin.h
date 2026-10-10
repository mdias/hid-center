// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtPlugin>

#include "core/telemetry/plugin/ITelemetrySource.h"

namespace hidcenter {

/// ABI version of the telemetry-source plugin interface.
///
/// Its own counter, deliberately unrelated to kDevicePluginAbiVersion. The two
/// interfaces have no reason to change together, and sharing one number would
/// mean every device module needing a rebuild because a sim gained a field.
///
/// Bump it whenever ITelemetrySourcePlugin or ITelemetrySource changes shape —
/// a new virtual, a changed signature, a changed signal — and whenever
/// TelemetryFrame does, since a source fills one in.
///
/// History:
///   1: first version.
inline constexpr int kTelemetrySourcePluginAbiVersion = 1;

/// One sim's telemetry module, as the loader sees it.
///
/// The shape of IDevicePlugin, for the same reasons: a plain abstract class
/// rather than a QObject, so the concrete plugin can inherit QObject first, and
/// unexported because it is implemented on the far side of the .so boundary
/// rather than called across it by name.
class ITelemetrySourcePlugin
{
public:
    virtual ~ITelemetrySourcePlugin() = default;

    /// Must return kTelemetrySourcePluginAbiVersion as compiled into the module.
    virtual int abiVersion() const = 0;

    /// Stable, machine-facing identifier — "rfactor2". Used to notice two
    /// modules claiming the same sim, and to name one in a setting later. Not
    /// translated, and not changed once shipped.
    virtual QString sourceId() const = 0;

    /// What the user sees: "rFactor 2 / Le Mans Ultimate". The name that appears
    /// beside the traffic arrow when this source is the one being listened to.
    virtual QString displayName() const = 0;

    /// Create the object that listens.
    ///
    /// **Returned with no parent.** The caller moves it to a thread of its own,
    /// which a parented QObject cannot do, and takes ownership of it outright.
    virtual ITelemetrySource *createSource() = 0;
};

} // namespace hidcenter

#define HIDCENTER_TELEMETRY_SOURCE_PLUGIN_IID "org.hidcenter.ITelemetrySourcePlugin/1.0"

Q_DECLARE_INTERFACE(hidcenter::ITelemetrySourcePlugin, HIDCENTER_TELEMETRY_SOURCE_PLUGIN_IID)
