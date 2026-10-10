// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>

#include "LedModes.h"

namespace hidcenter {
struct UsbDeviceInfo;
}

/// Keeping the wheel's LED modes between runs.
///
/// **Two layers, deliberately apart.** The top half turns a LedModeSet into a
/// JSON object and back and knows nothing about where it is kept; the bottom
/// half is the only part that has ever heard of QSettings.
///
/// That split is not tidiness. Configuration presets are coming, and a preset
/// will carry almost every setting in one document — so what this hands out has
/// to be a *value* the caller puts wherever it likes, nested beside the force
/// feedback settings in a file of its own, rather than a set of writes to one
/// store. Loading a preset will then be ledModesFromJson() on a slice of that
/// document, with nothing here having to change.
namespace hidcenter::simagic_alpha_evo {

/// The format the JSON below is written in.
///
/// Bumped when a change cannot be read by the rules already here — a field
/// whose meaning moved, rather than a field that was added. A document from the
/// future is refused rather than half-read, so a downgrade shows a user their
/// defaults instead of a garbled ring.
inline constexpr int kLedModesFormatVersion = 1;

/// What a base that has never been configured shows.
///
/// Idle is the palette LedRing itself starts on, so a first run looks exactly
/// as it did before modes existed. The other two are quick and loud, because a
/// mode that only appears for a fraction of a second while the car is doing
/// something has to be readable in that fraction.
LedModeSet defaultLedModes();

/// Serialise. Keyed by the stable strings in ledModeSpecs() and by animation
/// *names*, never by enum index, so inserting a mode or an animation cannot
/// silently reinterpret a file written by an older build.
QJsonObject ledModesToJson(const LedModeSet &modes);

/// Read back what ledModesToJson() wrote.
///
/// Forgiving on purpose: anything missing, unrecognised or out of range keeps
/// the default for that field rather than failing the whole document. A settings
/// file is not a protocol — a user who hand-edits one and gets a colour wrong
/// should lose that colour, not every mode they had set.
///
/// `warning` is filled in when something was actually dropped, for the log. An
/// empty warning means the document was read whole.
LedModeSet ledModesFromJson(const QJsonObject &json, QString *warning = nullptr);

/// Where one base's modes are kept.
///
/// The serial when there is one, so a saved look follows the base across ports
/// and reboots. The vendor/product pair when there is not, which means two
/// identical serial-less bases share a set — the honest trade, since there is
/// nothing else to tell them apart between runs.
///
/// Deliberately **not** `devicePath`: that is the sysfs node, and it changes on
/// a replug, so keying on it would quietly lose a user's settings the first time
/// they moved the cable.
QString ledSettingsKey(const UsbDeviceInfo &info);

LedModeSet loadLedModes(const UsbDeviceInfo &info);
void saveLedModes(const UsbDeviceInfo &info, const LedModeSet &modes);

} // namespace hidcenter::simagic_alpha_evo
