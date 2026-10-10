// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoBlocks.h"

#include "core/usb/IUsbDeviceHidInterface.h"

#include <QCoreApplication>
#include <QStringList>

#include <array>
#include <cstring>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Pack a block into the 64-byte transfer every report in this protocol uses.
template <typename Block>
QByteArray toReport(const Block &block, unsigned int reportSize = kReportSize)
{
    QByteArray buffer(reportSize, '\0');
    std::memcpy(buffer.data(), &block, qMin(sizeof(block), reportSize));
    return buffer;
}

/// TelemetryBlock's flag byte holds exactly five values. Values with no slot there
/// go out as none.
quint8 wireFlag(TelemetryFlag flag)
{
    switch (flag) {
    case TelemetryFlag::Green:
        return 1;
    case TelemetryFlag::Blue:
        return 2;
    case TelemetryFlag::Yellow:
        return 3;
    case TelemetryFlag::Black:
        return 4;
    case TelemetryFlag::White:
        return 5;
    case TelemetryFlag::None:
    case TelemetryFlag::Checkered:
    case TelemetryFlag::Orange:
    case TelemetryFlag::Red:
        return 0;
    }
    return 0;
}

} // namespace

namespace {

/// The live-telemetry half of readStatus(). Never fails the caller; it only
/// reports whether there was anything to have.
bool readBaseStatus(IUsbDeviceHidInterface *device, BaseStatusReport &out, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    QByteArray buffer;
    // 64 like everything else here, though only eighteen bytes are read.
    if (!device->getFeatureReport(kBaseStatusReportId, kReportSize, buffer))
        return fail(device->lastError());

    // Only the part that is parsed has to be there. Asking for the
    // full 64 and demanding it back would reject a base that answers correctly
    // in a shorter report.
    if (buffer.size() < int(sizeof(BaseStatusReport))) {
        return fail(
            QCoreApplication::translate("AlphaEvo", "Short base status report: %1 of %2 bytes.")
                .arg(buffer.size())
                .arg(sizeof(BaseStatusReport)));
    }

    if (quint8(buffer.at(0)) != kBaseStatusReportId) {
        return fail(
            QCoreApplication::translate("AlphaEvo",
                                        "Unexpected report id 0x%1 in the base status reply.")
                .arg(quint8(buffer.at(0)), 2, 16, QLatin1Char('0')));
    }

    std::memcpy(&out, buffer.constData(), sizeof(out));
    return true;
}

} // namespace

bool readStatus(IUsbDeviceHidInterface *device, StatusSnapshot &out, QString *error, bool readLive,
                QString *liveError)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    out.liveValid = false;

    if (!device)
        return fail(QCoreApplication::translate("AlphaEvo", "The device is not open."));

    QByteArray buffer;
    // Explicit length: the base does not declare these reports in its
    // descriptor, so there is nothing to derive a size from.
    if (!device->getFeatureReport(kStatusReportId, kReportSize, buffer))
        return fail(device->lastError());

    if (buffer.size() < kReportSize) {
        return fail(QCoreApplication::translate("AlphaEvo", "Short status report: %1 of %2 bytes.")
                        .arg(buffer.size())
                        .arg(kReportSize));
    }

    if (quint8(buffer.at(0)) != kStatusReportId) {
        return fail(QCoreApplication::translate("AlphaEvo",
                                                "Unexpected report id 0x%1 in the status reply.")
                        .arg(quint8(buffer.at(0)), 2, 16, QLatin1Char('0')));
    }

    std::memcpy(&out.settings, buffer.constData(), sizeof(out.settings));

    // Deliberately after the settings are already stored: whatever happens next,
    // the caller keeps what it came for.
    if (readLive)
        out.liveValid = readBaseStatus(device, out.live, liveError);

    return true;
}

QByteArray composeSettings1(const StatusReport &status, const SettingValues &changes)
{
    Settings1Block block = {};
    block.reportId = kSettingsReportId;
    block.selector = kSelectorSettings1;
    // Copied straight across in wire form: we do not interpret these, so there
    // is no reason to byte-swap them twice.
    block.maxAngle = status.maxAngle;
    block.ffStrength = status.ffStrength;
    block.unknown06 = 0x02;       // What the driver sends here, not the status byte.
    block.wheelRotationSpeed = status.wheelRotationSpeed;
    block.mechanicalCentering = status.mechanicalCentering;
    block.mechanicalDamper = status.mechanicalDamper;
    block.centerDamper = status.centerDamper;
    block.mechanicalFriction = status.mechanicalFriction;
    block.gameCentering = status.gameCentering;
    block.gameInertia = status.gameInertia;
    block.gameDamper = status.gameDamper;
    block.gameFriction = status.gameFriction;

    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const int value = clampToSpec(it.key(), it.value());
        switch (it.key()) {
        case Setting::FfStrength:
            block.ffStrength = writeLe(qint16(value));
            break;
        case Setting::MechanicalCentering:
            block.mechanicalCentering = quint8(value);
            break;
        case Setting::MechanicalDamper:
            block.mechanicalDamper = quint8(value);
            break;
        case Setting::MechanicalFriction:
            block.mechanicalFriction = quint8(value);
            break;
        case Setting::GameCentering:
            block.gameCentering = quint8(value);
            break;
        case Setting::GameInertia:
            block.gameInertia = quint8(value);
            break;
        case Setting::GameDamper:
            block.gameDamper = quint8(value);
            break;
        case Setting::GameFriction:
            block.gameFriction = quint8(value);
            break;
        case Setting::CenterDamper:
            block.centerDamper = quint8(value);
            break;
        case Setting::MaxAngle:
            block.maxAngle = writeLe(quint16(value));
            break;
        case Setting::WheelRotationSpeed:
            block.wheelRotationSpeed = quint8(value);
            break;
        default:
            break; // Belongs to another block.
        }
    }

    return toReport(block);
}

