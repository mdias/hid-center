// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableView;

namespace hidcenter {

class LogFilterProxyModel;

/// A view of the application log.
///
/// Several of these can exist at once — the Home page keeps one and the pop-out
/// window another — because they are only views onto the single LogModel. Each
/// carries its own filter, so the pop-out can show warnings only while the Home
/// panel shows everything.
class LogPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);

    /// Show the "Pop out" button. Off by default, so the popped-out window does
    /// not offer to pop itself out again.
    void setPopOutVisible(bool visible);

Q_SIGNALS:
    void popOutRequested();

private Q_SLOTS:
    void onRowsInserted();
    void copyToClipboard();

private:
    void buildUi();

    LogFilterProxyModel *m_proxy = nullptr;
    QTableView *m_view = nullptr;
    QComboBox *m_level = nullptr;
    QLineEdit *m_search = nullptr;
    QCheckBox *m_autoScroll = nullptr;
    QWidget *m_popOut = nullptr;
};

} // namespace hidcenter
