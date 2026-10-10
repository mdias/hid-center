// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMetaType>
#include <QtEndian>
#include <QtGlobal>

#include <cstddef>

/// Wire format of the SIMAGIC Alpha / Alpha Evo configuration protocol.
///
/// Transcribed from the `hid-simagic-ff` kernel driver (`hid-simagic-settings.h`
/// and `hid-simagic-sysfs.c`), which is where this protocol was worked out.
///
/// Two feature reports carry everything:
///
///   * GET feature `0x81` returns one 64-byte status block holding every
///     setting the base has;
///   * SET feature `0x80` writes a *block* of settings, selected by byte 1.
///
/// Nothing is written a field at a time. To change one setting you read the
/// status, copy the whole target block out of it — including the bytes whose
/// meaning is still unknown, which the base expects to get back verbatim —
/// change the one field, and write the block. See AlphaEvoIo::requestApply().
namespace hidcenter::simagic_alpha_evo {

inline constexpr quint8 kStatusReportId = 0x81;
inline constexpr quint8 kSettingsReportId = 0x80;

/// Every transfer in this protocol is 64 bytes, zero-padded past the block.
inline constexpr int kReportSize = 64;

inline constexpr quint8 kSelectorSettings1 = 0x01;
inline constexpr quint8 kSelectorSettings2 = 0x02;
inline constexpr quint8 kSelectorExtended = 0x10;  ///< Byte 2 then picks the block.
inline constexpr quint8 kSubSelectorRingLight = 0x38;
inline constexpr quint8 kSubSelectorSignal = 0x39;

/// The ring-light byte packs an enable flag and a brightness (0..100) into one
/// value. Changing brightness needs a further packet before the base refreshes
/// its LEDs: the host resends the ring colours (see AlphaEvoIo::requestApply).
inline constexpr quint8 kRingLightEnabledBit = 0x80;
inline constexpr quint8 kRingLightBrightnessMask = 0x7F;

#pragma pack(push, 1)

/// The reply to GET feature `0x81`.
struct StatusReport
{
    quint8 reportId;             ///< 0x81
    quint8 unknown01;
    quint16 maxAngle;            ///< 90..2520
    qint16 ffStrength;           ///< -100..100
    quint8 unknown06;            ///< Appears to be some kind of smooth level. Always 0x02 in practice. Any way to change it?
    quint8 wheelRotationSpeed;
    quint8 mechanicalCentering;
    quint8 mechanicalDamper;
    quint8 centerDamper;
    quint8 mechanicalFriction;
    quint8 gameCentering;
    quint8 gameInertia;
    quint8 gameDamper;
    quint8 gameFriction;
    quint16 angleLock;
    quint8 feedbackDetail;
    quint8 unknown19;            ///< Maybe interpolation level?
    quint8 angleLockStrength;    ///< 0 = Soft, 1 = Normal, 2 = Firm
    quint8 syncSoftLock;
    quint8 mechanicalInertia;
    quint8 torqueLimit;
    quint8 unknown24[23];
    quint8 ringLight;            ///< MSB enables; low bits are brightness 0..100.
    quint8 dynamicPredictionLevel; ///< 0..10
    quint8 unknown49;
    quint8 filterLevel;
    quint8 unknown51;            ///< Filter level high byte?
    quint8 slewRateControl;
    quint8 unknown53;            ///< Slew rate control high byte?
    quint8 steeringTorqueAssist; ///< 0..100
    quint8 wheelChannel;
    quint8 powerSupplyVoltage;
    quint8 unknown57;            ///< emergency stop state?
    quint16 firmwareVersion;
    quint8 unknown60[4];         ///< Not read.
};

/// The reply to GET feature `0x23`: what the base is *doing*, as against `0x81`,
/// which is what it is configured to do.
struct BaseStatusReport
{
    quint8 reportId;             ///< Expected to echo 0x23, by symmetry with 0x81.
    quint8 state;                ///< 1 align, 2 run, 3 stop, 4 fault, 5 tune,
                                 ///< 6 self-test, 7 zero.
    quint8 error;
    quint8 motorTemp;            ///< Degrees Celsius, confirmed on hardware.
    quint8 mosfetTemp;           ///< Degrees Celsius, confirmed on hardware.
    quint8 maxTorque;            ///< Newton metres.
    qint16 feedbackTorque;       ///< Sign-extended by the parser, so signed.
    quint8 input;
    quint8 output;
    quint8 wheelId;
    quint8 canId;
    quint8 canFdId;
    quint16 busVoltage;          ///< Zero-extended by the parser, so unsigned.
    quint8 version;              ///< Not the firmware version that 0x81 carries.
    quint8 flags;
    
