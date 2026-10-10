// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/DeviceListModel.h"

#include "core/Logging.h"
#include "core/plugin/PluginManager.h"

#include <QApplication>
#include <QHash>
#include <QStyle>

namespace hidcenter {
namespace {

/// Prefix marking a device path as synthetic. It can never collide with a real
/// node, which matters: both the sidebar and the window's cache of device views
/// are keyed by this string, and a collision would make unplugging a real device
/// tear down a placeholder's page instead.
constexpr auto kOfflinePathPrefix = "offline:";

} // namespace

QString DeviceListModel::offlinePathFor(const UsbDeviceId &id)
{
    return QLatin1String(kOfflinePathPrefix) + id.toString();
}

bool DeviceListModel::isOfflinePath(const QString &devicePath)
{
    return devicePath.startsWith(QLatin1String(kOfflinePathPrefix));
}

DeviceListModel::DeviceListModel(PluginManager *plugins, QObject *parent)
    : QAbstractListModel(parent)
    , m_plugins(plugins)
{
}

int DeviceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return FixedRows + int(m_devices.size()) + int(m_offline.size());
}

DeviceListModel::RowKind DeviceListModel::kindAt(int row) const
{
    switch (row) {
    case HomeRow:
        return RowKind::Home;
    case TelemetryRow:
        return RowKind::Telemetry;
    default:
        return RowKind::Device;
    }
}

const UsbDeviceInfo *DeviceListModel::deviceAt(int row) const
{
    const int index = row - FixedRows;
    if (index < 0)
        return nullptr;
    if (index < m_devices.size())
        return &m_devices.at(index).info;

    const int offlineIndex = index - m_devices.size();
    if (offlineIndex < m_offline.size())
        return &m_offline.at(offlineIndex).info;

    return nullptr;
}

bool DeviceListModel::isOfflineRow(int row) const
{
    const int index = row - FixedRows - int(m_devices.size());
    return index >= 0 && index < m_offline.size();
}

int DeviceListModel::rowForDevicePath(const QString &devicePath) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).info.devicePath == devicePath)
            return FixedRows + i;
    }
    for (int i = 0; i < m_offline.size(); ++i) {
        if (m_offline.at(i).info.devicePath == devicePath)
            return FixedRows + int(m_devices.size()) + i;
    }
    return -1;
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};

    const RowKind kind = kindAt(index.row());
    const bool offline = isOfflineRow(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (kind) {
        case RowKind::Home:
            return tr("Home");
        case RowKind::Telemetry:
            return tr("Telemetry");
        case RowKind::Device:
            break;
        }
        return offline ? m_offline.at(index.row() - FixedRows - m_devices.size()).label
                       : m_devices.at(index.row() - FixedRows).label;

    case Qt::DecorationRole:
        if (QStyle *style = QApplication::style()) {
            switch (kind) {
            case RowKind::Home:
                return style->standardIcon(QStyle::SP_DirHomeIcon);
            case RowKind::Telemetry:
                return style->standardIcon(QStyle::SP_ComputerIcon);
            case RowKind::Device:
                break;
            }
            // A placeholder gets a different icon, so the list reads at a glance
            // rather than only through the "(offline)" at the end of a label.
            return style->standardIcon(offline ? QStyle::SP_DriveNetIcon
                                               : QStyle::SP_DriveHDIcon);
        }
        return {};

    case Qt::ToolTipRole: {
        switch (kind) {
        case RowKind::Home:
            return tr("Overview and diagnostics");
        case RowKind::Telemetry:
            return tr("Sim telemetry shared with every device module");
        case RowKind::Device:
            break;
        }

        const UsbDeviceInfo *info = deviceAt(index.row());
        if (!info)
            return {};

        if (offline) {
            return tr("%1\nUSB %2\n\nNot connected. Its pages can be opened and changed, but "
                      "nothing is sent to or read from a device that is not there.")
                .arg(info->describe(), info->id.toString());
        }

        QString tip = tr("%1\nUSB %2\n%3 HID interface(s)")
                          .arg(info->describe(), info->id.toString())
                          .arg(info->interfaces.size());
        if (!info->serial.isEmpty())
            tip += tr("\nSerial: %1").arg(info->serial);
        return tip;
    }

    case RowKindRole:
        return int(kind);

    case DevicePathRole: {
        const UsbDeviceInfo *info = deviceAt(index.row());
        return info ? info->devicePath : QString();
    }

    default:
        return {};
    }
}

