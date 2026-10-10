// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/DeveloperMode.h"

namespace hidcenter {

namespace {
bool g_developerMode = false;
}

bool developerMode()
{
    return g_developerMode;
}

void setDeveloperMode(bool enabled)
{
    g_developerMode = enabled;
}

} // namespace hidcenter
