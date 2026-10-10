// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLoggingCategory>

#include "hidcenter/core_export.h"

// Run with QT_LOGGING_RULES='hidcenter.*=true' to see plugin loading, device
// enumeration and hotplug traffic.
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(hidcenterHid, HIDCENTER_CORE_EXPORT)
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(hidcenterPlugin, HIDCENTER_CORE_EXPORT)
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(hidcenterAnalyzer, HIDCENTER_CORE_EXPORT)
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(hidcenterTelemetry, HIDCENTER_CORE_EXPORT)
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(hidcenterApp, HIDCENTER_CORE_EXPORT)

// For device modules. Enabled at info level by default (see main.cpp) because a
// module's output — notably what a dry-run write *would* have sent — is meant
// for the user in the log window, not just for a developer with env vars set.
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(hidcenterDevice, HIDCENTER_CORE_EXPORT)
