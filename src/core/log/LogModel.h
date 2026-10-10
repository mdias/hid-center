// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QList>
#include <QSortFilterProxyModel>
#include <QString>

#include "hidcenter/core_export.h"

class QTimer;

namespace hidcenter {

/// One captured message.
struct HIDCENTER_CORE_EXPORT LogEntry
{
    QDateTime when;
    QtMsgType type = QtInfoMsg;
    QString category;
    QString message;
};

/// Severity order, 0 (debug) to 4 (fatal).
///
/// QtMsgType is *not* ordered by severity — QtInfoMsg was appended to the enum
/// after the fact and has the highest numeric value — so comparing the raw enum
/// would put info above critical. Always rank through this.
HIDCENTER_CORE_EXPORT int messageSeverity(QtMsgType type);

HIDCENTER_CORE_EXPORT QString messageTypeName(QtMsgType type);

/// The application's log, captured from Qt's logging framework.
///
/// A single instance is installed as the message handler early in main(), so
/// everything the app, the core and every device module logs is available to the
/// UI. This exists chiefly so a device module can report what it is doing — most
/// of all what a dry-run write *would* have sent — somewhere the user can see it
/// without running the application from a terminal.
class HIDCENTER_CORE_EXPORT LogModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        TimeColumn,
        LevelColumn,
        CategoryColumn,
        MessageColumn,
        ColumnCount,
    };

    /// Severity of the row, for filtering. See messageSeverity().
    static constexpr int SeverityRole = Qt::UserRole + 1;

    explicit LogModel(QObject *parent = nullptr);
    ~LogModel() override;

    /// The instance the message handler feeds, or nullptr before install().
    static LogModel *instance();

    /// Start capturing. Chains to the handler already installed, so terminal and
    /// journal output are unaffected. Safe to call once; later calls are ignored.
    static void install();
    static void uninstall();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    const LogEntry *entryAt(int row) const;

    /// Maximum entries retained; older ones are dropped as new ones arrive.
    int capacity() const { return m_capacity; }
    void setCapacity(int capacity);

    /// The whole log as text, for the Copy button.
    QString toPlainText() const;

public Q_SLOTS:
    /// Queue an entry. Called from the message handler, on any thread, through a
    /// queued connection — so the model itself is only ever touched on the GUI
    /// thread and needs no locking.
    void append(const hidcenter::LogEntry &entry);

    void clear();

    /// Publish anything queued immediately rather than waiting for the timer.
    void flush();

private:
    QList<LogEntry> m_entries;
    QList<LogEntry> m_pending;
    QTimer *m_flushTimer = nullptr;
    int m_capacity = 5000;
};

/// Filters a LogModel by minimum severity and a free-text substring.
class HIDCENTER_CORE_EXPORT LogFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit LogFilterProxyModel(QObject *parent = nullptr);

    void setMinimumSeverity(int severity);
    int minimumSeverity() const { return m_minimumSeverity; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    int m_minimumSeverity = 0;
};

} // namespace hidcenter

Q_DECLARE_METATYPE(hidcenter::LogEntry)