    quint8 unknown17;
};

inline constexpr quint8 kBaseStatusReportId = 0x23;
inline constexpr quint8 kBaseStatusFlagBit0 = 0x01;
inline constexpr quint8 kBaseStatusFlagBit1 = 0x02;

/// SET feature `0x80`, selector `0x01`: the force-feedback and game-effect block.
struct Settings1Block
{
    quint8 reportId;             ///< kSettingsReportId
    quint8 selector;             ///< kSelectorSettings1
    quint16 maxAngle;
    qint16 ffStrength;
    quint8 unknown06;            ///< Smooth level? The driver always sends 0x02 here, not the status byte.
    quint8 wheelRotationSpeed;
    quint8 mechanicalCentering;
    quint8 mechanicalDamper;
    quint8 centerDamper;
    quint8 mechanicalFriction;
    quint8 gameCentering;
    quint8 gameInertia;
    quint8 gameDamper;
    quint8 gameFriction;
};

/// SET feature `0x80`, selector `0x02`: angle lock, feedback detail, inertia.
///
/// Beware the driver's names here: its `unknown_offset_06`, `_08` and `_10`
/// actually sit at bytes **5, 7 and 9** — they were named after the *status*
/// offsets they are copied from rather than their position in this block. Ours
/// are named for where they really are, and the static_asserts below prove it.
struct Settings2Block
{
    quint8 reportId;             ///< kSettingsReportId
    quint8 selector;             ///< kSelectorSettings2
    quint16 angleLock;
    quint8 feedbackDetail;
    quint8 unknown05;            ///< Copied from status byte 19. Interpolation level?
    quint8 angleLockStrength;
    quint8 syncSoftLock;
    quint8 mechanicalInertia;
    quint8 torqueLimit;
};

/// SET feature `0x80`, selector `0x10` / sub-selector `0x38`: the ring light.
struct Settings3Block
{
    quint8 reportId;             ///< kSettingsReportId
    quint8 selector;             ///< kSelectorExtended
    quint8 subSelector;          ///< kSubSelectorRingLight
    quint8 unknown03;            ///< The driver always sends 0x00.
    quint8 unknown04;            ///< The driver always sends 0x01.
    quint8 ringLight;            ///< Bit 7 enables; bits 0-6 are brightness.
};

/// SET feature `0x80`, selector `0x10` / sub-selector `0x39`: signal filtering.
///
/// Bytes 5..11 mirror status bytes 48..54, which look like a byte pair (dynamic
/// prediction level and an unknown byte) followed by two little-endian words: the
/// filter level and the slew rate, then the steering torque assist at byte 11.
struct Settings4Block
{
    quint8 reportId;                ///< kSettingsReportId
    quint8 selector;                ///< kSelectorExtended
    quint8 subSelector;             ///< kSubSelectorSignal
    quint8 unknown03;               ///< The driver always sends 0x00.
    quint8 unknown04;               ///< The driver always sends 0x07.
    quint8 dynamicPredictionLevel;
    quint8 unknown06;
    quint8 filterLevel;             ///< Low byte of the filter word.
    quint8 unknown08;               ///< Filter level high byte?
    quint8 slewRateControl;         ///< Low byte of the slew-rate word.
    quint8 unknown10;
    quint8 steeringTorqueAssist;    ///< Mirrors status byte 54.
};

inline constexpr int kTelemetryReportSize = 62;
struct TelemetryBlock
{
    quint8 reportId = 0x91;
    quint8 unknown01 = 0x04;
    quint8 unknown02 = 0x00;
    quint16 rpm = 0;
    quint8 absActive : 1 = 0;
    quint8 tcActive : 1 = 0;
    quint8 speedLimiterActive : 1 = 0;
    quint8 unknown05_1 : 1 = 0;
    quint8 rpmLeds : 4 = 10; // from 10 (no leds) to 0 (all leds)
    quint8 unknown06 = 0x10;
    quint8 unfilteredBrake = 0;

