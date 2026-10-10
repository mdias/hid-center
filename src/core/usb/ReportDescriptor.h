// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "hidcenter/core_export.h"

namespace hidcenter {

enum class ReportType {
    Input,
    Output,
    Feature,
};

HIDCENTER_CORE_EXPORT QString reportTypeName(ReportType type);

/// One Input/Output/Feature main item: `count` elements of `bitSize` bits each,
/// starting at `bitOffset` within its report's payload.
struct HIDCENTER_CORE_EXPORT ReportField
{
    quint16 usagePage = 0;
    quint16 usage = 0;        ///< First declared usage, 0 if the item declared none.
    QVector<quint32> usages;  ///< Extended usages ((page << 16) | usage), in order.

    quint32 bitOffset = 0;    ///< Bits from the start of the report payload.
    quint32 bitSize = 0;      ///< Report Size: bits per element.
    quint32 count = 0;        ///< Report Count: number of elements.

    qint32 logicalMin = 0;
    qint32 logicalMax = 0;

    /// Raw data bits of the Input/Output/Feature main item.
    quint32 flags = 0;

    bool isConstant() const { return flags & 0x01; }  ///< Padding rather than data.
    bool isVariable() const { return flags & 0x02; }
    bool isArray() const { return !isVariable(); }
    bool isRelative() const { return flags & 0x04; }

    quint32 totalBits() const { return bitSize * count; }

    /// "Constant", "Array", "Variable" (+ ", Relative") — for the analyzer table.
    QString flagsDescription() const;
};

/// Every field declared for one (report id, report type) pair.
struct HIDCENTER_CORE_EXPORT ReportInfo
{
    quint8 id = 0;            ///< 0 when the descriptor declares no report IDs.
    ReportType type = ReportType::Input;
    QVector<ReportField> fields;

    quint32 bitLength = 0;    ///< Payload size in bits, excluding any report ID byte.

    /// Payload size in bytes, excluding any report ID byte.
    int byteLength() const { return int((bitLength + 7) / 8); }
};

/// A parsed HID report descriptor.
class HIDCENTER_CORE_EXPORT ReportDescriptor
{
public:
    /// Parse a raw descriptor. On malformed input this returns everything it
    /// managed to decode and, if `error` is non-null, sets it to a description
    /// of the problem — a partial report list is far more useful than nothing
    /// when staring at an unknown device.
    static ReportDescriptor parse(const QByteArray &raw, QString *error = nullptr);

    const QVector<ReportInfo> &reports() const { return m_reports; }
    const QByteArray &raw() const { return m_raw; }

    /// True when the descriptor declares report IDs, meaning every transfer
    /// carries the ID as its first byte.
    bool usesReportIds() const { return m_usesReportIds; }

    quint16 topUsagePage() const { return m_topUsagePage; }
    quint16 topUsage() const { return m_topUsage; }

    bool isEmpty() const { return m_reports.isEmpty(); }

    const ReportInfo *find(quint8 id, ReportType type) const;

    /// Total bytes on the wire for a report, including the report ID byte when
    /// the descriptor uses IDs.
    int transferLength(const ReportInfo &report) const;

private:
    QVector<ReportInfo> m_reports;
    QByteArray m_raw;
    bool m_usesReportIds = false;
    quint16 m_topUsagePage = 0;
    quint16 m_topUsage = 0;
};

} // namespace hidcenter
