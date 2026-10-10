// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoButtonInput.h"

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"
#include "core/usb/ReportDescriptor.h"

#include <algorithm>
#include <utility>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// HID's Button usage page, where usage N is button N.
constexpr quint32 kButtonUsagePage = 0x09;

} // namespace

// --- The decode -------------------------------------------------------------

QList<ButtonBit> buttonBitsOf(const ReportDescriptor &descriptor, quint8 reportId)
{
    QList<ButtonBit> bits;

    // Without ids there is one unnumbered input report, and nothing says it is
    // the one wanted; see AlphaEvoForceTrace::forceInterface().
    if (!descriptor.usesReportIds())
        return bits;
    const ReportInfo *report = descriptor.find(reportId, ReportType::Input);
    if (!report)
        return bits;

    for (const ReportField &field : report->fields) {
        if (field.isConstant() || field.isArray() || field.bitSize != 1 || field.usages.isEmpty())
            continue;

        for (quint32 element = 0; element < field.count; ++element) {
            // With fewer usages than elements, the last goes on for the rest,
            // as HID has it.
            const quint32 usage =
                field.usages.at(qMin<qsizetype>(qsizetype(element), field.usages.size() - 1));
            const int button = int(usage & 0xFFFF);
            if ((usage >> 16) != kButtonUsagePage || button == 0)
                continue;

            // The field's offset is into the payload; the report as received
            // starts with its id.
            bits.append({button, int(8 + field.bitOffset + element)});
        }
    }
    return bits;
}

AxisField axisFieldOf(const ReportDescriptor &descriptor, quint8 reportId, quint32 usage)
{
    if (!descriptor.usesReportIds())
        return {};
    const ReportInfo *report = descriptor.find(reportId, ReportType::Input);
    if (!report)
        return {};

    for (const ReportField &field : report->fields) {
        if (field.isConstant() || field.isArray() || field.usages.isEmpty())
            continue;
        for (quint32 element = 0; element < field.count; ++element) {
            const quint32 elementUsage =
                field.usages.at(qMin<qsizetype>(qsizetype(element), field.usages.size() - 1));
            if (elementUsage != usage)
                continue;
            AxisField axis;
            axis.bit = int(8 + field.bitOffset + element * field.bitSize);
            axis.bitSize = int(field.bitSize);
            axis.logicalMin = field.logicalMin;
            axis.logicalMax = field.logicalMax;
            return axis.isValid() ? axis : AxisField{};
        }
    }
    return {};
}

qint32 axisValue(const QByteArray &report, const AxisField &axis)
{
    if (!axis.isValid() || axis.bit + axis.bitSize > report.size() * 8)
        return axis.logicalMin;

    quint64 raw = 0;
    for (int i = 0; i < axis.bitSize; ++i) {
        const int bit = axis.bit + i;
        if ((quint8(report.at(bit / 8)) >> (bit % 8)) & 1)
            raw |= quint64(1) << i;
    }
    // The ranges seen are all non-negative, 0 to 4095, so the value is taken
    // as unsigned.
    return qBound(axis.logicalMin, qint32(raw), axis.logicalMax);
}

QList<int> pressedButtons(const QByteArray &report, const QList<ButtonBit> &bits)
{
    QList<int> pressed;
    for (const ButtonBit &bit : bits) {
        const qsizetype byte = bit.bit / 8;
        if (byte < report.size() && (quint8(report.at(byte)) >> (bit.bit % 8)) & 1)
            pressed.append(bit.button);
    }
    std::sort(pressed.begin(), pressed.end());
    pressed.erase(std::unique(pressed.begin(), pressed.end()), pressed.end());
    return pressed;
}

// --- Construction -----------------------------------------------------------

