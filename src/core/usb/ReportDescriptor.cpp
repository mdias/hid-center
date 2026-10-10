// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/ReportDescriptor.h"

#include <QStringList>

namespace hidcenter {
namespace {

// Item prefix layout (HID 1.11 §6.2.2.2):
//   bits 1:0  bSize  (0, 1, 2 or 3, where 3 means 4 data bytes)
//   bits 3:2  bType  (0 Main, 1 Global, 2 Local, 3 Reserved)
//   bits 7:4  bTag
constexpr quint8 kLongItemPrefix = 0xFE;

enum ItemType { TypeMain = 0, TypeGlobal = 1, TypeLocal = 2 };

enum MainTag {
    MainInput = 0x8,
    MainOutput = 0x9,
    MainCollection = 0xA,
    MainFeature = 0xB,
    MainEndCollection = 0xC,
};

enum GlobalTag {
    GlobalUsagePage = 0x0,
    GlobalLogicalMinimum = 0x1,
    GlobalLogicalMaximum = 0x2,
    GlobalPhysicalMinimum = 0x3,
    GlobalPhysicalMaximum = 0x4,
    GlobalUnitExponent = 0x5,
    GlobalUnit = 0x6,
    GlobalReportSize = 0x7,
    GlobalReportId = 0x8,
    GlobalReportCount = 0x9,
    GlobalPush = 0xA,
    GlobalPop = 0xB,
};

enum LocalTag {
    LocalUsage = 0x0,
    LocalUsageMinimum = 0x1,
    LocalUsageMaximum = 0x2,
};

constexpr quint16 kCollectionApplication = 0x01;

quint32 unsignedData(const quint8 *data, int size)
{
    quint32 value = 0;
    for (int i = 0; i < size; ++i)
        value |= quint32(data[i]) << (8 * i);
    return value;
}

qint32 signedData(const quint8 *data, int size)
{
    const quint32 raw = unsignedData(data, size);
    switch (size) {
    case 1:
        return qint32(qint8(raw & 0xFF));
    case 2:
        return qint32(qint16(raw & 0xFFFF));
    case 4:
        return qint32(raw);
    default:
        return qint32(raw);
    }
}

/// Global item state, saved and restored by Push/Pop.
struct GlobalState
{
    quint16 usagePage = 0;
    qint32 logicalMin = 0;
    qint32 logicalMax = 0;
    quint32 reportSize = 0;
    quint32 reportCount = 0;
    quint8 reportId = 0;
};

/// Local item state, cleared after every Main item.
struct LocalState
{
    QVector<quint32> usages;
    bool haveUsageMin = false;
    bool haveUsageMax = false;
    quint32 usageMin = 0;
    quint32 usageMax = 0;

