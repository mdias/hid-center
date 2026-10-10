// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>
#include <QtPlugin>

#include "core/usb/IUsbDevice.h"
#include "core/plugin/IDevice.h"

namespace hidcenter {

/// Bumped whenever anything in IDevicePlugin, IDevice, IUsbDevice or the
/// interface classes changes shape. PluginManager refuses modules built against
/// a different value, which turns a mystery crash into a clear log line.
/// 2: the HID interface gained the explicit-length getFeatureReport() overload
///    and the device's openPath() became the public open().
/// 3: IDevice gained infoFields() and infoFieldsChanged().
/// 4: The device object gained isOffline(), and with it the rule that a module
///    must issue no transfers when it is set.
/// 5: The interface struct gained `driver`, which a module reads through info().
/// 6: It gained `access`, in the same way.
/// 7: The whole shape changed. A module is handed an IUsbDevice rather than a
///    DeviceContext; IHidDevice became IUsbDeviceHidInterface under a generic
///    IUsbDeviceInterface that carries endpoints and interrupt transfers; and
///    HidDeviceInfo, HidDeviceId and HidInterfaceInfo became UsbDeviceInfo,
///    UsbDeviceId and UsbInterfaceInfo, the last with its HID half moved into a
///    nested member and an endpoint list added beside it.
/// 8: `access` moved out of the HID half of the interface struct onto the
///    interface itself, and deviceAccessFor() went away.
///
/// Each of these changes a vtable a module may already hold a pointer into, so
/// modules built against an older value must be rebuilt rather than loaded. 4 is
/// the exception: it adds no vtable entry, but a module compiled against 3 knows
/// nothing of offline devices and would poll one, so it is refused all the same.
/// 5 and 6 add none either, and are refused for a blunter reason: they move
/// fields in a struct passed across the boundary, so an older module would read
/// the wrong bytes for everything after them. 7 does both at once. 8 is 5 and 6
/// again: a field moved.
inline constexpr int kDevicePluginAbiVersion = 8;

/// A device *family* module.
///
/// One module handles one family and may claim any number of VID/PID pairs, so
/// that variants of the same product share their code and pages. Nothing in the
/// application knows about specific devices; all of that lives behind here.
class IDevicePlugin
{
public:
    virtual ~IDevicePlugin() = default;

    /// Must return kDevicePluginAbiVersion as compiled into the module.
    virtual int abiVersion() const = 0;

    /// Family name, for logs and the Home page (e.g. "Acme Wheelbases").
    virtual QString familyName() const = 0;

    /// Every VID/PID this module claims. This is the single source of truth for
    /// matching — deliberately not duplicated in the plugin's JSON metadata,
    /// where it could silently drift out of sync.
    virtual QVector<UsbDeviceId> supportedIds() const = 0;

    /// Name to show in the sidebar for a specific connected device.
    virtual QString displayNameFor(const UsbDeviceInfo &info) const = 0;

    /// Create the object handling one connected device.
    ///
    /// `device` is how the module reaches the hardware: it lists the interfaces
    /// the device exposes and opens the ones the module needs. It is owned by the
    /// caller and outlives the returned object.
    virtual IDevice *createDevice(IUsbDevice *device, QObject *parent) = 0;
};

} // namespace hidcenter

#define HIDCENTER_DEVICE_PLUGIN_IID "org.hidcenter.IDevicePlugin/1.0"

Q_DECLARE_INTERFACE(hidcenter::IDevicePlugin, HIDCENTER_DEVICE_PLUGIN_IID)