    // 0x08
    quint8 unfilteredThrottle = 0;
    quint8 unfilteredClutch = 100;
    quint8 unknown10 = 0;
    quint8 unknown11 = 0;
    quint8 unknown12 = 0;
    quint8 unknown13 = 0;
    quint8 rpmPercent = 0;
    quint8 currentLap = 0;

    // 0x10
    // 0 none, 1 green, 2 blue, 3 yellow, 4 black, 5 white
    // Checkered, orange and red have no slot here, so they go out as none.
    quint8 flag = 0;
    quint16 elapsedTimeMinutes = 0;
    quint8 elapsedTimeSeconds = 0;
    quint32 elapsedTimeMs : 24 = 0;
    quint32 unknown23 : 8 = 0;

    // 0x18
    quint8 unknown24 = 0;
    quint8 unknown25 = 0;
    quint8 unknown26 = 0;
    quint8 unknown27 = 0;
    quint8 unknown28 = 0;
    quint8 unknown29 = 0;
    quint8 unknown30 = 0;
    quint8 unknown31 = 0;

    // 0x20
    quint8 unknown32 = 0xef; // maybe wheel temp?
    quint8 unknown33 = 0xef; // maybe wheel temp?
    quint8 unknown34 = 0xef; // maybe wheel temp?
    quint8 unknown35 = 0xef; // maybe wheel temp?
    quint8 unknown36 = 0x01;
    quint8 tirePressureFL = 0x01; // PSI x 10
    quint8 tirePressureFR = 0x01; // PSI x 10
    quint8 tirePressureRL = 0x01; // PSI x 10

    // 0x28
    quint8 tirePressureRR = 0x01; // PSI x 10
    quint8 unknown41 = 0;
    quint8 unknown42 = 0;
    quint8 engineWaterTempCelcius = 0;
    quint8 engineOilTempCelcius = 0;
    quint8 unknown45 = 0;
    quint8 unknown46 = 0;
    quint16 brakeTempFL : 12 = 0;
    quint16 brakeTempFR : 12 = 0;
    quint8 unknown50 = 0; // seems related to brake RL, but only the most significant 4 bits??
    quint16 brakeTempRR : 12 = 0;
    quint16 unknown52 : 4 = 0;
    quint8 unknown53 = 0;
    quint8 unknown54 = 0;
    quint8 unknown55 = 0;

