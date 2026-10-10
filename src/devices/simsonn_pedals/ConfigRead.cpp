// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ConfigRead.h"

#include <QTimer>

#include <cstring>

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"

namespace hidcenter::simsonn_pedals {
namespace {

/// Report ID selecting the "read the configuration blob" exchange.
constexpr char kReportConfigRead = 0x03;

/// A reply is the report id, the packet number, then payload.
constexpr int kHeaderSize = 2;
constexpr int kPayloadSize = 62;

/// Where one axis lives in the blob, and where its fields live in the axis.
constexpr int kAxesOffset = 0x040;
constexpr int kAxisStride = 52;
constexpr int kEffectiveMinimum = 0x00;
constexpr int kEffectiveCentre = 0x02;
constexpr int kEffectiveMaximum = 0x04;
constexpr int kPhysicalMinimum = 0x06;
constexpr int kPhysicalMaximum = 0x08;
constexpr int kFlags = 0x0A;
constexpr int kCurve = 0x0B;
constexpr int kStartDeadzone = 0x22;
constexpr int kEndDeadzone = 0x23;

/// `Filter`, in the top three bits of the axis flag byte.
constexpr int kFilterShift = 5;
constexpr quint8 kFilterMask = 0x7 << kFilterShift;

/// An unanswered request is re-sent every 250 ms. The
/// deadline covers the whole eighteen-packet run rather than one packet, and is
/// generous: the run takes about a third of a second when it is working at all.
constexpr int kResendIntervalMs = 250;
constexpr int kDeadlineMs = 8000;

/// A little-endian signed 16-bit field of the blob.
qint16 readInt16(const QByteArray &blob, int offset)
{
    return qint16(quint16(quint8(blob.at(offset)))
                  | quint16(quint8(blob.at(offset + 1))) << 8);
}

void writeInt16(QByteArray &blob, int offset, qint16 value)
{
    blob[offset] = char(quint8(quint16(value) & 0xFF));
    blob[offset + 1] = char(quint8(quint16(value) >> 8));
}

} // namespace

ConfigRead::ConfigRead(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_blob(kBlobSize, '\0')
{
}

int ConfigRead::packetCount() const
{
    // Rounded up: the last packet carries whatever is left, which is less than a
    // full payload.
    return (kBlobSize + kPayloadSize - 1) / kPayloadSize;
}

// --- The exchange -----------------------------------------------------------

void ConfigRead::start(const UsbInterfaceInfo &interface)
{
    if (m_state != State::Idle)
        return;

    // Not a failure, and deliberately not reported as one: the application
    // builds a module for every supported device whether or not one is plugged
    // in, and an offline preview has nothing to answer.
    if (!m_device || m_device->isOffline()) {
        m_error = tr("Pedals not connected.");
        Q_EMIT stateChanged();
        return;
    }

    if (!interface.isHid()) {
        m_error = tr("Cannot talk to the pedals: %1 has no hidraw node.")
                      .arg(interface.displayName());
        Q_EMIT stateChanged();
        return;
    }

    m_interface = m_device->openHidFor(this, interface);
    if (!m_interface) {
        finish(State::Failed, m_device->lastError());
        return;
    }

    m_interfaceInfo = interface;
    connect(m_interface, &IUsbDeviceHidInterface::inputReport, this,
            &ConfigRead::onInputReport);

    // The handle belongs to the device and can be closed underneath this object.
    // Without this the read would sit waiting on an interface that no longer
    // exists until the deadline, and report a timeout for what is a disconnect.
    connect(m_interface, &QObject::destroyed, this, [this] {
        m_interface = nullptr;
        if (m_state == State::Reading)
            finish(State::Failed, tr("The pedals disconnected before answering."));
    });

    m_state = State::Reading;
    m_nextPacket = 1;

    // Built once and restarted afterwards, so a refresh() does not leave a pair
    // of timers behind on every call.
    if (!m_deadline) {
        m_deadline = new QTimer(this);
        m_deadline->setSingleShot(true);
        connect(m_deadline, &QTimer::timeout, this, &ConfigRead::expire);
    }
    m_deadline->start(kDeadlineMs);

    if (!m_resend) {
        m_resend = new QTimer(this);
        connect(m_resend, &QTimer::timeout, this, &ConfigRead::resend);
    }
    m_resend->start(kResendIntervalMs);

    Q_EMIT stateChanged();
    request(m_nextPacket);
}

void ConfigRead::refresh()
{
    // A read already under way will answer the question being asked, and one
    // that was never started has no interface to start on.
    if (m_state == State::Reading || !m_interfaceInfo)
        return;

    const UsbInterfaceInfo interface = *m_interfaceInfo;
    m_state = State::Idle;
    m_error.clear();
    start(interface);
}

void ConfigRead::request(int packet)
{
    if (!m_interface)
        return;

    // Two bytes, exactly what the
    // firmware declares this report's OUT size to be.
    //
    // Spelled out through an array rather than a braced QByteArray, which does
    // not mean what it looks like: `QByteArray{0x03, 0x01}` resolves to
    // QByteArray(qsizetype size, char ch) and builds *three bytes of 0x01*.
    const char request[] = {kReportConfigRead, char(packet)};

    if (!m_interface->write(QByteArray(request, int(std::size(request))))) {
        // One failed write is not the end of it — the resend timer will try
        // again — but a write that never succeeds would otherwise time out with
        // nothing to say about why.
        m_error = m_interface->lastError();
        qCDebug(hidcenterDevice).noquote()
            << "SIMSONN: config read request failed:" << m_error;
    }
}

void ConfigRead::resend()
{
    if (m_state != State::Reading)
        return;
    request(m_nextPacket);
}

void ConfigRead::onInputReport(const QByteArray &report)
{
    if (m_state != State::Reading)
        return;

    // A packet is accepted only when both the report ID and the
    // packet number are the ones it asked for. Everything else on this interface
    // — a params reply, an answer to a request already satisfied — is not an
    // answer to this one.
    if (report.size() < kHeaderSize || report.at(0) != kReportConfigRead
        || quint8(report.at(1)) != quint8(m_nextPacket))
        return;

    const int offset = (m_nextPacket - 1) * kPayloadSize;
    const int wanted = qMin(kPayloadSize, kBlobSize - offset);
    if (report.size() < kHeaderSize + wanted) {
        finish(State::Failed,
               tr("Packet %1 from the pedals was too short (%n byte(s)).",
                  nullptr, int(report.size()))
                   .arg(m_nextPacket));
        return;
    }

    std::memcpy(m_blob.data() + offset, report.constData() + kHeaderSize, size_t(wanted));

    if (m_nextPacket >= packetCount()) {
        qCInfo(hidcenterDevice).noquote()
            << "SIMSONN: read the" << kBlobSize << "byte configuration blob from"
            << (m_interfaceInfo ? m_interfaceInfo->displayName() : QString());
        finish(State::Ready, QString());
        return;
    }

    ++m_nextPacket;
    // Straight on to the next one rather than waiting out the resend interval:
    // the exchange is host-paced, so the device is doing nothing until asked.
    // The resend timer is only there for a packet that goes unanswered.
    if (m_resend)
        m_resend->start(kResendIntervalMs);
    Q_EMIT stateChanged();
    request(m_nextPacket);
}

void ConfigRead::expire()
{
    if (m_state != State::Reading)
        return;

    // A write that never went out is the more useful thing to report, when that
    // is what happened: "did not answer" sends the user looking at the pedals
    // when the problem was at this end.
    finish(State::Failed,
           m_error.isEmpty()
               ? tr("The pedals sent only %1 of %2 packets in time.")
                     .arg(packetsRead())
                     .arg(packetCount())
               : m_error);
}

void ConfigRead::finish(State state, const QString &error)
{
    m_state = state;
    m_error = error;

    if (m_resend)
        m_resend->stop();
    if (m_deadline)
        m_deadline->stop();

    // Nothing more will be asked of the interface, so the claim goes back. The
    // handle stays open only if something else still holds one — which, on the
    // Calibration page, the params poll does.
    if (m_device)
        m_device->releaseAllFor(this);
    m_interface = nullptr;

    Q_EMIT stateChanged();
}

// --- What is in it ----------------------------------------------------------

std::optional<ConfigRead::AxisCalibration> ConfigRead::axis(int index) const
{
    if (m_state != State::Ready || index < 0 || index >= kAxisCount)
        return {};

    const int base = kAxesOffset + index * kAxisStride;
    AxisCalibration calibration;
    calibration.effectiveMinimum = readInt16(m_blob, base + kEffectiveMinimum);
    calibration.effectiveCentre = readInt16(m_blob, base + kEffectiveCentre);
    calibration.effectiveMaximum = readInt16(m_blob, base + kEffectiveMaximum);
    calibration.physicalMinimum = readInt16(m_blob, base + kPhysicalMinimum);
    calibration.physicalMaximum = readInt16(m_blob, base + kPhysicalMaximum);
    calibration.startDeadzone = quint8(m_blob.at(base + kStartDeadzone));
    calibration.endDeadzone = quint8(m_blob.at(base + kEndDeadzone));
    return calibration;
}

std::optional<int> ConfigRead::filter(int index) const
{
    if (m_state != State::Ready || index < 0 || index >= kAxisCount)
        return {};

    const quint8 flags = quint8(m_blob.at(kAxesOffset + index * kAxisStride + kFlags));
    return (flags & kFilterMask) >> kFilterShift;
}

void ConfigRead::patchAxis(QByteArray &blob, int index,
                           const std::array<qint8, kCurvePoints> &curve,
                           const AxisCalibration &calibration, int filter)
{
    if (blob.size() != kBlobSize || index < 0 || index >= kAxisCount)
        return;

    const int base = kAxesOffset + index * kAxisStride;
    writeInt16(blob, base + kEffectiveMinimum, calibration.effectiveMinimum);
    writeInt16(blob, base + kEffectiveCentre, calibration.effectiveCentre);
    writeInt16(blob, base + kEffectiveMaximum, calibration.effectiveMaximum);
    writeInt16(blob, base + kPhysicalMinimum, calibration.physicalMinimum);
    writeInt16(blob, base + kPhysicalMaximum, calibration.physicalMaximum);
    blob[base + kStartDeadzone] = char(calibration.startDeadzone);
    blob[base + kEndDeadzone] = char(calibration.endDeadzone);
    for (int i = 0; i < kCurvePoints; ++i)
        blob[base + kCurve + i] = char(curve[size_t(i)]);

    // The other five bits of the flag byte are left exactly as they were read:
    // whether the axis is reported at all, inverted, centred and what function
    // it has are nothing this application edits.
    const quint8 flags = quint8(blob.at(base + kFlags));
    blob[base + kFlags] =
        char((flags & ~kFilterMask) | ((quint8(filter) << kFilterShift) & kFilterMask));
}

std::optional<std::array<qint8, ConfigRead::kCurvePoints>> ConfigRead::curve(int index) const
{
    if (m_state != State::Ready || index < 0 || index >= kAxisCount)
        return {};

    const int base = kAxesOffset + index * kAxisStride + kCurve;
    std::array<qint8, kCurvePoints> points {};
    for (int i = 0; i < kCurvePoints; ++i)
        points[size_t(i)] = qint8(m_blob.at(base + i));
    return points;
}

} // namespace hidcenter::simsonn_pedals
