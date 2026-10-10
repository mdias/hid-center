// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <array>
#include <optional>

#include "ConfigRead.h"
#include "Pedals.h"
#include "core/usb/UsbDeviceInfo.h"

class QObject;

namespace hidcenter {
class IUsbDevice;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

/// Everything one `<letter>_pf` carries: a pedal's whole state, curve,
/// calibration and deadzones together. The command is all-or-nothing, so a
/// caller that wants to change one part has to hand back every other part as the
/// device already holds it.
struct PedalRecord
{
    std::array<qint8, ConfigRead::kCurvePoints> curve {};
    ConfigRead::AxisCalibration calibration;
};

/// The record the device holds for one axis right now, straight out of the blob,
/// or nothing before the blob has been read.
std::optional<PedalRecord> storedRecord(const ConfigRead &config, int axis);

/// Recompute `record`'s effective trio from its physical range and deadzones.
///
/// Spelled out the long way, in the host's own order of operations, because the
/// rounding depends on it: effective minimum truncates the deadzone added to the floor
/// while effective maximum truncates it subtracted from the ceiling, and effective centre
/// is an integer divide of the pair that comes out. Doing this "more sensibly"
/// gives numbers a byte or two off, which would make a device look
/// changed when nothing was edited.
///
/// The device does not do this itself: it trusts whatever effective range it is
/// handed. So anything that moves the physical range or a deadzone must call it.
void deriveEffectiveRange(PedalRecord &record);

/// Send one pedal's `<letter>_pf` on `interface`, holding it open on behalf of
/// `owner` for as long as the write takes. False if the write was refused; the
/// device never acknowledges one that was not, so a caller that wants to know it
/// landed has to read the blob back.
bool writePedalRecord(IUsbDevice *device, QObject *owner, const UsbInterfaceInfo &interface,
                      const PedalSpec &pedal, const PedalRecord &record);

/// A named command's report: `06`, the name in plain ASCII, then the payload,
/// padded to 64 bytes. There is no length or separator — the device knows each
/// name's payload width.
QByteArray namedCommand(const char *name, const QByteArray &payload = {});

/// Send one named command on `interface`, held open for `owner` for as long as
/// it takes. False if the write was refused.
bool sendNamedCommand(IUsbDevice *device, QObject *owner, const UsbInterfaceInfo &interface,
                      const char *name, const QByteArray &payload);

// --- The brake -----------------------------------------------------------------

/// The brake's elastomer, as the device names it in `damping_type`.
enum class Elastomer : char { Soft = 's', Medium = 'm', Hard = 'h' };

/// The range allowed for "Maximum force" for each elastomer: six
/// kilograms at least, and at most 60, 100 or 140 as it gets harder.
inline constexpr int kBrakeForceMinimumKg = 6;
int brakeForceMaximumKg(Elastomer elastomer);

/// The brake's maximum force as the device holds it: the physical travel that is
/// left once the start deadzone is taken off, in kilograms. Rounded, because the
/// control for it is a whole number of kilograms.
int brakeForceKg(const ConfigRead::AxisCalibration &calibration);

/// The physical top of travel that puts `kg` at full output, given the start
/// deadzone. The `damping_max` arithmetic, clamped to the sensor range.
qint16 brakePhysicalMaximum(int kg, qint16 physicalMinimum, quint8 startDeadzone);

/// `damping_max`: the brake's new physical top and effective top, which a `b_pf`
/// also carries. Both are sent whenever the force changes.
QByteArray brakeForcePayload(const ConfigRead::AxisCalibration &calibration);

/// `damping_type`: the elastomer, and alongside it the maximum force as a raw
/// count above the effective floor — `kg * 360 + effective minimum`.
QByteArray elastomerPayload(Elastomer elastomer, int forceKg,
                            const ConfigRead::AxisCalibration &calibration);

/// The brake point past which the throttle is cut, and whether it is. Stored in
/// tenths of a percent of the brake's travel, 1..500 — 0.1 % to 50 %.
struct ThrottleCut
{
    bool enabled = false;
    int tenths = 0;

    bool operator==(const ThrottleCut &) const = default;
};

inline constexpr int kThrottleCutMinimum = 1;
inline constexpr int kThrottleCutMaximum = 500;

/// `brakeCutoff`: `[enabled] [tenths, uint16 little-endian]`.
QByteArray throttleCutPayload(const ThrottleCut &cut);

} // namespace hidcenter::simsonn_pedals