QByteArray composeSettings2(const StatusReport &status, const SettingValues &changes)
{
    Settings2Block block = {};
    block.reportId = kSettingsReportId;
    block.selector = kSelectorSettings2;
    // Wire form straight across, unswapped, for the same reason as in
    // composeSettings1: this value is never interpreted on the way.
    block.angleLock = status.angleLock;
    block.feedbackDetail = status.feedbackDetail;
    block.unknown05 = status.unknown19;
    block.angleLockStrength = status.angleLockStrength;
    block.syncSoftLock = status.syncSoftLock;
    block.mechanicalInertia = status.mechanicalInertia;
    block.torqueLimit = status.torqueLimit;

    // The base clamps the angle lock to the maximum rotation. Take that maximum
    // from this same batch when it is also being changed — clamping the new
    // lock against the old maximum is wrong in both directions.
    const int maxAngle = changes.contains(Setting::MaxAngle)
                             ? clampToSpec(Setting::MaxAngle, changes.value(Setting::MaxAngle))
                             : int(readLe(status.maxAngle));

    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const int value = clampToSpec(it.key(), it.value());
        switch (it.key()) {
        case Setting::FeedbackDetail:
            block.feedbackDetail = quint8(value);
            break;
        case Setting::MechanicalInertia:
            block.mechanicalInertia = quint8(value);
            break;
        case Setting::AngleLock:
            block.angleLock = writeLe(quint16(qMin(value, maxAngle)));
            break;
        case Setting::AngleLockStrength:
            block.angleLockStrength = quint8(value);
            break;
        default:
            break;
        }
    }

    return toReport(block);
}

QByteArray composeSettings3(const StatusReport &status, const SettingValues &changes)
{
    Settings3Block block = {};
    block.reportId = kSettingsReportId;
    block.selector = kSelectorExtended;
    block.subSelector = kSubSelectorRingLight;
    block.unknown03 = 0x00;
    block.unknown04 = 0x01;
    block.ringLight = status.ringLight;

    // The enable bit and the brightness share the byte, so each change touches
    // only its own bits: switching the light off must not dim it to zero, and
    // dimming it must not switch it off.
    if (changes.contains(Setting::RingLight)) {
        if (changes.value(Setting::RingLight) != 0)
            block.ringLight |= kRingLightEnabledBit;
        else
            block.ringLight &= quint8(~kRingLightEnabledBit);
    }
    if (changes.contains(Setting::RingLightBrightness)) {
        const int brightness = clampToSpec(Setting::RingLightBrightness,
                                           changes.value(Setting::RingLightBrightness));
        block.ringLight = quint8((block.ringLight & kRingLightEnabledBit)
                                 | (brightness & kRingLightBrightnessMask));
    }

    return toReport(block);
}

QByteArray composeSettings4(const StatusReport &status, const SettingValues &changes)
{
    Settings4Block block = {};
    block.reportId = kSettingsReportId;
    block.selector = kSelectorExtended;
    block.subSelector = kSubSelectorSignal;
    block.unknown03 = 0x00;
    block.unknown04 = 0x07;
    block.dynamicPredictionLevel = status.dynamicPredictionLevel;
    block.unknown06 = status.unknown49;
    block.filterLevel = status.filterLevel;
    block.unknown08 = status.unknown51;
    block.slewRateControl = status.slewRateControl;
    block.unknown10 = status.unknown53;
    block.steeringTorqueAssist = status.steeringTorqueAssist;

    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const int value = clampToSpec(it.key(), it.value());
        switch (it.key()) {
        case Setting::DynamicPredictionLevel:
            block.dynamicPredictionLevel = quint8(value);
            break;
        case Setting::SteeringTorqueAssist:
            block.steeringTorqueAssist = quint8(value);
            break;
        case Setting::FilterLevel:
            block.filterLevel = quint8(value);
            break;
        case Setting::SlewRateControl:
            block.slewRateControl = quint8(value);
            break;
        default:
            break;
        }
    }

    return toReport(block);
}

