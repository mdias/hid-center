// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "hidcenter/core_export.h"

class QWidget;

namespace hidcenter {

/// One tab in a device's main area.
struct HIDCENTER_CORE_EXPORT DevicePage
{
    QString title;
    QWidget *widget = nullptr;
};

/// One read-only fact about a device, shown in its header.
struct HIDCENTER_CORE_EXPORT DeviceInfoField
{
    QString label;    ///< Short caption, e.g. "Firmware".
    QString value;
    QString tooltip;  ///< Optional detail, e.g. the raw value behind a formatted one.
};

/// One connected device, created by its family module.
///
/// The module builds the pages; the application arranges them and appends the
/// protocol analyzer after them. Ownership of the page widgets passes to the
/// application, which reparents them into the tab widget.
///
/// **Lifetime is the device's, not a window's.** One of these is created as soon
/// as the device is detected and destroyed when it goes away, whether or not
/// anyone ever looks at it — which is what lets a module poll, drive a worker
/// thread, or feed a wheelbase telemetry from the moment it is plugged in.
class HIDCENTER_CORE_EXPORT IDevice : public QObject
{
    Q_OBJECT

public:
    explicit IDevice(QObject *parent = nullptr) : QObject(parent) {}
    ~IDevice() override = default;

    /// Called once, after construction. Return false to refuse the device — for
    /// instance when the interface carrying the protocol cannot be opened — and
    /// set `error` to something the user can act on.
    ///
    /// **Nobody may be looking.** This runs when the device is detected, which is
    /// usually long before any page is asked for and may be forever before it, so
    /// it must not assume a view exists. `error` is kept and turned into a tab if
    /// and when someone opens one.
    virtual bool initialize(QString *error) = 0;

    /// The tabs this device contributes, in display order. Called once — and
    /// possibly never, for a device nobody opens.
    virtual QVector<DevicePage> pages() = 0;

    /// Read-only facts worth seeing whatever tab is open — a firmware version,
    /// say. Shown in the device header, beside the device name.
    ///
    /// Not pure: a module with nothing to publish overrides nothing. Re-read
    /// whenever infoFieldsChanged() is emitted.
    virtual QVector<DeviceInfoField> infoFields() const { return {}; }

Q_SIGNALS:
    /// Progress or trouble worth showing in the status bar.
    void statusMessage(const QString &message);

    /// infoFields() would now return something different.
    void infoFieldsChanged();
};

} // namespace hidcenter
