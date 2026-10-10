// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QSortFilterProxyModel>

#include "core/analyzer/UsbPacket.h"
#include "core/analyzer/PacketFilter.h"
#include "hidcenter/core_export.h"

class QTimer;

namespace hidcenter {

/// The analyzer's packet log.
///
/// Two things keep this usable against real hardware, where a wheelbase happily
/// emits input reports at a kilohertz:
///
///  * a bounded ring buffer, so a capture left running overnight cannot exhaust
///    memory; and
///  * batched inserts, so the view is touched a few times a second instead of a
///    thousand times, which is the difference between a responsive window and an
///    unusable one.
class HIDCENTER_CORE_EXPORT PacketLogModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TimeColumn,
        DirectionColumn,
        InterfaceColumn,
        EndpointColumn,
        ReportIdColumn,
        LengthColumn,
        DataColumn,
        ColumnCount,
    };

    /// Access the underlying packet, for filtering and for detail views.
    static constexpr int PacketRole = Qt::UserRole + 1;

    explicit PacketLogModel(QObject *parent = nullptr);
    ~PacketLogModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    const UsbPacket *packetAt(int row) const;

    /// Maximum packets retained. Older ones are dropped as new ones arrive.
    int capacity() const { return m_capacity; }
    void setCapacity(int capacity);

    /// Packets seen since the last clear, including those already dropped by the
    /// ring buffer — the log shows a window, this counts the whole stream.
    quint64 totalReceived() const { return m_totalReceived; }

    /// Packets dropped because the buffer was full.
    quint64 totalDropped() const { return m_totalDropped; }

public Q_SLOTS:
    /// Queue a packet. It becomes visible on the next flush.
    void append(const hidcenter::UsbPacket &packet);

    void clear();

    /// Publish anything queued immediately, rather than waiting for the timer.
    void flush();

Q_SIGNALS:
    /// Emitted after a flush that changed anything, so the UI can update its
    /// counters without polling.
    void statisticsChanged();

private:
    QList<UsbPacket> m_packets;
    QList<UsbPacket> m_pending;
    QTimer *m_flushTimer = nullptr;
    int m_capacity = 50000;
    quint64 m_totalReceived = 0;
    quint64 m_totalDropped = 0;
};

/// Applies a PacketFilter to a PacketLogModel.
class HIDCENTER_CORE_EXPORT PacketFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit PacketFilterProxyModel(QObject *parent = nullptr);

    const PacketFilter &filter() const { return m_filter; }
    void setFilter(const PacketFilter &filter);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    PacketFilter m_filter;
};

} // namespace hidcenter
