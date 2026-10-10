// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/LogPanel.h"

#include "core/log/LogModel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace hidcenter {

LogPanel::LogPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void LogPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *toolbar = new QHBoxLayout;

    toolbar->addWidget(new QLabel(tr("Level:"), this));
    m_level = new QComboBox(this);
    m_level->setObjectName(QStringLiteral("logLevelFilter"));
    m_level->addItem(tr("All"), 0);
    m_level->addItem(tr("Info and above"), 1);
    m_level->addItem(tr("Warnings and above"), 2);
    m_level->addItem(tr("Errors only"), 3);
    toolbar->addWidget(m_level);

    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("logSearch"));
    m_search->setPlaceholderText(tr("Filter…"));
    m_search->setClearButtonEnabled(true);
    toolbar->addWidget(m_search, 1);

    m_autoScroll = new QCheckBox(tr("Auto-scroll"), this);
    m_autoScroll->setChecked(true);
    toolbar->addWidget(m_autoScroll);

    auto *copy = new QPushButton(tr("Copy"), this);
    copy->setToolTip(tr("Copy the whole log to the clipboard."));
    connect(copy, &QPushButton::clicked, this, &LogPanel::copyToClipboard);
    toolbar->addWidget(copy);

    auto *clear = new QPushButton(tr("Clear"), this);
    connect(clear, &QPushButton::clicked, this, [] {
        if (LogModel *model = LogModel::instance())
            model->clear();
    });
    toolbar->addWidget(clear);

    auto *popOut = new QPushButton(tr("Pop out"), this);
    popOut->setToolTip(tr("Open the log in its own window."));
    connect(popOut, &QPushButton::clicked, this, &LogPanel::popOutRequested);
    popOut->setVisible(false);
    m_popOut = popOut;
    toolbar->addWidget(popOut);

    layout->addLayout(toolbar);

    m_proxy = new LogFilterProxyModel(this);
    m_proxy->setSourceModel(LogModel::instance());

    m_view = new QTableView(this);
    m_view->setObjectName(QStringLiteral("logView"));
    m_view->setModel(m_proxy);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->setVisible(false);
    m_view->setWordWrap(false);
    // Uniform row heights let the view skip per-row measurement, which matters
    // once the log holds thousands of lines.
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->verticalHeader()->setDefaultSectionSize(QFontMetrics(font()).height() + 6);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setColumnWidth(LogModel::TimeColumn, 90);
    m_view->setColumnWidth(LogModel::LevelColumn, 70);
    m_view->setColumnWidth(LogModel::CategoryColumn, 110);
    layout->addWidget(m_view, 1);

    connect(m_level, &QComboBox::currentIndexChanged, this, [this] {
        m_proxy->setMinimumSeverity(m_level->currentData().toInt());
    });
    connect(m_search, &QLineEdit::textChanged, m_proxy,
            &QSortFilterProxyModel::setFilterFixedString);
    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, &LogPanel::onRowsInserted);
}

void LogPanel::setPopOutVisible(bool visible)
{
    if (m_popOut)
        m_popOut->setVisible(visible);
}

void LogPanel::onRowsInserted()
{
    if (m_autoScroll->isChecked())
        m_view->scrollToBottom();
}

void LogPanel::copyToClipboard()
{
    if (LogModel *model = LogModel::instance())
        QApplication::clipboard()->setText(model->toPlainText());
}

} // namespace hidcenter
