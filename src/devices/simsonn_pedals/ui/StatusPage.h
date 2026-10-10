// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include <optional>

#include "core/usb/UsbDeviceInfo.h"

class QGridLayout;
class QLabel;
class QVBoxLayout;

namespace hidcenter::simsonn_pedals {

class FirmwareProbe;

/// What is true about one connected pedal set, as opposed to what can be changed
/// on it.
///
/// The first tab, and so the one selected by default, because it is the tab that
/// still says something useful when nothing else on the device works: a condition
/// at the top saying whether the configuration protocol answered, then the facts
/// about the device, then every interface it exposes with whether each one's node
/// can be read and written. All of that except the firmware version is available
/// without a single transfer, which is deliberate — this page is at its most
/// useful exactly when the others cannot be.
///
/// The Alpha Evo module's StatusPage is the same page for a wheelbase, and this
/// one follows its shape on purpose: the two sit side by side in one application
/// and a user should not have to learn each separately. It is a good deal
/// smaller, because a pedal set has no motor to report a temperature or a fault.
class StatusPage : public QWidget
{
    Q_OBJECT

public:
    /// `device` is the enumeration data, which is all the interface table needs
    /// and is available whether or not anything was opened. `probe` may be null,
    /// in which case the firmware row stays empty and the condition says it was
    /// not checked.
    StatusPage(const QString &modelName, const UsbDeviceInfo &device,
               const std::optional<UsbInterfaceInfo> &vendorInterface, FirmwareProbe *probe,
               QWidget *parent = nullptr);

private Q_SLOTS:
    /// Follow the probe: the firmware version and the condition above it are the
    /// only things on this page that change while it is open.
    void refreshFirmware();

private:
    /// How a condition reads: confirmed, wrong, or not something we could check.
    enum class Severity { Ok, Alarm, Unknown };

    void setSeverityIcon(QLabel *icon, Severity severity) const;

    /// Fill the condition row at the top of the page.
    void setCondition(Severity severity, const QString &text);

    void buildDevice(QVBoxLayout *layout, const QString &modelName);

    /// Every interface the device exposes, one row each, with the one carrying
    /// the configuration protocol picked out and a warning beside any whose node
    /// cannot be both read and written.
    ///
    /// Built once and never refreshed. Enumeration data cannot change underneath
    /// this page: a driver binding or unbinding, or access being granted,
    /// re-enumerates the device, which rebuilds the module and this page with it.
    void buildInterfaces(QVBoxLayout *layout);

    /// One interface's access, in words, with a warning glyph and the fix in its
    /// tooltip when it is anything less than read and write.
    void buildAccessCell(QWidget *parent, QGridLayout *grid, int row, int column,
                         const UsbAccessInfo &access);

    UsbDeviceInfo m_device;
    std::optional<UsbInterfaceInfo> m_vendorInterface;
    FirmwareProbe *m_probe = nullptr;

    QLabel *m_conditionIcon = nullptr;
    QLabel *m_conditionText = nullptr;

    QLabel *m_firmware = nullptr;
};

} // namespace hidcenter::simsonn_pedals
