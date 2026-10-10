// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalInput.h"

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"
#include "core/usb/ReportDescriptor.h"

#include <QTimer>

namespace hidcenter::simsonn_pedals {
namespace {

/// Bits per axis, and how many the report must carry before it is worth reading.
constexpr quint32 kAxisBits = 16;

/// The axis field of report 0x01, or nullptr when `descriptor` declares none.
///
/// Matched on shape rather than on usage: the Plus X declares all three axes
/// under a single usage (0001:0030, X), so the usages cannot tell them apart and
/// there is nothing to gain by looking at them.
const ReportField *axisField(const ReportDescriptor &descriptor)
{
    if (!descriptor.usesReportIds())
        return nullptr;
    const ReportInfo *report = descriptor.find(kAxisReportId, ReportType::Input);
    if (!report)
        return nullptr;

    for (const ReportField &field : report->fields) {
        if (field.isConstant() || field.bitSize != kAxisBits
            || field.count < quint32(PedalInput::kAxisCount))
            continue;
        return &field;
    }
    return nullptr;
}

/// One little-endian signed 16-bit axis out of the report as received, so past
/// the report id byte.
qint16 readAxis(const QByteArray &report, int axis)
{
    const int offset = 1 + axis * 2;
    return qint16(quint16(quint8(report.at(offset)))
                  | quint16(quint8(report.at(offset + 1))) << 8);
}

} // namespace

PedalInput::PedalInput(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_usbDevice(device)
{
    // The range comes from what enumeration read, so a position can be turned
    // into a percentage from the first report onwards rather than from the
    // second.
    if (const UsbInterfaceInfo *iface = axisInterface())
        readRange(iface->hid.descriptor);
}

bool PedalInput::isAvailable() const
{
    return m_usbDevice && !m_usbDevice->isOffline() && axisInterface() != nullptr;
}

const UsbInterfaceInfo *PedalInput::axisInterface() const
{
    if (!m_usbDevice)
        return nullptr;

    for (const UsbInterfaceInfo &iface : m_usbDevice->info().interfaces) {
        if (iface.isHid() && axisField(iface.hid.descriptor))
            return &iface;
    }
    return nullptr;
}

void PedalInput::readRange(const ReportDescriptor &descriptor)
{
    const ReportField *field = axisField(descriptor);
    // A descriptor that declares an empty range would divide by zero below, and
    // says nothing useful anyway, so the built-in range stands.
    if (!field || field->logicalMax <= field->logicalMin)
        return;
    m_logicalMin = double(field->logicalMin);
    m_logicalMax = double(field->logicalMax);
}

// --- Holding the interface --------------------------------------------------

void PedalInput::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    if (m_enabled) {
        if (!attach())
            m_enabled = false;
    } else {
        detach();
    }
}

bool PedalInput::attach()
{
    const UsbInterfaceInfo *iface = axisInterface();
    if (!m_usbDevice || m_usbDevice->isOffline() || !iface)
        return false;

    IUsbDeviceHidInterface *handle = m_usbDevice->openHidFor(this, *iface);
    if (!handle) {
        qCWarning(hidcenterDevice).noquote()
            << "SIMSONN: the pedal positions could not be read from"
            << iface->displayName() << ":" << m_usbDevice->lastError();
        m_openFailed = true;
        return false;
    }

    m_openFailed = false;
    m_interfaceLost = false;
    m_interface = handle;
    // The open node's own descriptor is the one the reports actually follow.
    readRange(handle->descriptor());

    m_reportConnection =
        connect(handle, &IUsbDeviceHidInterface::inputReport, this, &PedalInput::onInputReport);
    // A handle is destroyed when its interface closes, so this is how an unplug
    // reaches here — without it the last positions would sit on screen for ever.
    m_goneConnection =
        connect(handle, &QObject::destroyed, this, &PedalInput::onInterfaceGone);

    if (!m_throttle) {
        m_throttle = new QTimer(this);
        m_throttle->setInterval(kUpdateIntervalMs);
        connect(m_throttle, &QTimer::timeout, this, [this] {
            if (!m_pending)
                return;
            m_pending = false;
            Q_EMIT positionsChanged();
        });
    }
    m_throttle->start();
    return true;
}

void PedalInput::detach()
{
    disconnect(m_reportConnection);
    m_reportConnection = {};
    disconnect(m_goneConnection);
    m_goneConnection = {};
    m_interface = nullptr;

    if (m_throttle)
        m_throttle->stop();

    // Letting go of the claim is what stops the reading.
    if (m_usbDevice) {
        if (const UsbInterfaceInfo *iface = axisInterface())
            m_usbDevice->releaseFor(this, *iface);
    }

    m_pending = false;
    m_positions = {};
    Q_EMIT positionsChanged();
}

void PedalInput::onInterfaceGone()
{
    m_enabled = false;
    detach();
    m_interfaceLost = true;
    Q_EMIT positionsChanged();
}

// --- Reading ----------------------------------------------------------------

void PedalInput::onInputReport(const QByteArray &report)
{
    if (report.size() < 1 + kAxisCount * 2 || quint8(report.at(0)) != kAxisReportId)
        return;

    const double span = m_logicalMax - m_logicalMin;
    for (int axis = 0; axis < kAxisCount; ++axis) {
        // A released pedal sits at the logical minimum, so the percentage is the
        // distance from there: 0 % released, 100 % at the end of the travel.
        const double position =
            qBound(0.0, (double(readAxis(report, axis)) - m_logicalMin) / span * 100.0, 100.0);
        if (m_positions[axis] && qFuzzyCompare(*m_positions[axis], position))
            continue;
        m_positions[axis] = position;
        m_pending = true;
    }
}

std::optional<double> PedalInput::position(int axis) const
{
    if (axis < 0 || axis >= kAxisCount)
        return {};
    return m_positions[axis];
}

// --- What to tell the user --------------------------------------------------

QString PedalInput::unavailableReason() const
{
    if (m_usbDevice && m_usbDevice->isOffline())
        return tr("Not connected: pedal positions are unavailable.");
    if (m_interfaceLost)
        return tr("Pedals unplugged: pedal positions are unavailable.");
    if (m_openFailed)
        return tr("Could not open the pedals to read their positions. %1")
            .arg(m_usbDevice ? m_usbDevice->lastError() : QString());
    if (!isAvailable())
        return tr("These pedals do not report positions.");
    return {};
}

} // namespace hidcenter::simsonn_pedals
