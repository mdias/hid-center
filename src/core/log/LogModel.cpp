// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/log/LogModel.h"

#include <QBrush>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QMetaObject>
#include <QTimer>

#include <cstdio>

namespace hidcenter {
namespace {

/// Publish queued entries at ~10 Hz, so a burst of logging costs a couple of
/// model updates rather than one per message.
constexpr int kFlushIntervalMs = 100;

LogModel *g_instance = nullptr;
QtMessageHandler g_previousHandler = nullptr;
bool g_installed = false;

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // Chain first, and unconditionally: the terminal must see every message
    // whether or not the model is alive to capture it.
    // qInstallMessageHandler() hands back Qt's own default handler when nothing
    // else was installed, so this normally chains to it and terminal or journal
    // output is unaffected. The fallback is for the case where it returns null.
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    } else {
        fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
        fflush(stderr);
    }

    if (!g_instance)
        return;

    // Anything logged while we are capturing would recurse forever. This can
    // happen for real: Qt warns from inside model signal handling.
    static thread_local bool inHandler = false;
    if (inHandler)
        return;
    inHandler = true;

    LogEntry entry;
    entry.when = QDateTime::currentDateTime();
    entry.type = type;
    entry.category = QString::fromUtf8(context.category ? context.category : "default");
    entry.message = message;

    // Queued: the handler runs on whatever thread logged, the model lives on the
    // GUI thread. Before the event loop starts these simply queue up and arrive
    // once it does.
    QMetaObject::invokeMethod(g_instance, "append", Qt::QueuedConnection,
                              Q_ARG(hidcenter::LogEntry, entry));

    inHandler = false;
}

} // namespace

int messageSeverity(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return 1;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
        return 3;
    case QtFatalMsg:
        return 4;
    }
    return 1;
}

QString messageTypeName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QCoreApplication::translate("hidcenter", "Debug");
    case QtInfoMsg:
        return QCoreApplication::translate("hidcenter", "Info");
    case QtWarningMsg:
        return QCoreApplication::translate("hidcenter", "Warning");
    case QtCriticalMsg:
        return QCoreApplication::translate("hidcenter", "Critical");
    case QtFatalMsg:
        return QCoreApplication::translate("hidcenter", "Fatal");
    }
    return {};
}

LogModel::LogModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &LogModel::flush);
    m_flushTimer->start();
}

LogModel::~LogModel()
{
    if (g_instance == this)
        uninstall();
}

LogModel *LogModel::instance()
{
    return g_instance;
}

void LogModel::install()
{
    if (g_installed)
        return;

    qRegisterMetaType<LogEntry>("hidcenter::LogEntry");

    // Parented to the application so it outlives every window: messages logged
    // during shutdown still have somewhere to go.
    g_instance = new LogModel(QCoreApplication::instance());
    g_previousHandler = qInstallMessageHandler(messageHandler);
    g_installed = true;
}

void LogModel::uninstall()
{
    if (!g_installed)
        return;

    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;
    g_instance = nullptr;
    g_installed = false;
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

int LogModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

const LogEntry *LogModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return nullptr;
    return &m_entries.at(row);
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    const LogEntry *entry = entryAt(index.row());
    if (!entry)
        return {};

    if (role == SeverityRole)
        return messageSeverity(entry->type);

    if (role == Qt::FontRole && index.column() == MessageColumn)
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);

    if (role == Qt::ForegroundRole) {
        switch (entry->type) {
        case QtWarningMsg:
            return QBrush(QColor(0xd0, 0x7a, 0x00));
        case QtCriticalMsg:
        case QtFatalMsg:
            return QBrush(QColor(0xc0, 0x30, 0x30));
        default:
            return {};
        }
    }

    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
        return {};

    switch (index.column()) {
    case TimeColumn:
        return entry->when.toString(QStringLiteral("hh:mm:ss.zzz"));
    case LevelColumn:
        return messageTypeName(entry->type);
    case CategoryColumn:
        return entry->category;
    case MessageColumn:
        return entry->message;
    default:
        return {};
    }
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case TimeColumn:
        return tr("Time");
    case LevelColumn:
        return tr("Level");
    case CategoryColumn:
        return tr("Category");
    case MessageColumn:
        return tr("Message");
    default:
        return {};
    }
}

void LogModel::append(const LogEntry &entry)
{
    m_pending.append(entry);
}

void LogModel::flush()
{
    if (m_pending.isEmpty())
        return;

    // A burst larger than the whole buffer can only leave its newest entries, so
    // discard the rest before touching the model.
    if (m_pending.size() > m_capacity)
        m_pending.erase(m_pending.begin(), m_pending.end() - m_capacity);

    const int incoming = int(m_pending.size());
    const int overflow = int(m_entries.size()) + incoming - m_capacity;

    if (overflow > 0) {
        const int toRemove = qMin(overflow, int(m_entries.size()));
        beginRemoveRows({}, 0, toRemove - 1);
        m_entries.erase(m_entries.begin(), m_entries.begin() + toRemove);
        endRemoveRows();
    }

    const int first = int(m_entries.size());
    beginInsertRows({}, first, first + incoming - 1);
    m_entries.append(m_pending);
    endInsertRows();

    m_pending.clear();
}

void LogModel::clear()
{
    m_pending.clear();
    if (m_entries.isEmpty())
        return;

    beginResetModel();
    m_entries.clear();
    endResetModel();
}

void LogModel::setCapacity(int capacity)
{
    m_capacity = qMax(1, capacity);
    if (m_entries.size() > m_capacity) {
        const int toRemove = int(m_entries.size()) - m_capacity;
        beginRemoveRows({}, 0, toRemove - 1);
        m_entries.erase(m_entries.begin(), m_entries.begin() + toRemove);
        endRemoveRows();
    }
}

QString LogModel::toPlainText() const
{
    QStringList lines;
    lines.reserve(int(m_entries.size()));
    for (const LogEntry &entry : m_entries) {
        lines << QStringLiteral("%1  %2  %3  %4")
                     .arg(entry.when.toString(QStringLiteral("hh:mm:ss.zzz")),
                          messageTypeName(entry.type), entry.category, entry.message);
    }
    return lines.join(QLatin1Char('\n'));
}

LogFilterProxyModel::LogFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterKeyColumn(-1); // Match against every column.
}

void LogFilterProxyModel::setMinimumSeverity(int severity)
{
    if (m_minimumSeverity == severity)
        return;
    m_minimumSeverity = severity;

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateRowsFilter();
#endif
}

bool LogFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (index.data(LogModel::SeverityRole).toInt() < m_minimumSeverity)
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

} // namespace hidcenter