void DeviceListModel::addDevice(const UsbDeviceInfo &device)
{
    if (!m_plugins || !m_plugins->handles(device.id)) {
        qCDebug(hidcenterApp) << "Ignoring unsupported device" << device.id.toString()
                           << device.devicePath;
        return;
    }
    if (rowForDevicePath(device.devicePath) >= 0)
        return;

    const int row = FixedRows + int(m_devices.size());
    beginInsertRows({}, row, row);
    m_devices.append({device, QString()});
    endInsertRows();

    relabel();

    // The device that just arrived may have a placeholder standing in for it.
    rebuildOfflineRows();
}

void DeviceListModel::removeDevice(const QString &devicePath)
{
    const int row = rowForDevicePath(devicePath);
    if (row < 0 || isOfflineRow(row))
        return;

    beginRemoveRows({}, row, row);
    m_devices.remove(row - FixedRows);
    endRemoveRows();

    relabel();

    // And now it may need one.
    rebuildOfflineRows();
}

void DeviceListModel::setDevices(const QVector<UsbDeviceInfo> &devices)
{
    beginResetModel();
    m_devices.clear();
    m_offline.clear();
    for (const UsbDeviceInfo &device : devices) {
        if (m_plugins && m_plugins->handles(device.id))
            m_devices.append({device, QString()});
    }
    endResetModel();

    relabel();
    rebuildOfflineRows();
}

void DeviceListModel::setShowDisconnected(bool show)
{
    if (m_showDisconnected == show)
        return;

    m_showDisconnected = show;
    rebuildOfflineRows();
}

void DeviceListModel::rebuildOfflineRows()
{
    QVector<Entry> wanted;

    if (m_showDisconnected && m_plugins) {
        // Which ids are already on screen for real. A placeholder for one of
        // those would be a duplicate of a device the user can actually use.
        QVector<UsbDeviceId> connected;
        connected.reserve(m_devices.size());
        for (const Entry &entry : m_devices)
            connected.append(entry.info.id);

        for (const PluginManager::LoadedPlugin &plugin : m_plugins->plugins()) {
            for (const UsbDeviceId &id : plugin.supportedIds) {
                if (connected.contains(id))
                    continue;

                UsbDeviceInfo info;
                info.id = id;
                info.devicePath = offlinePathFor(id);
                info.product =
                    plugin.plugin ? plugin.plugin->displayNameFor(info) : plugin.familyName;

                // The id goes in the label rather than being left to relabel()'s
                // serial-or-#N disambiguation: a family commonly claims several
                // ids under one product name, and neither a serial nor an index
                // says anything about hardware that is not there.
                Entry entry;
                entry.info = info;
                entry.label = tr("%1 (%2, offline)").arg(info.product, id.toString());
                wanted.append(entry);
            }
        }
    }

    if (wanted.size() == m_offline.size()) {
        bool same = true;
        for (int i = 0; i < wanted.size() && same; ++i)
            same = wanted.at(i).info.devicePath == m_offline.at(i).info.devicePath;
        if (same)
            return;
    }

    const int first = FixedRows + int(m_devices.size());

    if (!m_offline.isEmpty()) {
        beginRemoveRows({}, first, first + int(m_offline.size()) - 1);
        m_offline.clear();
        endRemoveRows();
    }

    if (!wanted.isEmpty()) {
        beginInsertRows({}, first, first + int(wanted.size()) - 1);
        m_offline = wanted;
        endInsertRows();
    }
}

void DeviceListModel::relabel()
{
    if (m_devices.isEmpty())
        return;

    // Two devices of the same model produce the same base name, which would make
    // the sidebar ambiguous. Count the names first, then disambiguate only those
    // that actually collide.
    QHash<QString, int> nameCounts;
    QVector<QString> baseNames;
    baseNames.reserve(m_devices.size());

    for (const Entry &entry : m_devices) {
        QString name;
        if (m_plugins) {
            if (IDevicePlugin *plugin = m_plugins->pluginFor(entry.info.id))
                name = plugin->displayNameFor(entry.info);
        }
        if (name.isEmpty())
            name = entry.info.describe();
        baseNames.append(name);
        nameCounts[name] += 1;
    }

    QHash<QString, int> seen;
    for (int i = 0; i < m_devices.size(); ++i) {
        const QString &base = baseNames.at(i);
        Entry &entry = m_devices[i];

        if (nameCounts.value(base) <= 1) {
            entry.label = base;
        } else if (!entry.info.serial.isEmpty()) {
            entry.label = QStringLiteral("%1 (%2)").arg(base, entry.info.serial);
        } else {
            entry.label = QStringLiteral("%1 #%2").arg(base).arg(++seen[base]);
        }
    }

    Q_EMIT dataChanged(index(FixedRows), index(FixedRows + int(m_devices.size()) - 1),
                       {Qt::DisplayRole, Qt::ToolTipRole});
}

} // namespace hidcenter