    void clear() { *this = LocalState(); }
};

/// Extended usages are 32 bits (page in the high half). A usage given as a
/// 4-byte item carries its own page; anything shorter inherits the current
/// Usage Page global.
quint32 makeExtendedUsage(quint32 value, int size, quint16 currentPage)
{
    if (size == 4)
        return value;
    return (quint32(currentPage) << 16) | (value & 0xFFFF);
}

} // namespace

QString reportTypeName(ReportType type)
{
    switch (type) {
    case ReportType::Input:
        return QStringLiteral("Input");
    case ReportType::Output:
        return QStringLiteral("Output");
    case ReportType::Feature:
        return QStringLiteral("Feature");
    }
    return QStringLiteral("Unknown");
}

QString ReportField::flagsDescription() const
{
    QStringList parts;
    if (isConstant())
        parts << QStringLiteral("Constant");
    else
        parts << (isVariable() ? QStringLiteral("Variable") : QStringLiteral("Array"));
    if (isRelative())
        parts << QStringLiteral("Relative");
    return parts.join(QStringLiteral(", "));
}

ReportDescriptor ReportDescriptor::parse(const QByteArray &raw, QString *error)
{
    ReportDescriptor result;
    result.m_raw = raw;

    const auto *bytes = reinterpret_cast<const quint8 *>(raw.constData());
    const int size = raw.size();

    GlobalState global;
    QVector<GlobalState> globalStack;
    LocalState local;

    int collectionDepth = 0;
    bool haveTopUsage = false;

    // Reports are keyed by (id, type) and accumulated as main items are seen.
    // The index keeps lookup cheap while preserving declaration order in
    // result.m_reports.
    auto reportIndex = [&result](quint8 id, ReportType type) -> ReportInfo & {
        for (ReportInfo &info : result.m_reports) {
            if (info.id == id && info.type == type)
                return info;
        }
        ReportInfo info;
        info.id = id;
        info.type = type;
        result.m_reports.append(info);
        return result.m_reports.last();
    };

    auto fail = [&error](const QString &message) {
        if (error)
            *error = message;
    };

    int pos = 0;
    while (pos < size) {
        const quint8 prefix = bytes[pos];

        if (prefix == kLongItemPrefix) {
            // Long items carry no state we model; skip them intact.
            if (pos + 2 >= size) {
                fail(QStringLiteral("truncated long item at offset %1").arg(pos));
                break;
            }
            const int dataSize = bytes[pos + 1];
            pos += 3 + dataSize;
            if (pos > size) {
                fail(QStringLiteral("long item at offset %1 runs past end of descriptor").arg(pos));
                break;
            }
            continue;
        }

        int dataSize = prefix & 0x03;
        if (dataSize == 3)
            dataSize = 4;
        const int type = (prefix >> 2) & 0x03;
        const int tag = (prefix >> 4) & 0x0F;

        if (pos + 1 + dataSize > size) {
            fail(QStringLiteral("truncated item at offset %1 (needs %2 data bytes)")
                     .arg(pos)
                     .arg(dataSize));
            break;
        }

        const quint8 *data = bytes + pos + 1;
        pos += 1 + dataSize;

        switch (type) {
        case TypeGlobal:
            switch (tag) {
            case GlobalUsagePage:
                global.usagePage = quint16(unsignedData(data, dataSize));
                break;
            case GlobalLogicalMinimum:
                global.logicalMin = signedData(data, dataSize);
                break;
            case GlobalLogicalMaximum:
                // The spec calls this signed, but descriptors in the wild
                // routinely encode e.g. 255 as 0xFF with a minimum of 0 and mean
                // 255, not -1. Follow the usual convention: interpret as signed
                // only when the minimum is itself negative.
                global.logicalMax = global.logicalMin < 0
                    ? signedData(data, dataSize)
                    : qint32(unsignedData(data, dataSize));
                break;
            case GlobalReportSize:
                global.reportSize = unsignedData(data, dataSize);
                break;
            case GlobalReportCount:
                global.reportCount = unsignedData(data, dataSize);
                break;
            case GlobalReportId:
                global.reportId = quint8(unsignedData(data, dataSize));
                result.m_usesReportIds = true;
                break;
            case GlobalPush:
                globalStack.append(global);
                break;
            case GlobalPop:
                if (globalStack.isEmpty()) {
                    fail(QStringLiteral("Pop without matching Push at offset %1").arg(pos));
                } else {
                    global = globalStack.takeLast();
                }
                break;
            case GlobalPhysicalMinimum:
            case GlobalPhysicalMaximum:
            case GlobalUnitExponent:
            case GlobalUnit:
            default:
                // Not modelled; physical units matter for interpretation, not
                // for laying out reports.
                break;
            }
            break;

        case TypeLocal:
            switch (tag) {
            case LocalUsage:
                local.usages.append(
                    makeExtendedUsage(unsignedData(data, dataSize), dataSize, global.usagePage));
                break;
            case LocalUsageMinimum:
                local.usageMin =
                    makeExtendedUsage(unsignedData(data, dataSize), dataSize, global.usagePage);
                local.haveUsageMin = true;
                break;
            case LocalUsageMaximum:
                local.usageMax =
                    makeExtendedUsage(unsignedData(data, dataSize), dataSize, global.usagePage);
                local.haveUsageMax = true;
                break;
            default:
                // Designator/string indices are not modelled.
                break;
            }
            break;

        case TypeMain: {
            switch (tag) {
            case MainCollection: {
                const quint16 collectionType = quint16(unsignedData(data, dataSize));
                if (!haveTopUsage && collectionType == kCollectionApplication
                    && !local.usages.isEmpty()) {
                    const quint32 usage = local.usages.first();
                    result.m_topUsagePage = quint16(usage >> 16);
                    result.m_topUsage = quint16(usage & 0xFFFF);
                    haveTopUsage = true;
                }
                ++collectionDepth;
                break;
            }
            case MainEndCollection:
                if (collectionDepth == 0)
                    fail(QStringLiteral("End Collection without Collection at offset %1").arg(pos));
                else
                    --collectionDepth;
                break;

            case MainInput:
            case MainOutput:
            case MainFeature: {
                const ReportType reportType = tag == MainInput    ? ReportType::Input
                    : tag == MainOutput                           ? ReportType::Output
                                                                  : ReportType::Feature;
                ReportInfo &report = reportIndex(global.reportId, reportType);

                ReportField field;
                field.usagePage = global.usagePage;
                field.bitSize = global.reportSize;
                field.count = global.reportCount;
                field.logicalMin = global.logicalMin;
                field.logicalMax = global.logicalMax;
                field.flags = unsignedData(data, dataSize);
                field.bitOffset = report.bitLength;

                field.usages = local.usages;
                if (local.haveUsageMin && local.haveUsageMax && local.usageMax >= local.usageMin) {
                    // Cap the expansion: a descriptor may declare a usage range
                    // spanning thousands of entries (a full keyboard page), and
                    // materialising all of them helps nobody.
                    constexpr quint32 kMaxExpandedUsages = 256;
                    const quint32 span = local.usageMax - local.usageMin + 1;
                    const quint32 limit = qMin(span, kMaxExpandedUsages);
                    for (quint32 i = 0; i < limit; ++i)
                        field.usages.append(local.usageMin + i);
                }
                if (!field.usages.isEmpty())
                    field.usage = quint16(field.usages.first() & 0xFFFF);

                report.bitLength += field.totalBits();
                report.fields.append(field);

                local.clear();
                break;
            }

            default:
                // Reserved main item; local state is still consumed.
                local.clear();
                break;
            }

            // Local items apply only to the main item that follows them.
            if (tag != MainInput && tag != MainOutput && tag != MainFeature)
                local.clear();
            break;
        }

        default:
            // Reserved item type; already skipped by advancing `pos`.
            break;
        }
    }

    if (collectionDepth != 0)
        fail(QStringLiteral("%1 unclosed collection(s)").arg(collectionDepth));

    return result;
}

const ReportInfo *ReportDescriptor::find(quint8 id, ReportType type) const
{
    for (const ReportInfo &info : m_reports) {
        if (info.id == id && info.type == type)
            return &info;
    }
    return nullptr;
}

int ReportDescriptor::transferLength(const ReportInfo &report) const
{
    return report.byteLength() + (m_usesReportIds ? 1 : 0);
}

} // namespace hidcenter
