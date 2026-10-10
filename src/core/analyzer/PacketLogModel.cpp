// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/analyzer/PacketLogModel.h"

#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QTimer>

namespace hidcenter {
namespace {

/// Publish queued packets at ~20 Hz. Fast enough to feel live, slow enough that
/// a 1 kHz report stream costs 20 model updates a second instead of 1000.
constexpr int kFlushIntervalMs = 50;

QString formatTimestamp(qint64 timestampUs)
{
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(timestampUs / 1000);
    return when.toString(QStringLiteral("hh:mm:ss.zzz"));
}

} // namespace

PacketLogModel::PacketLogModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &PacketLogModel::flush);
    m_flushTimer->start();
}

PacketLogModel::~PacketLogModel() = default;

int PacketLogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_packets.size());
}

int PacketLogModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

const UsbPacket *PacketLogModel::packetAt(int row) const
{
    if (row < 0 || row >= m_packets.size())
        return nullptr;
    return &m_packets.at(row);
}

QVariant PacketLogModel::data(const QModelIndex &index, int role) const
{
    const UsbPacket *packet = packetAt(index.row());
    if (!packet)
        return {};

    if (role == PacketRole)
        return QVariant::fromValue(*packet);

    if (role == Qt::FontRole && index.column() == DataColumn)
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case EndpointColumn:
        case ReportIdColumn:
        case LengthColumn:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
        return {};

    switch (index.column()) {
    case TimeColumn:
        return formatTimestamp(packet->timestampUs);
    case DirectionColumn:
        return trafficDirectionName(packet->direction);
    case InterfaceColumn:
        return packet->interfaceName;
    case EndpointColumn:
        // Blank rather than "0x00" for a control transfer. There is no endpoint 0
        // in an interface's descriptors, and printing one would read as a fact.
        return packet->endpoint == 0
            ? QString()
            : QStringLiteral("0x%1").arg(packet->endpoint, 2, 16, QLatin1Char('0'));
    case ReportIdColumn:
        return QStringLiteral("0x%1").arg(packet->reportId, 2, 16, QLatin1Char('0'));
    case LengthColumn:
        return packet->data.size();
    case DataColumn:
        return packet->toHex();
    default:
        return {};
    }
}

QVariant PacketLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case TimeColumn:
        return tr("Time");
    case DirectionColumn:
        return tr("Dir");
    case InterfaceColumn:
        return tr("Interface");
    case EndpointColumn:
        return tr("EP");
    case ReportIdColumn:
        return tr("Report");
    case LengthColumn:
        return tr("Len");
    case DataColumn:
        return tr("Data");
    default:
        return {};
    }
}

void PacketLogModel::append(const UsbPacket &packet)
{
    m_pending.append(packet);
    ++m_totalReceived;
}

void PacketLogModel::flush()
{
    if (m_pending.isEmpty())
        return;

    // A single burst can exceed the whole buffer; in that case only the newest
    // `capacity` packets can possibly survive, so discard the rest up front.
    if (m_pending.size() > m_capacity) {
        m_totalDropped += quint64(m_pending.size() - m_capacity);
        m_pending.erase(m_pending.begin(), m_pending.end() - m_capacity);
    }

    const int incoming = int(m_pending.size());
    const int overflow = int(m_packets.size()) + incoming - m_capacity;

    if (overflow > 0) {
        const int toRemove = qMin(overflow, int(m_packets.size()));
        beginRemoveRows({}, 0, toRemove - 1);
        m_packets.erase(m_packets.begin(), m_packets.begin() + toRemove);
        endRemoveRows();
        m_totalDropped += quint64(toRemove);
    }

    const int first = int(m_packets.size());
    beginInsertRows({}, first, first + incoming - 1);
    m_packets.append(m_pending);
    endInsertRows();

    m_pending.clear();
    Q_EMIT statisticsChanged();
}

void PacketLogModel::clear()
{
    m_pending.clear();
    m_totalReceived = 0;
    m_totalDropped = 0;

    if (!m_packets.isEmpty()) {
        beginResetModel();
        m_packets.clear();
        endResetModel();
    }
    Q_EMIT statisticsChanged();
}

void PacketLogModel::setCapacity(int capacity)
{
    m_capacity = qMax(1, capacity);
    if (m_packets.size() > m_capacity) {
        const int toRemove = int(m_packets.size()) - m_capacity;
        beginRemoveRows({}, 0, toRemove - 1);
        m_packets.erase(m_packets.begin(), m_packets.begin() + toRemove);
        endRemoveRows();
        m_totalDropped += quint64(toRemove);
        Q_EMIT statisticsChanged();
    }
}

PacketFilterProxyModel::PacketFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void PacketFilterProxyModel::setFilter(const PacketFilter &filter)
{
    m_filter = filter;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateRowsFilter();
#endif
}

bool PacketFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (!m_filter.isActive())
        return true;

    const auto *log = qobject_cast<const PacketLogModel *>(sourceModel());
    if (!log)
        return true;

    Q_UNUSED(sourceParent);
    const UsbPacket *packet = log->packetAt(sourceRow);
    return packet ? m_filter.matches(*packet) : true;
}

} // namespace hidcenter