QByteArray composeTelemetry(const SimagicTelemetry &data)
{
    // Braced, so the two fields with nothing to fill them get their defaults
    // rather than whatever was on the stack. Everything else is already
    // defaulted in the struct.
    TelemetryBlock block = {};

    block.rpm = writeLe(data.rpm);
    block.rpmPercent = data.rpmPercent;
    block.rpmLeds = qBound(0u, data.rpmLeds, 10u);
    block.tcActive = data.tractionControl ? 1 : 0;
    block.absActive = data.abs ? 1 : 0;
    block.speedLimiterActive = data.pitLimiter ? 1 : 0;
    block.flag = wireFlag(data.flag);

    return toReport(block, kTelemetryReportSize);
}

int valueOf(const StatusReport &status, Setting setting)
{
    switch (setting) {
    case Setting::FfStrength:
        return readLe(status.ffStrength);
    case Setting::MechanicalCentering:
        return status.mechanicalCentering;
    case Setting::MechanicalDamper:
        return status.mechanicalDamper;
    case Setting::MechanicalFriction:
        return status.mechanicalFriction;
    case Setting::MechanicalInertia:
        return status.mechanicalInertia;
    case Setting::CenterDamper:
        return status.centerDamper;
    case Setting::MaxAngle:
        return readLe(status.maxAngle);
    case Setting::AngleLock:
        return readLe(status.angleLock);
    case Setting::AngleLockStrength:
        return status.angleLockStrength;
    case Setting::WheelRotationSpeed:
        return status.wheelRotationSpeed;
    case Setting::RingLight:
        return (status.ringLight & kRingLightEnabledBit) ? 1 : 0;
    case Setting::RingLightBrightness:
        return status.ringLight & kRingLightBrightnessMask;
    case Setting::GameCentering:
        return status.gameCentering;
    case Setting::GameInertia:
        return status.gameInertia;
    case Setting::GameDamper:
        return status.gameDamper;
    case Setting::GameFriction:
        return status.gameFriction;
    case Setting::FeedbackDetail:
        return status.feedbackDetail;
    case Setting::DynamicPredictionLevel:
        return status.dynamicPredictionLevel;
    case Setting::SteeringTorqueAssist:
        return status.steeringTorqueAssist;
    case Setting::FilterLevel:
        return status.filterLevel;
    case Setting::SlewRateControl:
        return status.slewRateControl;
    }
    return 0;
}

void setValue(StatusReport &status, Setting setting, int value)
{
    const int clamped = clampToSpec(setting, value);

    switch (setting) {
    case Setting::FfStrength:
        status.ffStrength = writeLe(qint16(clamped));
        break;
    case Setting::MechanicalCentering:
        status.mechanicalCentering = quint8(clamped);
        break;
    case Setting::MechanicalDamper:
        status.mechanicalDamper = quint8(clamped);
        break;
    case Setting::MechanicalFriction:
        status.mechanicalFriction = quint8(clamped);
        break;
    case Setting::MechanicalInertia:
        status.mechanicalInertia = quint8(clamped);
        break;
    case Setting::CenterDamper:
        status.centerDamper = quint8(clamped);
        break;
    case Setting::MaxAngle:
        status.maxAngle = writeLe(quint16(clamped));
        break;
    case Setting::AngleLock:
        // Bounded by the maximum rotation, exactly as the base bounds it.
        status.angleLock = writeLe(quint16(qMin(clamped, int(readLe(status.maxAngle)))));
        break;
    case Setting::AngleLockStrength:
        status.angleLockStrength = quint8(clamped);
        break;
    case Setting::WheelRotationSpeed:
        status.wheelRotationSpeed = quint8(clamped);
        break;
    case Setting::RingLight:
        // Only bit 7 is ours; the brightness in the low bits must survive.
        if (clamped != 0)
            status.ringLight |= kRingLightEnabledBit;
        else
            status.ringLight &= quint8(~kRingLightEnabledBit);
        break;
    case Setting::RingLightBrightness:
        // Only bits 0-6 are ours; the enable bit must survive.
        status.ringLight = quint8((status.ringLight & kRingLightEnabledBit)
                                  | (clamped & kRingLightBrightnessMask));
        break;
    case Setting::GameCentering:
        status.gameCentering = quint8(clamped);
        break;
    case Setting::GameInertia:
        status.gameInertia = quint8(clamped);
        break;
    case Setting::GameDamper:
        status.gameDamper = quint8(clamped);
        break;
    case Setting::GameFriction:
        status.gameFriction = quint8(clamped);
        break;
    case Setting::FeedbackDetail:
        status.feedbackDetail = quint8(clamped);
        break;
    case Setting::DynamicPredictionLevel:
        status.dynamicPredictionLevel = quint8(clamped);
        break;
    case Setting::SteeringTorqueAssist:
        status.steeringTorqueAssist = quint8(clamped);
        break;
    case Setting::FilterLevel:
        status.filterLevel = quint8(clamped);
        break;
    case Setting::SlewRateControl:
        status.slewRateControl = quint8(clamped);
        break;
    }
}

