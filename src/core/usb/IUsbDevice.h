// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "core/usb/IUsbDeviceHidInterface.h"
#include "core/usb/UsbDeviceInfo.h"
#include "hidcenter/core_export.h"

namespace hidcenter {

/// One physical device, and a device module's only handle on hardware.
///
/// The device owns every interface it opens and hands out non-owning pointers,
/// so a module never has to think about lifetimes, and repeated requests for the
/// same interface share one handle rather than fighting over it.
///
/// It also exists so the protocol analyzer can observe traffic: because all I/O
/// goes through handles this object owns, the analyzer can attach to every one
/// of them — including interfaces opened long after the analyzer started — with
/// no cooperation from the module.
///
/// **One interface, several users.** An open interface is not free: the kernel
/// delivers every input report it carries to every reader, and a wheelbase emits
/// those at about a kilohertz, so an interface nobody is reading should not be
/// left open. But more than one part of the application may want the same one at
/// the same time — the analyzer and a module reading a live value, say — and
/// neither may take it from the other.
///
/// So a handle is held by *claims*. openInterface() takes a permanent one and is
/// what most callers want. openFor() takes one on behalf of an owner and
/// releaseFor() gives it back; the interface closes when the last claim goes, and
/// **a permanent claim outranks every temporary one**, so an interface a module
/// opened for its own protocol can never be closed underneath it.
///
/// Closing destroys the handle, which is why everything holding an
/// IUsbDeviceInterface* across time watches QObject::destroyed for it.
class HIDCENTER_CORE_EXPORT IUsbDevice : public QObject
{
    Q_OBJECT

public:
    explicit IUsbDevice(QObject *parent = nullptr) : QObject(parent) {}
    ~IUsbDevice() override = default;

    virtual const UsbDeviceInfo &info() const = 0;

    /// Every interface the device exposes, as enumeration found it. Includes
    /// interfaces with no hidraw node — which is the point of this layer, since
    /// those carry endpoints nothing else in the application could reach.
    const QVector<UsbInterfaceInfo> &interfaces() const { return info().interfaces; }

    /// True when there is no hardware behind this object.
    ///
    /// The application builds one of these for every device its modules support
    /// but which is not plugged in, so a module's pages can be opened and edited
    /// with the device absent. **A module must not attempt any transfer while
    /// this holds**, and must not start a polling timer: there is nothing to
    /// transfer with. Every open below refuses anyway, so this is about not
    /// asking rather than about safety.
    virtual bool isOffline() const = 0;

    /// Open (or return the already-open) handle for a specific interface.
    ///
    /// Modules hold these structs already, via info().interfaces — this is the
    /// way to walk them, which is what a module does when it has to probe for
    /// the interface a vendor protocol actually answers on.
    virtual IUsbDeviceInterface *openInterface(const UsbInterfaceInfo &interfaceInfo) = 0;

    /// openInterface() for the interface with this bInterfaceNumber. Returns
    /// nullptr when the device has no such interface or it cannot be opened;
    /// lastError() then explains why.
    virtual IUsbDeviceInterface *openInterfaceByNumber(int interfaceNumber) = 0;

    /// Open the first HID interface whose top-level collection matches. Usually
    /// the better choice for a vendor protocol, since interface numbers vary
    /// across firmware revisions while the usage rarely does.
    virtual IUsbDeviceHidInterface *openInterfaceByUsage(quint16 usagePage, quint16 usage) = 0;

    /// openInterface(), refusing anything that is not a HID interface.
    ///
    /// Returns the derived type, so a module wanting feature reports never has
    /// to downcast and never gets a handle that would refuse them at runtime.
    virtual IUsbDeviceHidInterface *openHidInterface(const UsbInterfaceInfo &interfaceInfo) = 0;

    /// Open the interface owning `endpoint`, so something can be sent to it.
    ///
    /// `endpoint` is a bEndpointAddress with its direction bit, matching
    /// UsbEndpointInfo::address. Endpoints belong to interfaces and an interface
    /// has to be open before any of its endpoints can carry anything, so this is
    /// the one call that turns an endpoint number into something usable.
    virtual IUsbDeviceInterface *openInterfaceForEndpoint(quint8 endpoint) = 0;

    /// Open every interface the device exposes. Used by the analyzer so it can
    /// see traffic on interfaces the module itself never touches.
    virtual QVector<IUsbDeviceInterface *> openAllInterfaces() = 0;

    /// Open — or share — an interface for as long as `owner` needs it.
    ///
    /// The interface stays open while at least one claim is on it and closes when
    /// the last one is released, so two parts of the application can read the
    /// same interface without either taking it away from the other. Claiming
    /// twice from one owner is the same as claiming once, so a caller need not
    /// track whether it already holds it.
    ///
    /// An interface any plain openInterface() asked for is never closed by
    /// releasing one of these. Claims are also dropped when `owner` is destroyed,
    /// so a missed release leaks nothing.
    ///
    /// Returns nullptr if the interface could not be opened, with lastError()
    /// explaining why — most often a permissions problem on the device node.
    virtual IUsbDeviceInterface *openFor(QObject *owner,
                                         const UsbInterfaceInfo &interfaceInfo) = 0;

    /// openFor(), refusing anything that is not a HID interface.
    virtual IUsbDeviceHidInterface *openHidFor(QObject *owner,
                                               const UsbInterfaceInfo &interfaceInfo) = 0;

    /// openAllInterfaces(), with a claim by `owner` on each one this opens.
    virtual QVector<IUsbDeviceInterface *> openAllInterfacesFor(QObject *owner) = 0;

    /// Give back one claim. Does nothing if `owner` did not hold one.
    virtual void releaseFor(QObject *owner, const UsbInterfaceInfo &interfaceInfo) = 0;

    /// Give back every claim `owner` holds. What an owner's destruction does, and
    /// the tidy way for one to let go of everything at once.
    virtual void releaseAllFor(QObject *owner) = 0;

    /// Handles currently open, in the order they were opened.
    virtual QVector<IUsbDeviceInterface *> openInterfaces() const = 0;

    virtual QString lastError() const = 0;

Q_SIGNALS:
    /// A new interface handle became available. Emitted for every successful
    /// open, so late subscribers such as the analyzer can catch up.
    void interfaceOpened(hidcenter::IUsbDeviceInterface *interface);

    /// The physical device went away.
    void deviceLost();
};

} // namespace hidcenter
