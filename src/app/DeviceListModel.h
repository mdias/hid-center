// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "core/usb/UsbDeviceInfo.h"

namespace hidcenter {

class PluginManager;

/// The sidebar: the application's own pages, then every connected device we have
/// a module for, then — when the debug option is on — placeholders for the
/// devices we support but cannot see.
class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /// What a row stands for. Position alone used to be enough, when Home was
    /// the only thing that was not a device; with two fixed rows and two kinds
    /// of device row it is worth naming.
    enum class RowKind { Home, Telemetry, Device };

    static constexpr int HomeRow = 0;
    static constexpr int TelemetryRow = 1;

    /// Rows before the first device. Everything that maps a row to a device goes
    /// through this rather than through a literal 1.
    static constexpr int FixedRows = 2;

    static constexpr int DevicePathRole = Qt::UserRole + 1;
    static constexpr int RowKindRole = Qt::UserRole + 2;

    explicit DeviceListModel(PluginManager *plugins, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /// Devices with no module are ignored: the sidebar lists only what we can
    /// actually configure.
    void addDevice(const UsbDeviceInfo &device);
    void removeDevice(const QString &devicePath);
    void setDevices(const QVector<UsbDeviceInfo> &devices);

    /// Also list every device our modules claim but which is not plugged in, so
    /// their pages can be reached without the hardware.
    void setShowDisconnected(bool show);

    int deviceCount() const { return int(m_devices.size()); }

    RowKind kindAt(int row) const;

    /// Device for a model row, or nullptr for the fixed rows and out-of-range
    /// ones. Offline rows return a synthetic info, which is exactly what the
    /// caller needs to build a page for one.
    const UsbDeviceInfo *deviceAt(int row) const;

    /// Whether the device at this row is a placeholder for absent hardware.
    bool isOfflineRow(int row) const;

    /// The synthetic device path a placeholder carries, and how to recognise
    /// one. Public because the window has to identify a placeholder's page
    /// after its row has already gone.
    static QString offlinePathFor(const UsbDeviceId &id);
    static bool isOfflinePath(const QString &devicePath);

    int rowForDevicePath(const QString &devicePath) const;

private:
    struct Entry
    {
        UsbDeviceInfo info;
        QString label;
    };

    /// Recompute labels so that several devices of the same model are told
    /// apart — by serial when the device reports one, by an index otherwise.
    void relabel();

    /// Replace the offline placeholders wholesale. They are always the last
    /// rows, so this is a remove and an insert of one contiguous run, which
    /// leaves a selection anywhere above it untouched.
    void rebuildOfflineRows();

    PluginManager *m_plugins = nullptr;

    QVector<Entry> m_devices;  ///< Connected.
    QVector<Entry> m_offline;  ///< Synthetic, and only while m_showDisconnected.

    bool m_showDisconnected = false;
};

} // namespace hidcenter
