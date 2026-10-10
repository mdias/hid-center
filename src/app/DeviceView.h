// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "core/usb/UsbDeviceInfo.h"

class QHBoxLayout;
class QTabWidget;

namespace hidcenter {

class IUsbDevice;
class DeviceModule;
class UsbPacketSource;
class TrafficIndicator;

/// Everything shown for one connected device: the pages its module contributes,
/// with the protocol analyzer appended last.
///
/// A window onto a DeviceModule, which exists before this does and outlives
/// nothing: the two are discarded together. The view owns what is about being on
/// screen — the header, the arrows, the capture source, the tabs — and none of
/// the module, the context, or the handles underneath them.
class DeviceView : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceView(DeviceModule *module, QWidget *parent = nullptr);

    const UsbDeviceInfo &info() const;

private Q_SLOTS:
    /// Rebuild the read-only fields the module publishes.
    void refreshInfoFields();

    /// Grey the arrows out when there is nothing left to observe, and bring them
    /// back when an interface opens.
    void refreshTrafficAvailability();

private:
    void buildHeader();
    void addModulePages();

    /// Borrowed, not owned, and guaranteed to outlive this view: MainWindow
    /// discards the two together.
    DeviceModule *m_module = nullptr;

    /// The module's context, kept only to save reaching through it constantly.
    IUsbDevice *m_context = nullptr;

    QTabWidget *m_tabs = nullptr;

    /// Watches the handles the context owns, without opening any of its own.
    UsbPacketSource *m_source = nullptr;

    /// Device name and traffic arrows on the left, module info fields on the right.
    QHBoxLayout *m_headerLayout = nullptr;
    QHBoxLayout *m_infoLayout = nullptr;
    TrafficIndicator *m_indicator = nullptr;
};

} // namespace hidcenter
