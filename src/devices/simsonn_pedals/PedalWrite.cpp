// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalWrite.h"

#include <QByteArray>

#include <cstring>

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"

namespace hidcenter::simsonn_pedals {
namespace {

/// Report ID for a named command, and the shape of a `<letter>_pf` one. The
/// dispatch rebuilds the header by hand, so the name is always exactly these
/// four bytes.
constexpr char kReportCommand = 0x06;
constexpr int kReportSize = 64;
constexpr int kNameSize = 4;
constexpr int kPayloadStart = 1 + kNameSize;

/// The 23-byte `<x>_pf` payload, field by field. Offsets are into the payload.
constexpr int kPayloadPhysicalMinimum = 11;
constexpr int kPayloadPhysicalMaximum = 13;
constexpr int kPayloadEffectiveMinimum = 15;
constexpr int kPayloadEffectiveCentre = 17;
constexpr int kPayloadEffectiveMaximum = 19;
constexpr int kPayloadStartDeadzone = 21;
constexpr int kPayloadEndDeadzone = 22;

void writeInt16(QByteArray &report, int offset, qint16 value)
{
    report[offset] = char(quint8(quint16(value) & 0xFF));
    report[offset + 1] = char(quint8(quint16(value) >> 8));
}

} // namespace

QByteArray namedCommand(const char *name, const QByteArray &payload)
{
    QByteArray report(kReportSize, '\0');
    report[0] = kReportCommand;
    const QByteArray ascii(name);
    const int nameSize = qMin(int(ascii.size()), kReportSize - 1);
    std::memcpy(report.data() + 1, ascii.constData(), size_t(nameSize));
    const int payloadSize = qMin(int(payload.size()), kReportSize - 1 - nameSize);
    std::memcpy(report.data() + 1 + nameSize, payload.constData(), size_t(payloadSize));
    return report;
}

bool sendNamedCommand(IUsbDevice *device, QObject *owner, const UsbInterfaceInfo &interface,
                      const char *name, const QByteArray &payload)
{
    if (!device)
        return false;

    IUsbDeviceHidInterface *handle = device->openHidFor(owner, interface);
    if (!handle)
        return false;

    const bool written = handle->write(namedCommand(name, payload));
    if (!written) {
        qCWarning(hidcenterDevice).noquote()
            << "SIMSONN: writing" << name << "failed:" << handle->lastError();
    } else {
        qCInfo(hidcenterDevice).noquote()
            << "SIMSONN: wrote" << name << payload.toHex(' ');
    }

    device->releaseFor(owner, interface);
    return written;
}

int brakeForceMaximumKg(Elastomer elastomer)
{
    switch (elastomer) {
    case Elastomer::Soft:
        return 60;
    case Elastomer::Medium:
        return 100;
    case Elastomer::Hard:
        return 140;
    }
    return 60;
}

int brakeForceKg(const ConfigRead::AxisCalibration &calibration)
{
    // The inverse of brakePhysicalMaximum(): the travel above the floor is the
    // force divided by the part of it the start deadzone leaves.
    const double travel = double(calibration.physicalMaximum) - calibration.physicalMinimum;
    return qRound(travel * (1.0 - calibration.startDeadzone * 0.01) / kBrakeCountsPerKg);
}

qint16 brakePhysicalMaximum(int kg, qint16 physicalMinimum, quint8 startDeadzone)
{
    // Guarded against a deadzone that leaves nothing to divide by.
    double remaining = 1.0 - startDeadzone * 0.01;
    if (remaining <= 0.0)
        remaining = 0.01;
    const int top = int(double(kg) * kBrakeCountsPerKg / remaining) + physicalMinimum;
    return qint16(qBound(-32768, top, 32767));
}

QByteArray brakeForcePayload(const ConfigRead::AxisCalibration &calibration)
{
    QByteArray payload(10, '\0');
    writeInt16(payload, 0, calibration.physicalMaximum);
    writeInt16(payload, 2, calibration.effectiveMaximum);
    return payload;
}

QByteArray elastomerPayload(Elastomer elastomer, int forceKg,
                            const ConfigRead::AxisCalibration &calibration)
{
    // The spin box's value times the counts per kilogram, on top
    // of the brake's effective floor, truncated to 16 bits.
    const qint16 count =
        qint16(int(double(forceKg) * kBrakeCountsPerKg + double(calibration.effectiveMinimum)));

    QByteArray payload(10, '\0');
    writeInt16(payload, 0, count);
    payload[2] = char(elastomer);
    return payload;
}

QByteArray throttleCutPayload(const ThrottleCut &cut)
{
    QByteArray payload(3, '\0');
    payload[0] = cut.enabled ? 1 : 0;
    writeInt16(payload, 1, qint16(quint16(cut.tenths)));
    return payload;
}

std::optional<PedalRecord> storedRecord(const ConfigRead &config, int axis)
{
    const std::optional<ConfigRead::AxisCalibration> calibration = config.axis(axis);
    const std::optional<std::array<qint8, ConfigRead::kCurvePoints>> curve = config.curve(axis);
    if (!calibration || !curve)
        return {};
    return PedalRecord {*curve, *calibration};
}

void deriveEffectiveRange(PedalRecord &record)
{
    ConfigRead::AxisCalibration &c = record.calibration;

    // A plain int: a full-range axis spans 65534, which an int16 could not hold,
    //.
    const int span = int(c.physicalMaximum) - int(c.physicalMinimum);

    c.effectiveMinimum = qint16(
        int(double(int(c.startDeadzone) * span) * 0.01 + double(c.physicalMinimum)));
    c.effectiveMaximum = qint16(
        int(double(c.physicalMaximum) - double(int(c.endDeadzone) * span) * 0.01));
    c.effectiveCentre =
        qint16(c.effectiveMinimum + (c.effectiveMaximum - c.effectiveMinimum) / 2);
}

bool writePedalRecord(IUsbDevice *device, QObject *owner, const UsbInterfaceInfo &interface,
                      const PedalSpec &pedal, const PedalRecord &record)
{
    if (!device)
        return false;

    const ConfigRead::AxisCalibration &c = record.calibration;

    QByteArray report(kReportSize, '\0');
    report[0] = kReportCommand;
    report[1] = pedal.letter;
    report[2] = '_';
    report[3] = 'p';
    report[4] = 'f';

    for (int i = 0; i < ConfigRead::kCurvePoints; ++i)
        report[kPayloadStart + i] = char(record.curve[size_t(i)]);

    writeInt16(report, kPayloadStart + kPayloadPhysicalMinimum, c.physicalMinimum);
    writeInt16(report, kPayloadStart + kPayloadPhysicalMaximum, c.physicalMaximum);
    writeInt16(report, kPayloadStart + kPayloadEffectiveMinimum, c.effectiveMinimum);
    writeInt16(report, kPayloadStart + kPayloadEffectiveCentre, c.effectiveCentre);
    writeInt16(report, kPayloadStart + kPayloadEffectiveMaximum, c.effectiveMaximum);
    report[kPayloadStart + kPayloadStartDeadzone] = char(c.startDeadzone);
    report[kPayloadStart + kPayloadEndDeadzone] = char(c.endDeadzone);

    IUsbDeviceHidInterface *handle = device->openHidFor(owner, interface);
    if (!handle)
        return false;

    const QString name = QString(QChar(pedal.letter)) + QStringLiteral("_pf");
    const bool written = handle->write(report);
    if (!written) {
        qCWarning(hidcenterDevice).noquote()
            << "SIMSONN: writing" << name << "failed:" << handle->lastError();
    } else {
        qCInfo(hidcenterDevice).noquote()
            << "SIMSONN: wrote" << name << "with travel" << c.physicalMinimum << "to"
            << c.physicalMaximum << "and deadzones" << c.startDeadzone << "/"
            << c.endDeadzone;
    }

    device->releaseFor(owner, interface);
    return written;
}

} // namespace hidcenter::simsonn_pedals