// ---------------------------------------------------------------------------
// LED ring
// ---------------------------------------------------------------------------

namespace {

/// Packets captured setting the ring to one, two, three,
/// five, nine and ten colours.
///
/// These are the only ground truth this module has for the LED protocol, and
/// asserting the composer against them below is what stands in for a test suite.
/// They are here in the .cpp rather than the header so that the sixty-four byte
/// literals are compiled once rather than in every translation unit that wants
/// to send LEDs.
///
/// Do not tidy them into a loop or a generator. The point of a capture is that
/// it was not produced by our own encoder.
///
/// The first three are one colour repeated; the rest have a blue at the third
/// stop, which is a second witness to the byte order of `key_color` and the only
/// one where a channel swap could not hide behind a near-grey.
constexpr LedRgb kCaptureRed{0xD3, 0x20, 0x29};
constexpr LedRgb kCaptureBlue{0x2B, 0x4A, 0xCB};

constexpr std::array<quint8, kReportSize> kCaptureOneColour{
    0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x11, 0x01, 0x01, 0x08, 0x01, 0x2a, 0x0d, 0x10, 0x01,
    0x18, 0x01, 0x42, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x48, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

constexpr std::array<quint8, kReportSize> kCaptureTwoColours{
    0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x1b, 0x01, 0x01, 0x08, 0x01, 0x2a, 0x17, 0x10, 0x01,
    0x18, 0x01, 0x42, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0x90, 0x03, 0x15, 0xd3,
    0x20, 0x29, 0x00, 0x48, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

constexpr std::array<quint8, kReportSize> kCaptureThreeColours{
    0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x25, 0x01, 0x01, 0x08, 0x01, 0x2a, 0x21, 0x10, 0x01,
    0x18, 0x01, 0x42, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0x80, 0x02, 0x15, 0xd3,
    0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0x80, 0x04, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x48, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/// The five, nine and ten stop captures, each two reports.
///
/// These are the only fragmented packets we have, and they are what turned the
/// multi-frame header rules from a reading of the protocol into a checked one.
constexpr std::array<std::array<quint8, kReportSize>, 2> kCaptureFiveColours{{
    {
     0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x39, 0x02, 0x01, 0x08, 0x01, 0x2a, 0x35, 0x10, 0x01,
     0x18, 0x01, 0x42, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xa0, 0x01, 0x15, 0xd3,
     0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xc0, 0x02, 0x15, 0x2b, 0x4a, 0xcb, 0x00, 0x42, 0x08, 0x08,
     0xe0, 0x03, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0x80, 0x05, 0x15, 0xd3, 0x20, 0x29},
    {
     0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x39, 0x02, 0x02, 0x00, 0x48, 0x04, 0x35, 0x10, 0x01,
     0x18, 0x01, 0x42, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xa0, 0x01, 0x15, 0xd3,
     0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xc0, 0x02, 0x15, 0x2b, 0x4a, 0xcb, 0x00, 0x42, 0x08, 0x08,
     0xe0, 0x03, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0x80, 0x05, 0x15, 0xd3, 0x20, 0x29}
}};

constexpr std::array<std::array<quint8, kReportSize>, 2> kCaptureNineColours{{
    {
     0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x60, 0x02, 0x01, 0x08, 0x01, 0x2a, 0x5c, 0x10, 0x01,
     0x18, 0x01, 0x42, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x07, 0x08, 0x50, 0x15, 0xd3, 0x20,
     0x29, 0x00, 0x42, 0x08, 0x08, 0xa0, 0x01, 0x15, 0x2b, 0x4a, 0xcb, 0x00, 0x42, 0x08, 0x08, 0xf0,
     0x01, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xc0, 0x02, 0x15, 0xd3, 0x20, 0x29, 0x00},
    {
     0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x60, 0x02, 0x02, 0x42, 0x08, 0x08, 0x90, 0x03, 0x15,
     0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xe0, 0x03, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08,
     0x08, 0xb0, 0x04, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0x80, 0x05, 0x15, 0xd3, 0x20,
     0x29, 0x00, 0x48, 0x04, 0x29, 0x00, 0x42, 0x08, 0x08, 0xc0, 0x02, 0x15, 0xd3, 0x20, 0x29, 0x00}
}};

constexpr std::array<std::array<quint8, kReportSize>, 2> kCaptureTenColours{{
    {
     0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x6a, 0x02, 0x01, 0x08, 0x01, 0x2a, 0x66, 0x10, 0x01,
     0x18, 0x01, 0x42, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x07, 0x08, 0x50, 0x15, 0xd3, 0x20,
     0x29, 0x00, 0x42, 0x08, 0x08, 0xa0, 0x01, 0x15, 0x2b, 0x4a, 0xcb, 0x00, 0x42, 0x08, 0x08, 0xf0,
     0x01, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xc0, 0x02, 0x15, 0xd3, 0x20, 0x29, 0x00},
    {
     0x80, 0x1e, 0x01, 0x08, 0x02, 0x03, 0x00, 0x6a, 0x02, 0x02, 0x42, 0x08, 0x08, 0x90, 0x03, 0x15,
     0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0xe0, 0x03, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08,
     0x08, 0xb0, 0x04, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x42, 0x08, 0x08, 0x80, 0x05, 0x15, 0xd3, 0x20,
     0x29, 0x00, 0x42, 0x08, 0x08, 0xd0, 0x05, 0x15, 0xd3, 0x20, 0x29, 0x00, 0x48, 0x04, 0x29, 0x00}
}};

/// Compose `count` stops of the captured red and compare against the capture.
///
/// Device model 8 is what the captures carry, an Alpha Evo.
constexpr bool matchesCapture(int count, const std::array<quint8, kReportSize> &expected)
{
    LedRingColours ring;
    ring.count = quint8(count);
    for (int i = 0; i < count; ++i)
        ring.stops[std::size_t(i)] = kCaptureRed;

    const auto packet = composeLedRing(ring, 8);
    return packet.ok && packet.frameCount == 1 && packet.frames[0] == expected;
}

/// The ring the multi-stop captures were taken from: the captured red, with the
/// third stop set to the blue.
constexpr LedRingColours capturedRing(int count)
{
    LedRingColours ring;
    ring.count = quint8(count);
    for (int i = 0; i < count; ++i)
        ring.stops[std::size_t(i)] = (i == 2) ? kCaptureBlue : kCaptureRed;
    return ring;
}

/// Compare against a capture that spans more than one report.
///
/// Only the bytes the header accounts for are compared. The captured sender clears its
/// transfer buffer once per packet rather than once per frame, so past a final
/// frame's payload sits whatever the frame before it left there — the five-stop
/// capture ends with fifty-one bytes of its own first frame. Comparing that
/// would be comparing against leftovers, and the base ignores it too, since
/// payload_total_size is what says where the message ends.
template <std::size_t N>
constexpr bool matchesFragmentedCapture(
    const LedRingColours &ring, const std::array<std::array<quint8, kReportSize>, N> &expected)
{
    const auto packet = composeLedRing(ring, 8);
    if (!packet.ok || packet.frameCount != int(N))
        return false;

    for (int f = 0; f < int(N); ++f) {
        // The same arithmetic composeLedRing cuts its chunks with.
        const int chunk = qMin(kLedFramePayload, packet.payloadSize - f * kLedFramePayload);
        const int real = int(sizeof(LedPacketHeader)) + chunk;
        for (int i = 0; i < real; ++i) {
            if (packet.frames[std::size_t(f)][std::size_t(i)]
                != expected[std::size_t(f)][std::size_t(i)]) {
                return false;
            }
        }
    }
    return true;
}

// If one of these fails, the composer no longer produces what the wheel was
// actually sent. Nothing downstream will catch that: a wrong packet is accepted
// by setFeatureReport and simply does not light the ring.
static_assert(matchesCapture(1, kCaptureOneColour), "one-colour packet must match the capture");
static_assert(matchesCapture(2, kCaptureTwoColours), "two-colour packet must match the capture");
static_assert(matchesCapture(3, kCaptureThreeColours),
              "three-colour packet must match the capture");

// The fragmented ones. Their final frames carry 3, 42 and 52 payload bytes, so
// between them they pin a barely-used last report and a nearly-full one.
static_assert(matchesFragmentedCapture(capturedRing(5), kCaptureFiveColours),
              "five-colour packet must match the capture");
static_assert(matchesFragmentedCapture(capturedRing(9), kCaptureNineColours),
              "nine-colour packet must match the capture");
static_assert(matchesFragmentedCapture(capturedRing(10), kCaptureTenColours),
              "ten-colour packet must match the capture");

} // namespace

namespace {

/// Setting the wheel's LED brightness to 3 %: the four reports sent for every
/// change, in order.
///
/// The only ground truth for the wheel configuration, as the LED captures above
/// are for the ring, and under the same rule: typed out from the capture, never
/// generated. They are also where every byte this module does not own comes from
/// — see defaultWheelConfig().
///
/// Taken from `20260914_wheel_brightness_report91_0_1_2_3.pcapng`, frames 13 to
/// 16. Between the brightness steps of that capture nothing changes but byte 52
/// of the last report.
constexpr std::array<std::array<quint8, kWheelConfigReportSize>, kWheelConfigChunkCount>
    kCaptureWheelConfigBrightness3{{
        {0x91, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
         0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x00, 0x13, 0x00, 0x15, 0x16, 0x22,
         0x18, 0x19, 0x00, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
         0x25, 0x26, 0x27, 0x4c, 0x4c, 0x4e, 0x4f, 0x52, 0x54, 0x56, 0x58, 0x59, 0x5a,
         0x33, 0x60, 0x55, 0x13, 0x55, 0x20, 0x33, 0x20, 0x21, 0x72},
        {0x91, 0x01, 0x0a, 0x21, 0x8e, 0x05, 0x05, 0x04, 0x02, 0x01, 0x0c, 0x4f, 0x06,
         0xa2, 0x95, 0x61, 0x25, 0x55, 0x1e, 0x53, 0x59, 0xe2, 0x45, 0x45, 0x45, 0x45,
         0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x45, 0x45,
         0x45, 0x95, 0x95, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x95, 0x95, 0x95,
         0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x95, 0x95, 0x95, 0x95},
        {0x91, 0x02, 0x15, 0x15, 0x15, 0x15, 0x15, 0x95, 0x95, 0x95, 0x95, 0x95, 0x15,
         0x15, 0x15, 0x15, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x15, 0x15, 0x15, 0x95,
         0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x15, 0x15, 0x95, 0x95, 0x95, 0x95, 0x95,
         0x95, 0x95, 0x95, 0x15, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95,
         0x08, 0x01, 0xfe, 0x26, 0xfa, 0x22, 0x08, 0x21, 0x14, 0x05},
        {0x91, 0x03, 0x00, 0x0c, 0x3a, 0x01, 0x99, 0x17, 0x00, 0x2e, 0x50, 0x0e, 0x00,
         0x01, 0x8a, 0xda, 0xe0, 0x44, 0x03, 0x8b, 0x50, 0x03, 0xe0, 0x07, 0x96, 0xa2,
         0xf1, 0x94, 0x00, 0xa5, 0xe0, 0x01, 0x00, 0x06, 0x61, 0x61, 0x42, 0xfc, 0x82,
         0xac, 0x30, 0x0f, 0xa0, 0x0a, 0x4a, 0xa4, 0xff, 0xe5, 0x30, 0x0f, 0x98, 0x03,
         0x03, 0x01, 0x22, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    }};

/// The last report of the same capture's first step, at 0 %. The other three
/// reports of that step are identical to the ones above, so this is the one
/// worth having: it is a second, independent witness to where the brightness is.
constexpr std::array<quint8, kWheelConfigReportSize> kCaptureWheelConfigChunk3Brightness0{
    0x91, 0x03, 0x00, 0x0c, 0x3a, 0x01, 0x99, 0x17, 0x00, 0x2e, 0x50, 0x0e, 0x00,
    0x01, 0x8a, 0xda, 0xe0, 0x44, 0x03, 0x8b, 0x50, 0x03, 0xe0, 0x07, 0x96, 0xa2,
    0xf1, 0x94, 0x00, 0xa5, 0xe0, 0x01, 0x00, 0x06, 0x61, 0x61, 0x42, 0xfc, 0x82,
    0xac, 0x30, 0x0f, 0xa0, 0x0a, 0x4a, 0xa4, 0xff, 0xe5, 0x30, 0x0f, 0x98, 0x03,
    0x00, 0x01, 0x22, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/// The block the capture carried, with the report headers taken off.
constexpr WheelConfig capturedWheelConfig()
{
    WheelConfig config;
    for (int chunk = 0; chunk < kWheelConfigChunkCount; ++chunk) {
        for (int i = 0; i < kWheelConfigChunkSize; ++i) {
            config.bytes[std::size_t(chunk * kWheelConfigChunkSize + i)] =
                kCaptureWheelConfigBrightness3[std::size_t(chunk)][std::size_t(2 + i)];
        }
    }
    return config;
}

/// Compose every report of the captured block at `brightness` and compare it
/// with what was captured. The headers and the chunk order are what this checks;
/// the payload is the capture's own, so it could hardly disagree.
constexpr bool matchesWheelConfigCapture()
{
    WheelConfig config = capturedWheelConfig();
    config.setLedBrightness(3);
    for (int chunk = 0; chunk < kWheelConfigChunkCount; ++chunk) {
        if (composeWheelConfigChunk(config, chunk)
            != kCaptureWheelConfigBrightness3[std::size_t(chunk)]) {
            return false;
        }
    }
    return true;
}

/// The same block set to 0 % must be the other step's last report. This is the
/// check that pins the brightness to its byte: a wrong offset would still pass
/// the one above, and not this.
constexpr bool matchesWheelConfigBrightness0()
{
    WheelConfig config = capturedWheelConfig();
    config.setLedBrightness(0);
    return composeWheelConfigChunk(config, wheelConfigChunkOf(kWheelConfigLedBrightnessOffset))
           == kCaptureWheelConfigChunk3Brightness0;
}

// As for the ring: nothing downstream notices a wrong report. The base takes it
// and the wheel simply does not change.
static_assert(matchesWheelConfigCapture(),
              "the wheel configuration reports must match the capture");
static_assert(matchesWheelConfigBrightness0(),
              "the brightness must be where the 0 % capture says it is");
static_assert(capturedWheelConfig().ledBrightness() == 3,
              "the captured block was taken at 3 %");
static_assert(!capturedWheelConfig().clutchSynced() && capturedWheelConfig().clutchRatio() == 34,
              "the captured block has independent clutch paddles, and a ratio of 34 %");

/// Changing one button colour on a GTC, from off to orange: report 0 before
/// and after. From `20260914_wheel_ledswitch_report91.pcapng`, frames 1 and 5,
/// a later session than the block above, which is why the unused mapping bytes
/// differ from it. Each step of that capture changes one byte and nothing else.
constexpr std::array<quint8, kWheelConfigReportSize> kCaptureLedSwitchChunk0Before{
    0x91, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0xda, 0x13, 0x96, 0x15, 0x16, 0xa5,
    0x18, 0x19, 0x05, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0x27, 0x4c, 0x4c, 0x4e, 0x4f, 0x52, 0x54, 0x56, 0x58, 0x59, 0x5a,
    0x33, 0x60, 0x10, 0x90, 0x90, 0x20, 0x33, 0x90, 0x21, 0x72};
constexpr std::array<quint8, kWheelConfigReportSize> kCaptureLedSwitchChunk0After{
    0x91, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0xda, 0x13, 0x96, 0x15, 0x16, 0xa5,
    0x18, 0x19, 0x05, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0x27, 0x4c, 0x4c, 0x4e, 0x4f, 0x52, 0x54, 0x56, 0x58, 0x59, 0x5a,
    0x33, 0x60, 0x10, 0x20, 0x90, 0x20, 0x33, 0x90, 0x21, 0x72};

/// Setting slot 3 to orange, lit all the time, on the "before" block has to give
/// exactly the "after" report: which byte, which nibble is the colour, and that
/// off is 9, all at once.
constexpr bool matchesLedSwitchCapture()
{
    WheelConfig config;
    for (int i = 0; i < kWheelConfigChunkSize; ++i)
        config.bytes[std::size_t(i)] = kCaptureLedSwitchChunk0Before[std::size_t(2 + i)];
    if (config.ledColour(3) != kWheelLedColourOff)
        return false;
    config.setLed(3, 2, kWheelLedTriggerStatic);
    return composeWheelConfigChunk(config, 0) == kCaptureLedSwitchChunk0After;
}

static_assert(matchesLedSwitchCapture(),
              "a colour change must reproduce the captured report 0 byte for byte");

// The LED slots, checked against something other than the bytes typed above.
// The ABS and TC windows are in slots 8 and 9, lit orange on ABS and purple on
// TC; the capture's block has exactly that there. Slot 1 is a button lit cyan
// all the time.
static_assert(capturedWheelConfig().ledColour(8) == 2
                  && capturedWheelConfig().ledTrigger(8) == kWheelLedTriggerAbs,
              "slot 8 is the ABS window: orange, lit on ABS");
static_assert(capturedWheelConfig().ledColour(9) == 7
                  && capturedWheelConfig().ledTrigger(9) == kWheelLedTriggerTc,
              "slot 9 is the TC window: purple, lit on TC");
static_assert(capturedWheelConfig().ledColour(1) == 6
                  && capturedWheelConfig().ledTrigger(1) == kWheelLedTriggerStatic,
              "slot 1 is a button lit cyan all the time");

/// The default mapping gives every part of a GTC the button whose number is its
/// input plus one: the first button, code 8, goes to input 7, and so on for all
/// 38 codes. The captured block has exactly
/// that at every input a GTC uses, and something else at each of the four it
/// does not, which nothing writes. So the byte is the button less one, and the
/// block is a GTC's with the default mappings.
constexpr bool capturedMappingsAreGtcDefaults()
{
    const WheelConfig config = capturedWheelConfig();
    for (int input = 0; input < kWheelConfigMappingCount; ++input) {
        if ((config.buttonMapping(input) == input + 1) != isGtcButtonInput(input))
            return false;
    }
    return true;
}

static_assert(capturedMappingsAreGtcDefaults(),
              "every input a GTC uses presses the button the default gives it");

/// The captured RPM lights, read the way the accessors read them, are an
/// ordinary shift light on a GTC's nine LEDs: the higher the threshold, the
/// more LEDs lit, up to all nine at pattern 0. Pairing thresholds with patterns
/// the other way round, or reading the table unreversed, would have the bar
/// shrink as the revs rise.
constexpr bool capturedRpmLightsGrowWithRevs()
{
    const WheelConfig config = capturedWheelConfig();
    const auto lit = [&config](int pattern) {
        int count = 0;
        for (int led = 0; led < 9; ++led)
            count += config.rpmLedColour(led, pattern) != kWheelLedColourOff ? 1 : 0;
        return count;
    };

    for (int pattern = 0; pattern + 1 < kWheelConfigRpmPatternCount; ++pattern) {
        if (config.rpmThreshold(pattern) < config.rpmThreshold(pattern + 1)
            || lit(pattern) < lit(pattern + 1)) {
            return false;
        }
    }
    return lit(0) == 9 && config.rpmThreshold(0) == 90 && config.rpmThreshold(9) == 76;
}

static_assert(capturedRpmLightsGrowWithRevs(),
              "the captured bar lights more LEDs from each higher threshold");
static_assert(capturedWheelConfig().rpmLedColour(0, 9) == 4
                  && capturedWheelConfig().rpmLedColour(8, 0) == 1
                  && !capturedWheelConfig().rpmLedFlashing(8, 0)
                  && capturedWheelConfig().rpmLedTrigger(8, 0) == 5,
              "the bar runs from green to red, not flashing, trigger 5 throughout");

} // namespace

WheelConfig defaultWheelConfig()
{
    return capturedWheelConfig();
}

QByteArray composeWheelConfigReport(const WheelConfig &config, int chunk)
{
    if (chunk < 0 || chunk >= kWheelConfigChunkCount)
        return {};

    const auto report = composeWheelConfigChunk(config, chunk);
    return QByteArray(reinterpret_cast<const char *>(report.data()), kWheelConfigReportSize);
}

QString describeWheelConfigChunk(const WheelConfig &config, int chunk)
{
    if (chunk == wheelConfigChunkOf(kWheelConfigLedBrightnessOffset))
        return QStringLiteral("LED brightness %1 %, clutch paddles %2, ratio %3 %")
            .arg(config.ledBrightness())
            .arg(config.clutchSynced() ? QStringLiteral("synced") : QStringLiteral("independent"))
            .arg(config.clutchRatio());

    if (chunk == wheelConfigChunkOf(kWheelConfigLedOffset)) {
        // The ten slots a GTC uses, as colour/trigger, and the buttons its eight
        // lit buttons press. The rest are left as captured and would only make
        // the line longer.
        QStringList leds;
        for (int led = 0; led < 10; ++led)
            leds << QStringLiteral("%1/%2").arg(config.ledColour(led)).arg(config.ledTrigger(led));
        QStringList buttons;
        for (int input = 0; input < 8; ++input)
            buttons << QString::number(config.buttonMapping(input));
        QStringList thresholds;
        for (int pattern = 0; pattern < kWheelConfigRpmPatternCount; ++pattern)
            thresholds << QString::number(config.rpmThreshold(pattern));
        return QStringLiteral("LED slots 0–9: %1; inputs 0–7 press %2; RPM patterns 0–9 from %3 %")
            .arg(leds.join(u' '), buttons.join(u' '), thresholds.join(u' '));
    }

    // Each LED whose patterns are in this report, as its colour number in
    // patterns 0 to 9, with a star after a flashing one.
    QStringList rpmLeds;
    int first = -1;
    for (int led = 0; led < kWheelConfigRpmLedCount; ++led) {
        if (wheelConfigChunkOf(WheelConfig::rpmLedOffset(led, 0)) != chunk)
            continue;
        if (first < 0)
            first = led;
        QString patterns;
        for (int pattern = 0; pattern < kWheelConfigRpmPatternCount; ++pattern) {
            patterns += QString::number(config.rpmLedColour(led, pattern), 16);
            if (config.rpmLedFlashing(led, pattern))
                patterns += u'*';
        }
        rpmLeds << patterns;
    }
    if (rpmLeds.isEmpty())
        return {};
    return QStringLiteral("RPM LEDs %1–%2 by pattern: %3")
        .arg(first)
        .arg(first + rpmLeds.size() - 1)
        .arg(rpmLeds.join(u' '));
}

QVector<QByteArray> composeLedRingReports(const LedRingColours &colours, quint8 deviceModel)
{
    const LedPacket packet = composeLedRing(colours, deviceModel, true);
    if (!packet.ok)
        return {};

    QVector<QByteArray> reports;
    reports.reserve(packet.frameCount);
    for (int i = 0; i < packet.frameCount; ++i) {
        const auto &frame = packet.frames[std::size_t(i)];
        reports.append(QByteArray(reinterpret_cast<const char *>(frame.data()), kReportSize));
    }
    return reports;
}

} // namespace hidcenter::simagic_alpha_evo
