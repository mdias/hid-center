// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ConfigWrite.h"

#include "ConfigRead.h"

#include <QTimer>

#include <cstring>

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"

namespace hidcenter::simsonn_pedals {
namespace {

/// Report ID selecting the "write the configuration blob" exchange.
constexpr char kReportConfigWrite = 0x04;

/// Every packet is written as a whole 64-byte report: the id, the packet number,
/// then 62 bytes of blob — the same slicing as the read.
constexpr int kReportSize = 64;
constexpr int kHeaderSize = 2;
constexpr int kPayloadSize = 62;
constexpr int kPacketCount = (ConfigRead::kBlobSize + kPayloadSize - 1) / kPayloadSize;

/// What the device says, after the last packet, when it will not take the blob.
constexpr quint8 kRefused = 0xFE;

/// An unanswered packet goes again after 250 ms, and
/// the whole exchange gets 5 s. The write then listens for a refusal for what is left
/// of 2 s from the start, which in practice is a few hundred milliseconds.
constexpr int kResendIntervalMs = 250;
constexpr int kDeadlineMs = 5000;
constexpr int kSettleMs = 500;

} // namespace

ConfigWrite::ConfigWrite(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_resend = new QTimer(this);
    connect(m_resend, &QTimer::timeout, this, &ConfigWrite::resend);

    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, &ConfigWrite::expire);

    m_settle = new QTimer(this);
    m_settle->setSingleShot(true);
    connect(m_settle, &QTimer::timeout, this, [this] {
        if (m_state == State::Settling)
            finish(State::Done, QString());
    });
}

void ConfigWrite::start(const UsbInterfaceInfo &interface, const QByteArray &blob)
{
    if (isRunning())
        return;

    if (blob.size() != ConfigRead::kBlobSize) {
        finish(State::Failed, tr("The settings data has the wrong size: %1 bytes, expected %2.")
                                  .arg(blob.size())
                                  .arg(ConfigRead::kBlobSize));
        return;
    }
    if (!m_device) {
        finish(State::Failed, tr("Pedals not connected."));
        return;
    }

    m_interface = m_device->openHidFor(this, interface);
    if (!m_interface) {
        finish(State::Failed, m_device->lastError());
        return;
    }

    m_interfaceInfo = interface;
    m_blob = blob;
    m_error.clear();
    m_state = State::Writing;

    connect(m_interface, &IUsbDeviceHidInterface::inputReport, this,
            &ConfigWrite::onInputReport, Qt::UniqueConnection);
    connect(m_interface, &QObject::destroyed, this, [this] {
        m_interface = nullptr;
        if (isRunning())
            finish(State::Failed, tr("The pedals disconnected while saving."));
    });

    m_deadline->start(kDeadlineMs);
    m_resend->start(kResendIntervalMs);
    send(0);
}

void ConfigWrite::send(int packet)
{
    if (!m_interface)
        return;

    m_sent = packet;

    QByteArray report(kReportSize, '\0');
    report[0] = kReportConfigWrite;
    report[1] = char(quint8(packet));
    if (packet > 0) {
        const int offset = (packet - 1) * kPayloadSize;
        const int size = qMin(kPayloadSize, ConfigRead::kBlobSize - offset);
        std::memcpy(report.data() + kHeaderSize, m_blob.constData() + offset, size_t(size));
    }

    if (!m_interface->write(report)) {
        finish(State::Failed, tr("Could not send packet %1: %2")
                                  .arg(packet)
                                  .arg(m_interface->lastError()));
    }
}

void ConfigWrite::onInputReport(const QByteArray &report)
{
    if (!isRunning() || report.size() < kHeaderSize || report.at(0) != kReportConfigWrite)
        return;

    const quint8 asked = quint8(report.at(1));

    if (asked == kRefused) {
        finish(State::Failed, tr("The pedals rejected the settings, usually because of a firmware "
                                 "mismatch."));
        return;
    }

    // Only the next packet moves things on. Anything else is an echo of a
    // request already answered, which a resend can produce.
    if (m_state != State::Writing || asked != m_sent + 1 || asked > kPacketCount)
        return;

    m_resend->start(kResendIntervalMs);
    send(asked);
    if (m_state != State::Writing)
        return;

    if (asked == kPacketCount) {
        m_resend->stop();
        m_deadline->stop();
        m_state = State::Settling;
        m_settle->start(kSettleMs);
    }
}

void ConfigWrite::resend()
{
    if (m_state == State::Writing)
        send(m_sent);
}

void ConfigWrite::expire()
{
    if (m_state != State::Writing)
        return;
    finish(State::Failed, tr("The pedals requested only %1 of %2 packets in time.")
                              .arg(m_sent)
                              .arg(kPacketCount));
}

void ConfigWrite::finish(State state, const QString &error)
{
    m_state = state;
    m_error = error;
    m_resend->stop();
    m_deadline->stop();
    m_settle->stop();

    if (state == State::Done) {
        qCInfo(hidcenterDevice).noquote()
            << "SIMSONN: wrote the" << ConfigRead::kBlobSize << "byte configuration blob";
    } else {
        qCWarning(hidcenterDevice).noquote()
            << "SIMSONN: writing the configuration blob failed:" << error;
    }

    if (m_device)
        m_device->releaseAllFor(this);
    if (m_interface)
        disconnect(m_interface, nullptr, this, nullptr);
    m_interface = nullptr;

    Q_EMIT finished();
}

} // namespace hidcenter::simsonn_pedals
