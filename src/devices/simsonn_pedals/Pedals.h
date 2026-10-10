// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <iterator>

namespace hidcenter::simsonn_pedals {

/// One pedal, and which of the device's axes carries it.
struct PedalSpec
{
    const char *title;
    int axis;

    /// The letter naming this pedal's `<letter>_pf` command — the one write that
    /// updates a single pedal's curve, calibration and deadzones in one report.
    char letter;
};

/// The pedals, in the order they are shown, and the axis each one is.
///
/// The same index works for both places a pedal's position turns up: axis `i` of
/// input report `0x01`, and the live raw value at params `+0x440 + 2*i`. They
/// agree because both index the same per-axis record of the config blob, whose source axis
/// is `0`, `1` and `2` on the three pedals of a Plus X.
///
/// The report declares three axes and names none of them — all three share usage
/// 0001:0030 — so this order cannot be read off the descriptor. It was read off
/// the hardware instead: on a Plus X, pressing the throttle moves axis 0 and
/// pressing the brake moves axis 1, which leaves the clutch as axis 2. A later
/// params capture agrees from a second direction: axis 0 tops out just past its
/// stored physical maximum, the way a pedal with a physical stop does, while axis
/// 1 runs far beyond its own — which is what a load cell does, since its
/// physical maximum is a force setting and not an end of travel.
inline constexpr PedalSpec kPedals[] = {
    {QT_TR_NOOP("Throttle"), 0, 't'},
    {QT_TR_NOOP("Brake"), 1, 'b'},
    {QT_TR_NOOP("Clutch"), 2, 'c'},
};

inline constexpr int kPedalCount = int(std::size(kPedals));

/// Which entry of kPedals is the load cell. The brake is the only pedal whose
/// reading is a force rather than a position, and so the only one that can be
/// shown in kilograms.
inline constexpr int kBrakePedal = 1;

/// The raw range the pedals report in, on both the axis report and the params
/// report: signed 16-bit, but one short of the full range at each end. Confirmed
/// from the Plus X report descriptor (logical -32767..32767) and from the
/// `0x8001` every axis reads at rest.
inline constexpr int kRawMinimum = -32767;
inline constexpr int kRawMaximum = 32767;

/// Raw sensor counts per kilogram on the brake load cell.
///
/// The "Maximum force" spin box becomes a raw count through `damping_max`,
/// which stores `(int)(kg * 360 / (1 - start deadzone/100)) + physical minimum` as
/// the new physical top of travel, and both writes it made on a Plus X land
/// exactly — 18 kg with a 5 % start deadzone gave `physical maximum = -25946`
/// (6821 counts, and `(int)(18 * 360 / 0.95)` is 6821), 18 kg with none gave
/// `-26287` (6480, and `18 * 360` is 6480).
///
/// A load cell being linear is the whole reason a single factor can stand in for
/// a curve. Nothing here has been checked against a known weight.
inline constexpr double kBrakeCountsPerKg = 360.0;

/// The count an unloaded brake reads, and so the zero the force is measured
/// from. It is physical minimum on every axis of every Plus X config seen so far,
/// and it is what the pedals report at rest.
///
/// A device that has been tared (the `write_offset` command) against a loaded
/// pedal would sit somewhere else, and this would then be wrong by that offset.
/// Reading physical minimum out of the config blob is the fix, and wants the
/// whole blob read — which nothing here does yet.
inline constexpr int kBrakeZeroCount = kRawMinimum;

/// `raw` as a force on the brake load cell, in kilograms.
///
/// A raw count and nothing else: the deadzones are not part of a force. They move
/// where the device's *output* reaches 0 % and 100 %, so taking them off a figure
/// in kilograms would make the pedal look as though it were being pressed less
/// hard because somebody widened one.
inline double brakeForceKg(int raw)
{
    return (double(raw) - kBrakeZeroCount) / kBrakeCountsPerKg;
}

/// How far a pedal has to travel during a calibration before the travel is
/// believed, as a count of raw units: one percent of the sensor's whole range.
///
/// A pedal that is not plugged in still reports, and it does not report a flat
/// line — a disconnected clutch measured here wandered over sixteen counts in
/// twenty-five seconds. Sixteen is nothing against the 655 below, and 655 is
/// nothing against the six thousand a real brake covers, so the gap between "not
/// connected" and "barely used" is wide enough that one threshold separates them
/// without having to be chosen carefully.
inline constexpr int kMovementThreshold = (kRawMaximum - kRawMinimum) / 100;

} // namespace hidcenter::simsonn_pedals
