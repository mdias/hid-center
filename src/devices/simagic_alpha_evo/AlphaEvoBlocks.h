// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "AlphaEvoLedProtocol.h"
#include "AlphaEvoProtocol.h"
#include "AlphaEvoSpecs.h"
#include "AlphaEvoWheelConfig.h"
#include "SimagicLeds.h"
#include "SimagicTelemetry.h"

namespace hidcenter {
class IUsbDeviceHidInterface;
}

namespace hidcenter::simagic_alpha_evo {

/// The protocol itself: turning a status report and a set of edits into the
/// bytes to send.
///
/// Free functions with no state of their own, because they run on the I/O thread
/// and must not reach into an object the GUI thread owns. Each takes the status
/// it composes from explicitly, which is also the read-modify-write rule made
/// impossible to skip: there is no way to compose a block without a status to
/// copy the unknown bytes out of.

/// Read the base's two status reports into `out`. Blocking; call it on the I/O
/// thread.
///
/// Report `0x81` is the settings, and it is what the return value is about: no
/// answer means no status, and the caller has nothing to work with.
///
/// Report `0x23` is the live telemetry, and it is read on sufferance. No
/// wheelbase has ever been seen answering it — the layout is not
/// confirmed on the wire — so a base that ignores it leaves
/// `out.liveValid` false, puts the reason in `liveError` if one was asked for,
/// and **still returns true**. Failing the whole read on an unverified report id
/// would take probing, polling and every settings write down with it.
///
/// `readLive` is for the caller that has no use for telemetry: requestApply()
/// reads first only to copy the unknown bytes into a settings block, so it skips
/// the second transfer.
bool readStatus(IUsbDeviceHidInterface *device, StatusSnapshot &out, QString *error, bool readLive = true,
                QString *liveError = nullptr);

/// Compose one 64-byte SET-feature payload. Every field not named in `changes`
/// is copied from `status`, including the ones whose meaning is unknown.
QByteArray composeSettings1(const StatusReport &status, const SettingValues &changes);
QByteArray composeSettings2(const StatusReport &status, const SettingValues &changes);
QByteArray composeSettings3(const StatusReport &status, const SettingValues &changes);
QByteArray composeSettings4(const StatusReport &status, const SettingValues &changes);

/// Compose the 64-byte telemetry report, `0x91`.
///
/// Alone among the composers this takes no status, and the difference is not an
/// oversight: a settings block is a modification of state the base already
/// holds, so it has to carry back the bytes we do not understand. A telemetry
/// report tells the base about the car instead. There is nothing of the device's
/// in it to preserve, and TelemetryBlock's defaults are the whole of what is
/// known about the fields we cannot fill.
QByteArray composeTelemetry(const SimagicTelemetry &data);

/// Compose the reports that set the wheel's LED ring, in the order to send them.
///
/// Usually one; five stops or more need two, because the payload outgrows a
/// single 64-byte report. An empty list means the packet could not be composed,
/// which at these sizes can only be a bug rather than bad input.
///
/// Like composeTelemetry() and unlike the settings blocks this takes no status:
/// there is no LED state in the status report to preserve, and the ring widget
/// is the only place the colours live.
QVector<QByteArray> composeLedRingReports(const LedRingColours &colours, quint8 deviceModel);

/// The wheel configuration this module sends when it has nothing better: the
/// block the protocol was confirmed from.
///
/// **One user's settings, restated on every write.** The base never hands this
/// block back, so there is no read-modify-write to be had, and every byte this
/// module does not set goes out as that block had it: button mappings, RPM
/// thresholds and patterns, clutch mode and rate. Sending only the report that
/// holds a change limits how much of it is restated, but does not avoid it.
///
/// **It is a GTC's block.** Its mappings, thresholds and LED slots are laid out
/// the way that wheel lays them out, and mean something else on
/// another. See kDefaultWheelConfigWheelId.
WheelConfig defaultWheelConfig();

/// The wheel defaultWheelConfig() was captured from, by its wheel id: 4, the GTC, which is also what the base reported when this
/// module ran against it. The parts of the block that depend on the model, such
/// as the LED colours, are only sent while the base reports this wheel.
inline constexpr int kDefaultWheelConfigWheelId = 4;

/// Whether a GTC presses a joystick button through mapping byte `input`.
///
/// Every one of the forty but 18, 20, 23 and 26. 36 of them are for the eight buttons,
/// two 7-way switches, four tuners and two encoders, and the shift paddles are
/// buttons 13 and 14: inputs 12 and 13. The clutch paddles are axes.
///
/// What says two controls press the same button, and so what the Wheel tab
/// warns about. The drawing gives every one of them a control but the shift
/// paddles'.
constexpr bool isGtcButtonInput(int input)
{
    return WheelConfig::isMappingInput(input) && input != 18 && input != 20 && input != 23
           && input != 26;
}

/// Compose one report of the wheel configuration: `chunk` 0 to 3, 62 bytes.
/// Empty for a chunk out of range.
///
/// Like the ring, and unlike the settings blocks, this takes no status: the base
/// has no copy of this block to preserve. See defaultWheelConfig() for what that
/// costs.
QByteArray composeWheelConfigReport(const WheelConfig &config, int chunk);

/// What a report of the wheel configuration carries that this module sets, for
/// the log: the brightness for chunk 3; the LED slots, mappings and RPM
/// thresholds for chunk 0; the RPM patterns of the LEDs in chunks 1 and 2.
QString describeWheelConfigChunk(const WheelConfig &config, int chunk);

/// Read one setting out of a status report.
int valueOf(const StatusReport &status, Setting setting);

/// Write one setting into a status report, clamped to its spec.
///
/// The exact inverse of valueOf(), and the reason it exists is offline mode:
/// with no wheelbase to read back from, the status report *is* the wheelbase,
/// and edits have to land somewhere. Keeping them here rather than in a parallel
/// table means value() has one possible answer whether or not hardware is
/// present, and the block composers still read from the one place they always
/// did.
void setValue(StatusReport &status, Setting setting, int value);

} // namespace hidcenter::simagic_alpha_evo
