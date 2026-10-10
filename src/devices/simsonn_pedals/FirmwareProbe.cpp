// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareProbe.h"

#include <QTimer>

#include <iterator>

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"

namespace hidcenter::simsonn_pedals {
namespace {

/// Report ID selecting the "read the configuration blob" exchange.
constexpr char kReportConfigRead = 0x03;

/// Which packet of the blob is asked for. Packet 1 carries config bytes
/// `0x000`..`0x03D`, and both firmware fields are in the first four of those.
constexpr char kFirstPacket = 0x01;

/// Where the fields sit inside the packet's payload, which begins at byte 2 of
/// the report (after the report ID and the packet number) and is config offset
/// `0x000`.
constexpr int kPayloadStart = 2;
constexpr int kVersionOffset = kPayloadStart + 0x00;
constexpr int kVersionSubOffset = kPayloadStart + 0x02;

/// The device answers with a full 64-byte report. Anything shorter than the two
/// fields is a truncated read rather than a packet worth parsing.
constexpr int kMinimumReplyLength = kVersionSubOffset + 2;

/// An unanswered request is re-sent every 250 ms and the whole transfer gets
/// 5 s, a cadence the device is known to tolerate.
constexpr int kResendIntervalMs = 250;
constexpr int kDeadlineMs = 5000;

/// A little-endian uint16 at `offset`. The whole configuration blob is
/// little-endian.
quint16 readUint16(const QByteArray &data, int offset)
{
    return quint16(quint8(data.at(offset))) | quint16(quint8(data.at(offset + 1))) << 8;
}

} // namespace

FirmwareProbe::FirmwareProbe(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
}

void FirmwareProbe::start(const UsbInterfaceInfo &interface)
{
    if (m_state != State::Idle)
        return;

    // Not a failure, and deliberately not reported as one: the application
    // builds a module for every supported device whether or not one is plugged
    // in, and an offline preview has nothing to answer. IUsbDevice::isOffline()
    // asks a module not to attempt transfers at all.
    if (!m_device || m_device->isOffline()) {
        m_error = tr("Pedals not connected.");
        return;
    }

    if (!interface.isHid()) {
        m_error = tr("Cannot talk to the pedals: %1 has no hidraw node.")
                      .arg(interface.displayName());
        return;
    }

    m_interface = m_device->openHidFor(this, interface);
    if (!m_interface) {
        finish(State::Failed, m_device->lastError());
        return;
    }

    m_interfaceInfo = interface;
    connect(m_interface, &IUsbDeviceHidInterface::inputReport, this,
            &FirmwareProbe::onInputReport);

    // The handle belongs to the device and can be closed underneath this object
    // — the analyzer releasing a shared claim, or the device going away. Without
    // this the probe would sit Waiting on an interface that no longer exists
    // until the deadline, and report a timeout for what is really a disconnect.
    connect(m_interface, &QObject::destroyed, this, [this] {
        m_interface = nullptr;
        if (m_state == State::Waiting)
            finish(State::Failed, tr("The pedals disconnected before answering."));
    });

    m_state = State::Waiting;

    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, &FirmwareProbe::expire);
    m_deadline->start(kDeadlineMs);

    m_resend = new QTimer(this);
    connect(m_resend, &QTimer::timeout, this, &FirmwareProbe::resend);
    m_resend->start(kResendIntervalMs);

    Q_EMIT stateChanged();
    resend();
}

void FirmwareProbe::resend()
{
    if (m_state != State::Waiting || !m_interface)
        return;

    // Two bytes, exactly what the
    // firmware declares this report's OUT size to be.
    //
    // Spelled out through an array rather than a braced QByteArray, which does
    // not mean what it looks like: `QByteArray{0x03, 0x01}` resolves to
    // QByteArray(qsizetype size, char ch) and builds *three bytes of 0x01*. The
    // device ignores that, as it should.
    static constexpr char kRequest[] = {kReportConfigRead, kFirstPacket};

    if (!m_interface->write(QByteArray(kRequest, int(std::size(kRequest))))) {
        // One failed write is not the end of it — the resend timer will try
        // again — but a write that never succeeds would otherwise time out with
        // nothing to say about why.
        m_error = m_interface->lastError();
        qCDebug(hidcenterDevice).noquote()
            << "SIMSONN: config read request failed:" << m_error;
    }
}

void FirmwareProbe::onInputReport(const QByteArray &report)
{
    if (m_state != State::Waiting)
        return;

    // A packet is accepted only when both the report ID and the
    // packet number are the ones it asked for. Everything else on this interface
    // — an axis report, a params reply, a packet from some other exchange — is
    // not an answer to this request.
    if (report.size() < 2 || report.at(0) != kReportConfigRead || report.at(1) != kFirstPacket)
        return;

    if (report.size() < kMinimumReplyLength) {
        finish(State::Failed,
               tr("The pedals' reply was too short (%n byte(s)).",
                  nullptr, int(report.size())));
        return;
    }

    m_version = readUint16(report, kVersionOffset);
    m_versionSub = readUint16(report, kVersionSubOffset);

    qCInfo(hidcenterDevice).noquote() << "SIMSONN: firmware" << versionString() << "on"
                                  << (m_interfaceInfo ? m_interfaceInfo->displayName()
                                                      : QString());
    finish(State::Ready, QString());
}

void FirmwareProbe::expire()
{
    if (m_state != State::Waiting)
        return;

    // A write that never went out is the more useful thing to report, when that
    // is what happened: "did not answer" sends the user looking at the pedals
    // when the problem was at this end.
    finish(State::Failed,
           m_error.isEmpty()
               ? tr("The pedals did not answer in time.")
               : m_error);
}

void FirmwareProbe::finish(State state, const QString &error)
{
    m_state = state;
    m_error = error;

    if (m_resend)
        m_resend->stop();
    if (m_deadline)
        m_deadline->stop();

    // Nothing more will be asked of the interface, so the claim goes back. The
    // handle stays open only if something else still holds one.
    if (m_device)
        m_device->releaseAllFor(this);
    m_interface = nullptr;

    Q_EMIT stateChanged();
}

QString FirmwareProbe::versionString() const
{
    if (m_state != State::Ready)
        return {};

    // **Base 16, deliberately.** The version's nibbles spell it out: this unit
    // answers 0x2070 / 0x0204 and is firmware 2070.204, not 8304.516. The
    // No zero padding: a sub-version of 0x0205 is written `205`, not `0205`.
    return QStringLiteral("%1.%2")
        .arg(m_version, 0, 16)
        .arg(m_versionSub, 0, 16);
}

QString FirmwareProbe::rawVersion() const
{
    return QStringLiteral("0x%1 / 0x%2")
        .arg(m_version, 4, 16, QLatin1Char('0'))
        .arg(m_versionSub, 4, 16, QLatin1Char('0'));
}

} // namespace hidcenter::simsonn_pedals
