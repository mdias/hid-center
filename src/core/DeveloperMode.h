// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hidcenter/core_export.h"

namespace hidcenter {

/// Whether the app was started with --dev, which turns on tools that are of no
/// use to someone just configuring a device: the protocol analyzer, dry run.
///
/// Lives in core, not in the app, so device modules can ask too; a plain inline
/// variable would give each shared object its own copy.
HIDCENTER_CORE_EXPORT bool developerMode();
HIDCENTER_CORE_EXPORT void setDeveloperMode(bool enabled);

} // namespace hidcenter