    // 0x38
    quint8 unknown56 = 0;
    quint8 unknown57 = 0;
    quint8 unknown58 = 0;
    quint8 unknown59 = 0;
    quint8 unknown60 = 0;
    quint8 unknown61 = 0;
};

#pragma pack(pop)

/// One poll's worth of the base: what it is configured to do, and what it is
/// doing. Host-side, so deliberately outside the packing above.
///
/// The two reports are read together but they are not equally certain. `0x81`
/// has to answer or there is nothing to show at all; `0x23` has never been seen
/// answered by real hardware, so its absence is an ordinary outcome rather than
/// a failure, and `liveValid` is what says which happened.
///
/// Trivially copyable on purpose: it crosses to the GUI thread by queued signal,
/// and AlphaEvoSettings decides what changed with a memcmp. Anything with a
/// constructor in here — a QString holding a failure reason, say — would break
/// that quietly, which is why readStatus() reports that through a parameter.
struct StatusSnapshot
{
    StatusReport settings{};    ///< GET 0x81. Present whenever the read succeeded.
    BaseStatusReport live{};    ///< GET 0x23. Meaningful only when liveValid.
    bool liveValid = false;
};

// The structs above are only correct if they are laid out exactly as the driver
// lays them out. #pragma pack(1) gets us there, and these turn any drift — a
// reordered field, a compiler that ignores the pragma, a mistyped width — into
// a compile error rather than a wheelbase receiving the wrong settings.
static_assert(sizeof(StatusReport) == 64, "status report must be exactly 64 bytes");
static_assert(offsetof(StatusReport, maxAngle) == 2);
static_assert(offsetof(StatusReport, ffStrength) == 4);
static_assert(offsetof(StatusReport, mechanicalCentering) == 8);
static_assert(offsetof(StatusReport, mechanicalDamper) == 9);
static_assert(offsetof(StatusReport, mechanicalFriction) == 11);
static_assert(offsetof(StatusReport, gameCentering) == 12);
static_assert(offsetof(StatusReport, gameInertia) == 13);
static_assert(offsetof(StatusReport, gameDamper) == 14);
static_assert(offsetof(StatusReport, gameFriction) == 15);
static_assert(offsetof(StatusReport, angleLock) == 16);
static_assert(offsetof(StatusReport, feedbackDetail) == 18);
static_assert(offsetof(StatusReport, mechanicalInertia) == 22);
static_assert(offsetof(StatusReport, syncSoftLock) == 21);
static_assert(offsetof(StatusReport, torqueLimit) == 23);
static_assert(offsetof(StatusReport, ringLight) == 47);
static_assert(offsetof(StatusReport, dynamicPredictionLevel) == 48);
static_assert(offsetof(StatusReport, filterLevel) == 50);
static_assert(offsetof(StatusReport, slewRateControl) == 52);
static_assert(offsetof(StatusReport, steeringTorqueAssist) == 54);
static_assert(offsetof(StatusReport, wheelChannel) == 55);
static_assert(offsetof(StatusReport, powerSupplyVoltage) == 56);
static_assert(offsetof(StatusReport, firmwareVersion) == 58);

static_assert(sizeof(BaseStatusReport) == 18, "the base status reply is eighteen bytes");
static_assert(offsetof(BaseStatusReport, motorTemp) == 3);
static_assert(offsetof(BaseStatusReport, mosfetTemp) == 4);
static_assert(offsetof(BaseStatusReport, feedbackTorque) == 6);
static_assert(offsetof(BaseStatusReport, input) == 8);
static_assert(offsetof(BaseStatusReport, wheelId) == 10);
static_assert(offsetof(BaseStatusReport, busVoltage) == 13);
static_assert(offsetof(BaseStatusReport, version) == 15);
static_assert(offsetof(BaseStatusReport, flags) == 16);

static_assert(sizeof(Settings1Block) == 16);
static_assert(offsetof(Settings1Block, maxAngle) == 2);
static_assert(offsetof(Settings1Block, ffStrength) == 4);
static_assert(offsetof(Settings1Block, wheelRotationSpeed) == 7);
static_assert(offsetof(Settings1Block, centerDamper) == 10);
static_assert(offsetof(Settings1Block, gameFriction) == 15);

static_assert(sizeof(Settings2Block) == 10);
static_assert(offsetof(Settings2Block, angleLock) == 2);
static_assert(offsetof(Settings2Block, feedbackDetail) == 4);
static_assert(offsetof(Settings2Block, angleLockStrength) == 6);
static_assert(offsetof(Settings2Block, syncSoftLock) == 7);
static_assert(offsetof(Settings2Block, mechanicalInertia) == 8);
static_assert(offsetof(Settings2Block, torqueLimit) == 9);

static_assert(sizeof(Settings3Block) == 6);
static_assert(offsetof(Settings3Block, ringLight) == 5);

static_assert(sizeof(Settings4Block) == 12);
static_assert(offsetof(Settings4Block, dynamicPredictionLevel) == 5);
static_assert(offsetof(Settings4Block, filterLevel) == 7);
static_assert(offsetof(Settings4Block, slewRateControl) == 9);
static_assert(offsetof(Settings4Block, steeringTorqueAssist) == 11);

// The telemetry block is nearly half bit-fields, and three of its runs land
// where the offset comments say only if the compiler packs them contiguously
// across byte boundaries. offsetof() is not allowed on a bit-field, so each run
// is pinned by asserting the plain field on the far side of it.
static_assert(sizeof(TelemetryBlock) == kTelemetryReportSize);
static_assert(offsetof(TelemetryBlock, rpm) == 3);
static_assert(offsetof(TelemetryBlock, unknown06) == 6);  // past the flags byte
static_assert(offsetof(TelemetryBlock, unfilteredBrake) == 7);
static_assert(offsetof(TelemetryBlock, unfilteredThrottle) == 8);
static_assert(offsetof(TelemetryBlock, rpmPercent) == 14);
static_assert(offsetof(TelemetryBlock, currentLap) == 15);
static_assert(offsetof(TelemetryBlock, flag) == 16);
static_assert(offsetof(TelemetryBlock, unknown24) == 24);  // past elapsedTimeMs:24
static_assert(offsetof(TelemetryBlock, unknown32) == 32);
static_assert(offsetof(TelemetryBlock, tirePressureRR) == 40);
static_assert(offsetof(TelemetryBlock, unknown50) == 50);  // past the 12-bit brake pair
static_assert(offsetof(TelemetryBlock, unknown53) == 53);  // past brakeTempRR + unknown52
static_assert(offsetof(TelemetryBlock, unknown56) == 56);

// Bit-field allocation order follows the target's byte order on GCC and Clang:
// LSB-first here, MSB-first on a big-endian machine. Every run in TelemetryBlock
// — the flags-and-LED byte, elapsedTimeMs:24, and the three 12-bit brake
// temperatures — is written assuming the former, and no amount of byte-swapping
// repairs it, because a bit-field is not an integer you can hand to
// qToLittleEndian. The offsetof asserts above cannot see it either: the plain
// members bracketing each run keep their offsets whichever way the bits went. So
// refuse the build rather than send a wheelbase a report that is wrong in a way
// nothing would report.
static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN,
              "TelemetryBlock's bit-fields are laid out LSB-first; a big-endian "
              "port must pack them by hand rather than byte-swap them.");