AlphaEvoButtonInput::AlphaEvoButtonInput(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_usbDevice(device)
{
    if (!m_usbDevice || m_usbDevice->isOffline()) {
        m_state = ForceTraceSource::Offline;
        return;
    }

    // Counted from what enumeration read, so the page can lay out the buttons
    // before anything is opened.
    if (const UsbInterfaceInfo *iface = buttonInterface()) {
        m_state = ForceTraceSource::Available;
        m_bits = buttonBitsOf(iface->hid.descriptor, kButtonReportId);
        for (const ButtonBit &bit : std::as_const(m_bits))
            m_buttonCount = qMax(m_buttonCount, bit.button);
        findClutchAxes(iface->hid.descriptor);
    }
}

void AlphaEvoButtonInput::findClutchAxes(const ReportDescriptor &descriptor)
{
    const AxisField left = axisFieldOf(descriptor, kButtonReportId, kLeftClutchUsage);
    const AxisField right = axisFieldOf(descriptor, kButtonReportId, kRightClutchUsage);
    if (left.isValid() && right.isValid()) {
        m_leftClutch = left;
        m_rightClutch = right;
    }
}

const UsbInterfaceInfo *AlphaEvoButtonInput::buttonInterface() const
{
    if (!m_usbDevice)
        return nullptr;

    for (const UsbInterfaceInfo &iface : m_usbDevice->info().interfaces) {
        if (iface.isHid() && !buttonBitsOf(iface.hid.descriptor, kButtonReportId).isEmpty())
            return &iface;
    }
    return nullptr;
}

// --- Holding the interface --------------------------------------------------

void AlphaEvoButtonInput::setEnabled(bool enabled)
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

bool AlphaEvoButtonInput::attach()
{
    const UsbInterfaceInfo *iface = buttonInterface();
    if (!m_usbDevice || !iface)
        return false;

    IUsbDeviceHidInterface *device = m_usbDevice->openHidFor(this, *iface);
    if (!device) {
        qCWarning(hidcenterDevice).noquote() << "The button preview could not open"
                                          << iface->displayName() << ":"
                                          << m_usbDevice->lastError();
        setState(ForceTraceSource::OpenFailed);
        return false;
    }

    m_device = device;
    // The handle's own descriptor, read from the open node, is the one the
    // reports follow. It is the same one enumeration read, barring a firmware
    // update in between, which would have re-enumerated the device anyway.
    const QList<ButtonBit> bits = buttonBitsOf(device->descriptor(), kButtonReportId);
    if (!bits.isEmpty())
        m_bits = bits;
    findClutchAxes(device->descriptor());

    m_reportConnection = connect(device, &IUsbDeviceHidInterface::inputReport, this,
                                 &AlphaEvoButtonInput::onInputReport);
    m_deviceGoneConnection = connect(device, &IUsbDeviceHidInterface::disconnected, this,
                                     &AlphaEvoButtonInput::onDeviceLost);

    setState(ForceTraceSource::Available);
    return true;
}

void AlphaEvoButtonInput::detach()
{
    disconnect(m_reportConnection);
    m_reportConnection = {};
    disconnect(m_deviceGoneConnection);
    m_deviceGoneConnection = {};
    m_device = nullptr;

    // Letting go is what stops the reading, as for the force trace.
    if (m_usbDevice) {
        if (const UsbInterfaceInfo *iface = buttonInterface())
            m_usbDevice->releaseFor(this, *iface);
    }

    // Nothing read is nothing pressed, rather than whatever was held when the
    // reading stopped.
    setPressed({});
    setClutch(m_leftClutch.logicalMin, m_rightClutch.logicalMin);
}

void AlphaEvoButtonInput::onDeviceLost()
{
    m_enabled = false;
    detach();
    setState(ForceTraceSource::DeviceGone);
}

// --- Reading ----------------------------------------------------------------

void AlphaEvoButtonInput::onInputReport(const QByteArray &report)
{
    if (report.isEmpty() || quint8(report.at(0)) != kButtonReportId)
        return;
    setPressed(pressedButtons(report, m_bits));
    if (hasClutchAxes())
        setClutch(axisValue(report, m_leftClutch), axisValue(report, m_rightClutch));
}

void AlphaEvoButtonInput::setClutch(qint32 left, qint32 right)
{
    if (left == m_leftClutchValue && right == m_rightClutchValue)
        return;

    m_leftClutchValue = left;
    m_rightClutchValue = right;
    Q_EMIT clutchChanged();
}

namespace {

qreal normalised(const AxisField &axis, qint32 value)
{
    if (!axis.isValid())
        return 0.0;
    return qreal(value - axis.logicalMin) / qreal(axis.logicalMax - axis.logicalMin);
}

} // namespace

qreal AlphaEvoButtonInput::leftClutch() const
{
    return normalised(m_leftClutch, m_leftClutchValue);
}

qreal AlphaEvoButtonInput::rightClutch() const
{
    return normalised(m_rightClutch, m_rightClutchValue);
}

void AlphaEvoButtonInput::setPressed(const QList<int> &pressed)
{
    if (pressed == m_pressed)
        return;

    m_pressed = pressed;
    Q_EMIT pressedChanged(m_pressed);
}

void AlphaEvoButtonInput::setState(ForceTraceSource state)
{
    if (m_state == state)
        return;

    m_state = state;
    Q_EMIT sourceStateChanged(m_state);
}

// --- What to tell the user --------------------------------------------------

QString AlphaEvoButtonInput::sourceDescription() const
{
    switch (m_state) {
    case ForceTraceSource::Available:
        return {};
    case ForceTraceSource::NoSuchReport:
        return tr("This wheelbase reports no buttons.");
    case ForceTraceSource::Offline:
        return tr("Not connected: buttons are unavailable.");
    case ForceTraceSource::DeviceGone:
        return tr("Wheelbase unplugged: buttons are unavailable.");
    case ForceTraceSource::OpenFailed:
        return tr("Could not open the wheelbase to read buttons. %1")
            .arg(m_usbDevice ? m_usbDevice->lastError() : QString());
    }
    return {};
}

} // namespace hidcenter::simagic_alpha_evo
