// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/DeviceModule.h"

#include "core/Logging.h"
#include "core/usb/IUsbBackend.h"
#include "core/usb/UsbDeviceBase.h"
#include "core/plugin/IDevice.h"
#include "core/plugin/IDevicePlugin.h"

namespace hidcenter {

DeviceModule::DeviceModule(IUsbBackend *backend, IDevicePlugin *plugin, const UsbDeviceInfo &info,
                           QObject *parent)
    : QObject(parent)
    , m_info(info)
    , m_plugin(plugin)
{
    // Withholding the backend is the whole of what makes a module offline: the
    // offline device refuses every open, which is exactly the truth about a
    // wheelbase that is not plugged in.
    if (backend) {
        m_usb = backend->openDevice(info).release();
        m_usb->setParent(this);
    } else {
        m_usb = new OfflineUsbDevice(info, this);
    }
}

DeviceModule::~DeviceModule()
{
    // Order matters, so state it rather than leaving it to QObject. Both are
    // children of this object, and children are destroyed in creation order —
    // device first, module second — which would pull the interface handles out
    // from under a module still driving them from a worker thread.
    delete m_device;
    m_device = nullptr;
}

QString DeviceModule::displayName() const
{
    return m_plugin ? m_plugin->displayNameFor(m_info) : m_info.describe();
}

void DeviceModule::start()
{
    if (m_started)
        return;
    m_started = true;

    if (!m_plugin)
        return;

    m_device = m_plugin->createDevice(m_usb, this);
    if (!m_device) {
        qCWarning(hidcenterApp) << m_plugin->familyName() << "declined to create a device for"
                             << m_info.devicePath;
        return;
    }

    connect(m_device, &IDevice::statusMessage, this, &DeviceModule::statusMessage);
    connect(m_device, &IDevice::infoFieldsChanged, this, &DeviceModule::infoFieldsChanged);

    if (!m_device->initialize(&m_startError)) {
        qCWarning(hidcenterApp) << "Device module failed to initialize:" << m_startError;
        Q_EMIT statusMessage(m_startError);
    }
}

} // namespace hidcenter