/// The multi-byte *integer* fields are little-endian on the wire. Read and write
/// them through these rather than touching the members directly: on this target
/// they compile away, and they are what a big-endian port would have to make
/// correct.
///
/// They do not cover the bit-fields, and cannot — see the assert above, which is
/// what stands in for them there.
inline quint16 readLe(quint16 wire) { return qFromLittleEndian(wire); }
inline qint16 readLe(qint16 wire) { return qFromLittleEndian(wire); }
inline quint32 readLe(quint32 wire) { return qFromLittleEndian(wire); }
inline qint32 readLe(qint32 wire) { return qFromLittleEndian(wire); }
inline quint16 writeLe(quint16 host) { return qToLittleEndian(host); }
inline qint16 writeLe(qint16 host) { return qToLittleEndian(host); }
inline quint32 writeLe(quint32 host) { return qToLittleEndian(host); }
inline qint32 writeLe(qint32 host) { return qToLittleEndian(host); }

} // namespace hidcenter::simagic_alpha_evo

/// The status crosses from the I/O thread to the GUI thread through a queued
/// signal, which needs it to be a metatype. It is a packed POD, so copying it is
/// a memcpy and nothing here costs anything.
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::StatusReport)

/// What a poll actually delivers, and so what crosses the thread boundary.
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::StatusSnapshot)
