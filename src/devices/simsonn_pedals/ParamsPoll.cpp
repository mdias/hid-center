// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ParamsPoll.h"

#include "Pedals.h"

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"

#include <QTimer>

#include <cstring>

namespace hidcenter::simsonn_pedals {
namespace {

constexpr char kReportParams = 0x02;

/// The declared OUT size of report `0x02`: the id plus 63 payload bytes.
constexpr int kReportSize = 64;

/// The two reply packets, by their packet byte, and how much of the params area
/// each one carries. A `01` is refused unless a `00` has been seen before it.
constexpr quint8 kPacketFirst = 0x00;
constexpr quint8 kPacketSecond = 0x01;
constexpr int kFirstPayload = 62;
constexpr int kSecondPayload = 8;

/// Where the payload starts in a reply: past the report id and the packet byte.
constexpr int kPayloadStart = 2;

/// The live raw axis values, as an offset into the params area rather than into
/// the config blob. The area begins at blob offset `0x43C`.
constexpr int kParamsBase = 0x43C;
constexpr int kRawAxisOffset = 0x440 - kParamsBase;

/// The request as it goes on the wire.
///
/// Byte 1 onwards carries nothing: the device answers the same way whether it is
/// zero or `0xA0`.
///
/// The braced `QByteArray(size, char)` constructor is the right one *here* — a
/// run of 64 zero bytes — unlike in FirmwareProbe, where the same-looking
/// `QByteArray{0x03, 0x01}` silently meant three bytes of `0x01`.
QByteArray paramsRequest()
{
    QByteArray request(kReportSize, '\0');
    request[0] = kReportParams;
    return request;
}

/// A little-endian signed 16-bit field of the params area.
qint16 readInt16(const QByteArray &area, int offset)
{
    return qint16(quint16(quint8(area.at(offset)))
                  | quint16(quint8(area.at(offset + 1))) << 8);
}

} // namespace

static_assert(kFirstPayload + kSecondPayload == ParamsPoll::kParamsSize);

ParamsPoll::ParamsPoll(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_assembling(kParamsSize, '\0')
{
}

// --- Holding the interface --------------------------------------------------

void ParamsPoll::start(const UsbInterfaceInfo &interface)
{
    if (isRunning())
        return;

    if (!m_device || m_device->isOffline()) {
        m_error = tr("Not connected.");
        Q_EMIT stateChanged();
        return;
    }
    if (!interface.isHid()) {
        m_error = tr("Cannot talk to the pedals: %1 has no hidraw node.")
                      .arg(interface.displayName());
        Q_EMIT stateChanged();
        return;
    }

    IUsbDeviceHidInterface *handle = m_device->openHidFor(this, interface);
    if (!handle) {
        m_error = m_device->lastError();
        qCWarning(hidcenterDevice).noquote()
            << "SIMSONN: the params request could not open" << interface.displayName() << ":"
            << m_error;
        Q_EMIT stateChanged();
        return;
    }

    m_interface = handle;
    m_interfaceInfo = interface;
    m_error.clear();
    m_writeFailureLogged = false;
    m_sawFirstPacket = false;

    m_reportConnection =
        connect(handle, &IUsbDeviceHidInterface::inputReport, this, &ParamsPoll::onInputReport);
    // The handle dies with the interface, so this is how an unplug stops the
    // timer rather than leaving it writing into a dangling pointer.
    m_goneConnection = connect(handle, &QObject::destroyed, this, &ParamsPoll::onInterfaceGone);

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kIntervalMs);
        connect(m_timer, &QTimer::timeout, this, &ParamsPoll::send);
    }
    m_timer->start();

    if (!m_throttle) {
        m_throttle = new QTimer(this);
        m_throttle->setInterval(kUpdateIntervalMs);
        connect(m_throttle, &QTimer::timeout, this, [this] {
            if (!m_pending)
                return;
            m_pending = false;
            Q_EMIT paramsChanged();
        });
    }
    m_throttle->start();

    // The first one now rather than a second from now: waiting out the interval
    // before the first request would leave the device silent for no reason.
    send();
    Q_EMIT stateChanged();
}

void ParamsPoll::stop()
{
    if (m_timer)
        m_timer->stop();
    if (m_throttle)
        m_throttle->stop();

    disconnect(m_reportConnection);
    m_reportConnection = {};
    disconnect(m_goneConnection);
    m_goneConnection = {};
    m_interface = nullptr;

    if (m_device && m_interfaceInfo)
        m_device->releaseFor(this, *m_interfaceInfo);
    m_interfaceInfo.reset();

    forgetParams();
    Q_EMIT stateChanged();
}

void ParamsPoll::onInterfaceGone()
{
    stop();
    m_error = tr("The pedals were unplugged.");
    Q_EMIT stateChanged();
}

// --- Asking -----------------------------------------------------------------

void ParamsPoll::send()
{
    if (!m_interface)
        return;

    if (m_interface->write(paramsRequest())) {
        m_writeFailureLogged = false;
        return;
    }

    m_error = m_interface->lastError();
    if (!m_writeFailureLogged) {
        m_writeFailureLogged = true;
        qCWarning(hidcenterDevice).noquote()
            << "SIMSONN: the params request could not be written:" << m_error;
        Q_EMIT stateChanged();
    }
}

// --- Reading ----------------------------------------------------------------

void ParamsPoll::onInputReport(const QByteArray &report)
{
    if (report.size() < kPayloadStart || quint8(report.at(0)) != quint8(kReportParams))
        return;

    switch (quint8(report.at(1))) {
    case kPacketFirst:
        if (report.size() < kPayloadStart + kFirstPayload)
            return;
        std::memcpy(m_assembling.data(), report.constData() + kPayloadStart, kFirstPayload);
        m_sawFirstPacket = true;
        return;

    case kPacketSecond:
        // Half an area is worse than none: it would pair this reply's tail with
        // whatever the previous one left behind.
        if (!m_sawFirstPacket || report.size() < kPayloadStart + kSecondPayload)
            return;
        std::memcpy(m_assembling.data() + kFirstPayload, report.constData() + kPayloadStart,
                    kSecondPayload);
        m_sawFirstPacket = false;
        m_params = m_assembling;
        m_haveParams = true;
        m_pending = true;
        return;

    default:
        // Report `0x02` also carries the replies to some named commands, which
        // nothing here asked for.
        return;
    }
}

void ParamsPoll::forgetParams()
{
    m_sawFirstPacket = false;
    m_pending = false;
    if (!m_haveParams)
        return;

    m_haveParams = false;
    m_params.clear();
    Q_EMIT paramsChanged();
}

std::optional<qint16> ParamsPoll::rawAxis(int axis) const
{
    if (!m_haveParams || axis < 0 || axis >= kPedalCount)
        return {};
    return readInt16(m_params, kRawAxisOffset + axis * 2);
}

QByteArray ParamsPoll::params() const
{
    return m_haveParams ? m_params : QByteArray();
}

} // namespace hidcenter::simsonn_pedals
