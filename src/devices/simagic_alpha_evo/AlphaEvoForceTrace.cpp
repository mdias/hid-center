// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoForceTrace.h"

#include "core/Logging.h"
#include "core/usb/IUsbDeviceHidInterface.h"
#include "core/usb/IUsbDevice.h"

namespace hidcenter::simagic_alpha_evo {

// --- The decode -------------------------------------------------------------

bool decodeForceSample(const QByteArray &report, bool usesReportIds, qreal *out)
{
    // The field sits at a fixed offset into the payload; the ID byte, when the
    // descriptor declares IDs, is in front of it.
    const int offset = kForceFieldOffset + (usesReportIds ? 1 : 0);
    if (report.size() < offset + 2)
        return false;

    if (usesReportIds && quint8(report.at(0)) != kForceReportId)
        return false;

    const auto low = quint16(quint8(report.at(offset)));
    const auto high = quint16(quint8(report.at(offset + 1)));
    const auto raw = qint16(quint16(low | quint16(high << 8)));

    if (out)
        *out = qreal(raw) / qreal(kForceFullScale);
    return true;
}

// --- Construction -----------------------------------------------------------

AlphaEvoForceTrace::AlphaEvoForceTrace(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_usbDevice(device)
{
    // Worked out once, from enumeration data. Whether the interface is *open* is
    // no longer part of the question — this class opens it when it needs it — so
    // there is nothing here that can change without the device going away.
    if (!m_usbDevice || m_usbDevice->isOffline())
        m_state = ForceTraceSource::Offline;
    else if (forceInterface())
        m_state = ForceTraceSource::Available;
    else
        m_state = ForceTraceSource::NoSuchReport;
}

const UsbInterfaceInfo *AlphaEvoForceTrace::forceInterface() const
{
    if (!m_usbDevice)
        return nullptr;

    for (const UsbInterfaceInfo &iface : m_usbDevice->info().interfaces) {
        // Only a HID interface has a descriptor to declare the report in. The
        // device's other interfaces are listed too now, and there is nothing on
        // one of those to look the report up in.
        if (!iface.isHid())
            continue;

        // Without report IDs there is no report 0x01 to speak of — there is one
        // unnumbered input report — so such an interface cannot be identified as
        // the one wanted, and saying so is better than guessing.
        if (iface.hid.descriptor.usesReportIds()
            && iface.hid.descriptor.find(kForceReportId, ReportType::Input)) {
            return &iface;
        }
    }
    return nullptr;
}

bool AlphaEvoForceTrace::isPossible() const
{
    return m_state == ForceTraceSource::Available || m_state == ForceTraceSource::OpenFailed;
}

// --- Holding the interface --------------------------------------------------

void AlphaEvoForceTrace::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;

    if (m_enabled) {
        if (!attach())
            m_enabled = false;  // Nothing was opened, so nothing is being read.
    } else {
        detach();
    }
}

bool AlphaEvoForceTrace::attach()
{
    const UsbInterfaceInfo *iface = forceInterface();
    if (!m_usbDevice || !iface)
        return false;

    // Claimed, not opened outright: the analyzer may want this same interface,
    // and on some firmware it is the one the configuration protocol answers on.
    // Whoever else holds it keeps it.
    IUsbDeviceHidInterface *device = m_usbDevice->openHidFor(this, *iface);
    if (!device) {
        qCWarning(hidcenterDevice).noquote()
            << "The force trace could not open" << iface->displayName() << ":"
            << m_usbDevice->lastError();
        setState(ForceTraceSource::OpenFailed);
        return false;
    }

    m_device = device;
    m_usesReportIds = device->descriptor().usesReportIds();

    m_reportConnection =
        connect(device, &IUsbDeviceHidInterface::inputReport, this, &AlphaEvoForceTrace::onInputReport);

    // An unplug is the one thing that takes the source away without anything
    // else telling us.
    m_deviceGoneConnection =
        connect(device, &IUsbDeviceHidInterface::disconnected, this, &AlphaEvoForceTrace::onDeviceLost);

    setState(ForceTraceSource::Available);
    return true;
}

void AlphaEvoForceTrace::detach()
{
    disconnect(m_reportConnection);
    m_reportConnection = {};
    disconnect(m_deviceGoneConnection);
    m_deviceGoneConnection = {};
    m_device = nullptr;

    // Letting go is the half that matters. While an interface is open the kernel
    // delivers every report on it and this process reads them, whether or not
    // anything decodes them — so disconnecting alone would leave a kilohertz
    // stream running for the rest of the session.
    if (m_usbDevice) {
        if (const UsbInterfaceInfo *iface = forceInterface())
            m_usbDevice->releaseFor(this, *iface);
    }
}

void AlphaEvoForceTrace::onDeviceLost()
{
    m_enabled = false;
    detach();
    setState(ForceTraceSource::DeviceGone);
}

// --- Reading ----------------------------------------------------------------

void AlphaEvoForceTrace::onInputReport(const QByteArray &report)
{
    qreal value = 0.0;
    if (decodeForceSample(report, m_usesReportIds, &value))
        Q_EMIT forceSampled(value);
}

void AlphaEvoForceTrace::setState(ForceTraceSource state)
{
    if (m_state == state)
        return;

    m_state = state;
    Q_EMIT sourceStateChanged(m_state);
}

// --- What to tell the user --------------------------------------------------

QString AlphaEvoForceTrace::sourceDescription() const
{
    switch (m_state) {
    case ForceTraceSource::Available:
        return {};
    case ForceTraceSource::NoSuchReport:
        return tr("This wheelbase does not report force (no input report 0x01).");
    case ForceTraceSource::Offline:
        return tr("No wheelbase connected: nothing to trace.");
    case ForceTraceSource::DeviceGone:
        return tr("Wheelbase unplugged.");
    case ForceTraceSource::OpenFailed:
        return tr("Could not open the wheelbase to read force. %1")
            .arg(m_usbDevice ? m_usbDevice->lastError() : QString());
    }
    return {};
}

} // namespace hidcenter::simagic_alpha_evo
