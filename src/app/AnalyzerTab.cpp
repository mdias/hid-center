// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/AnalyzerTab.h"

#include "core/Logging.h"
#include "core/analyzer/UsbPacketSource.h"
#include "core/analyzer/PacketLogModel.h"
#include "core/usb/IUsbDevice.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace hidcenter {
namespace {

enum ReportColumn {
    ReportInterfaceColumn,
    ReportIdColumn,
    ReportTypeColumn,
    ReportBytesColumn,
    ReportFieldsColumn,
    ReportUsageColumn,
    ReportColumnCount,
};

QString hex16(quint16 value)
{
    return QStringLiteral("0x%1").arg(value, 4, 16, QLatin1Char('0'));
}

QString hex8(quint8 value)
{
    return QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0'));
}

/// Longest report the length box will accept. HID itself allows far more, but
/// nothing sane sends it, and an unbounded box invites a typo that allocates.
constexpr int kMaxSendLength = 4096;

/// What the length box holds until a descriptor or the user says otherwise.
/// Vendor protocols land on 64 far more often than on anything else.
constexpr int kDefaultSendLength = 64;

} // namespace

AnalyzerTab::AnalyzerTab(IUsbDevice *device, QWidget *parent)
    : QWidget(parent)
    , m_device(device)
{
    m_log = new PacketLogModel(this);
    m_proxy = new PacketFilterProxyModel(this);
    m_proxy->setSourceModel(m_log);

    m_source = new UsbPacketSource(device, this);
    connect(m_source, &UsbPacketSource::packetCaptured, m_log, &PacketLogModel::append);

    buildUi();
    populateReportsTable();
    populateFilterChoices();
    updateStatistics();

    connect(m_log, &PacketLogModel::statisticsChanged, this, &AnalyzerTab::updateStatistics);
}

void AnalyzerTab::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    // --- Reports ----------------------------------------------------------
    auto *splitter = new QSplitter(Qt::Vertical, this);

    auto *reportsBox = new QGroupBox(tr("Reports declared by this device"), splitter);
    auto *reportsLayout = new QVBoxLayout(reportsBox);
    m_reports = new QStandardItemModel(this);
    m_reports->setHorizontalHeaderLabels({tr("Interface"), tr("Report ID"), tr("Type"),
                                          tr("Bytes"), tr("Fields"), tr("Usage")});
    m_reportsView = new QTableView(reportsBox);
    m_reportsView->setObjectName(QStringLiteral("reportsView"));
    m_reportsView->setModel(m_reports);
    m_reportsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_reportsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_reportsView->verticalHeader()->setVisible(false);
    m_reportsView->horizontalHeader()->setStretchLastSection(true);
    reportsLayout->addWidget(m_reportsView);
    splitter->addWidget(reportsBox);

    // --- Traffic toolbar and log  -----------------------------------------
    auto *trafficBox = new QGroupBox(tr("Traffic"), splitter);
    auto *trafficLayout = new QVBoxLayout(trafficBox);

    { // toolbar
        auto *toolbar = new QHBoxLayout;

        m_listen = new QCheckBox(tr("Listen"), this);
        m_listen->setObjectName(QStringLiteral("listenToggle"));
        // Off by default: capture is a development tool, and a wheelbase streaming
        // input reports at a kilohertz should not be logged unless asked for.
        m_listen->setChecked(false);
        m_listen->setToolTip(tr("Start capturing traffic to and from this device."));
        connect(m_listen, &QCheckBox::toggled, this, &AnalyzerTab::setListening);
        if (m_device->isOffline()) {
            // Ticking this would open nothing, fail, and put a warning dialog in
            // front of the user for something entirely expected.
            m_listen->setEnabled(false);
            m_listen->setToolTip(
                tr("This device is not connected, so there is no traffic to capture."));
        }
        toolbar->addWidget(m_listen);

        m_clear = new QPushButton(tr("Clear"), this);
        connect(m_clear, &QPushButton::clicked, m_log, &PacketLogModel::clear);
        toolbar->addWidget(m_clear);

        toolbar->addSpacing(16);

        toolbar->addWidget(new QLabel(tr("Report:"), this));
        m_reportFilter = new QComboBox(this);
        connect(m_reportFilter, &QComboBox::currentIndexChanged, this, &AnalyzerTab::applyFilter);
        toolbar->addWidget(m_reportFilter);

        toolbar->addWidget(new QLabel(tr("Direction:"), this));
        m_directionFilter = new QComboBox(this);
        connect(m_directionFilter, &QComboBox::currentIndexChanged, this, &AnalyzerTab::applyFilter);
        toolbar->addWidget(m_directionFilter);

        toolbar->addWidget(new QLabel(tr("Interface:"), this));
        m_interfaceFilter = new QComboBox(this);
        connect(m_interfaceFilter, &QComboBox::currentIndexChanged, this, &AnalyzerTab::applyFilter);
        toolbar->addWidget(m_interfaceFilter);

        toolbar->addSpacing(16);

        m_autoScroll = new QCheckBox(tr("Auto-scroll"), this);
        m_autoScroll->setChecked(true);
        toolbar->addWidget(m_autoScroll);

        toolbar->addStretch(1);

        m_statistics = new QLabel(this);
        toolbar->addWidget(m_statistics);

        trafficLayout->addLayout(toolbar);
    } // ~toolbar

    m_logView = new QTableView(trafficBox);
    m_logView->setObjectName(QStringLiteral("packetLogView"));
    m_logView->setModel(m_proxy);
    m_logView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logView->verticalHeader()->setVisible(false);
    m_logView->horizontalHeader()->setStretchLastSection(true);
    // Uniform row heights let the view skip per-row measurement, which matters
    // once the log holds tens of thousands of packets.
    m_logView->setWordWrap(false);
    m_logView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_logView->verticalHeader()->setDefaultSectionSize(
        QFontMetrics(font()).height() + 6);
    trafficLayout->addWidget(m_logView);
    splitter->addWidget(trafficBox);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);

    // --- Send ------------------------------------------------------------
    // Outside the splitter, so it keeps its height while the log takes the room.
    m_sendBox = new QGroupBox(tr("Send report"), this);
    m_sendBox->setObjectName(QStringLiteral("sendBox"));
    buildSendArea(m_sendBox);
    layout->addWidget(m_sendBox);

    auto *note = new QLabel(
        tr("Shows every report this application exchanges with the device, plus all input "
           "reports the device sends. Output reports written by other programs are not "
           "visible to hidraw."),
        this);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this] {
        if (m_autoScroll->isChecked())
            m_logView->scrollToBottom();
    });
}

void AnalyzerTab::buildSendArea(QGroupBox *box)
{
    auto *sendLayout = new QVBoxLayout(box);
    auto *row = new QHBoxLayout;

    // One node, not "All": a report goes somewhere specific. This is why the
    // send area has its own interface combo rather than borrowing the filter's.
    m_sendInterface = new QComboBox(box);
    m_sendInterface->setObjectName(QStringLiteral("sendInterface"));
    if (m_device) {
        // HID interfaces only. What this panel sends is a *report*, and an
        // interface with no node has no report layer to send one through — its
        // endpoints take raw transfers, which is a different thing and not what
        // the length and report-type controls beside this describe.
        for (const UsbInterfaceInfo &iface : m_device->info().interfaces) {
            if (iface.isHid())
                m_sendInterface->addItem(iface.displayName(), iface.sysPath);
        }
    }
    connect(m_sendInterface, &QComboBox::currentIndexChanged, this, &AnalyzerTab::updateSendState);
    row->addWidget(m_sendInterface);

    m_sendType = new QComboBox(box);
    m_sendType->setObjectName(QStringLiteral("sendType"));
    m_sendType->addItem(reportTypeName(ReportType::Output), int(ReportType::Output));
    m_sendType->addItem(reportTypeName(ReportType::Feature), int(ReportType::Feature));
    m_sendType->setToolTip(tr("Output reports are written to the device node; feature reports "
                              "go through an ioctl. Which one a report is decides how long the "
                              "descriptor says it should be."));
    // Defaulted once. A device that declares no output reports is almost
    // certainly one whose protocol lives on feature reports, so start there —
    // but never move it again, because after this the choice is the user's.
    if (const UsbInterfaceInfo *iface = sendInterface()) {
        if (!iface->hid.descriptor.isEmpty()) {
            bool hasOutput = false;
            for (const ReportInfo &report : iface->hid.descriptor.reports())
                hasOutput = hasOutput || report.type == ReportType::Output;
            if (!hasOutput)
                m_sendType->setCurrentIndex(m_sendType->findData(int(ReportType::Feature)));
        }
    }
    connect(m_sendType, &QComboBox::currentIndexChanged, this, &AnalyzerTab::updateSendState);
    row->addWidget(m_sendType);

    m_sendData = new QLineEdit(box);
    m_sendData->setObjectName(QStringLiteral("sendData"));
    // The same font the log gives the Data column, so a sequence copied out of
    // the log lines up with what is typed under it.
    m_sendData->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_sendData->setClearButtonEnabled(true);
    m_sendData->setPlaceholderText(tr("80 01 3f …"));
    connect(m_sendData, &QLineEdit::textChanged, this, &AnalyzerTab::updateSendState);
    connect(m_sendData, &QLineEdit::returnPressed, this, &AnalyzerTab::sendReport);
    row->addWidget(m_sendData, 1);

    row->addWidget(new QLabel(tr("Length:"), box));
    m_sendLength = new QSpinBox(box);
    m_sendLength->setObjectName(QStringLiteral("sendLength"));
    m_sendLength->setRange(1, kMaxSendLength);
    m_sendLength->setValue(kDefaultSendLength);
    m_sendLength->setSuffix(tr(" bytes"));
    connect(m_sendLength, &QSpinBox::valueChanged, this, &AnalyzerTab::updateSendState);
    row->addWidget(m_sendLength);

    m_send = new QPushButton(tr("Send"), box);
    m_send->setObjectName(QStringLiteral("sendButton"));
    connect(m_send, &QPushButton::clicked, this, &AnalyzerTab::sendReport);
    row->addWidget(m_send);

    sendLayout->addLayout(row);

    m_sendStatus = new QLabel(box);
    m_sendStatus->setObjectName(QStringLiteral("sendStatus"));
    m_sendStatus->setWordWrap(true);
    sendLayout->addWidget(m_sendStatus);

    if (m_device && m_device->isOffline()) {
        box->setEnabled(false);
        box->setToolTip(tr("This device is not connected, so there is nothing to send to."));
    }

    updateSendState();
}

const UsbInterfaceInfo *AnalyzerTab::sendInterface() const
{
    if (!m_device || !m_sendInterface)
        return nullptr;

    const QString sysPath = m_sendInterface->currentData().toString();
    for (const UsbInterfaceInfo &iface : m_device->info().interfaces) {
        if (iface.sysPath == sysPath)
            return &iface;
    }
    return nullptr;
}

void AnalyzerTab::updateSendState()
{
    const auto say = [this](const QString &text, bool isError) {
        m_sendStatus->setText(text);
        m_sendStatus->setStyleSheet(isError ? QStringLiteral("color: #c03030;") : QString());
    };

    QString error;
    const QByteArray bytes = parseHexBytes(m_sendData->text(), &error);
    if (!error.isEmpty()) {
        m_send->setEnabled(false);
        say(error, true);
        return;
    }

    // How long the report has to be. Without declared report IDs byte 0 is
    // payload rather than an ID, which is the same branch UsbPacketSource
    // takes when it labels a captured packet.
    const auto type = static_cast<ReportType>(m_sendType->currentData().toInt());
    const ReportInfo *declared = nullptr;
    bool hasIds = false;
    quint8 id = 0;
    if (const UsbInterfaceInfo *iface = sendInterface()) {
        const ReportDescriptor &descriptor = iface->hid.descriptor;
        hasIds = descriptor.usesReportIds();
        if (hasIds && !bytes.isEmpty())
            id = quint8(bytes.at(0));
        declared = descriptor.find(id, type);
        if (declared) {
            const QSignalBlocker blocker(m_sendLength);
            m_sendLength->setValue(descriptor.transferLength(*declared));
        }
    }

    // Read-only rather than quietly overwritten: a box that looks editable while
    // ignoring what is typed into it is worse than one that plainly is not.
    m_sendLength->setReadOnly(declared != nullptr);
    m_sendLength->setButtonSymbols(declared ? QAbstractSpinBox::NoButtons
                                            : QAbstractSpinBox::UpDownArrows);

    const int length = m_sendLength->value();

    if (bytes.isEmpty()) {
        m_send->setEnabled(false);
        say(tr("Type the bytes to send, in the same hex the Data column shows."), false);
        return;
    }

    // Phrased to read at any count rather than with Qt's %n plural forms: there
    // are no translations in this project for those forms to come from, so %n
    // would print a literal "byte(s)".
    if (bytes.size() > length) {
        m_send->setEnabled(false);
        if (!declared) {
            say(tr("Too long: %1 bytes typed, and the length is set to %2 bytes.")
                    .arg(bytes.size())
                    .arg(length),
                true);
        } else if (hasIds) {
            say(tr("Too long: %1 bytes typed, and report %2 is %3 bytes.")
                    .arg(bytes.size())
                    .arg(hex8(id))
                    .arg(length),
                true);
        } else {
            say(tr("Too long: %1 bytes typed, and the descriptor says %2 bytes.")
                    .arg(bytes.size())
                    .arg(length),
                true);
        }
        return;
    }

    m_send->setEnabled(true);
    QString where;
    if (!declared)
        where = tr("The descriptor declares no such report, so the length is yours to set.");
    else if (hasIds)
        where = tr("That is what the descriptor declares for report %1.").arg(hex8(id));
    else
        where = tr("That is what the descriptor declares.");

    say(tr("Will send %1 bytes: %2 typed, %3 of zero padding. %4")
            .arg(length)
            .arg(bytes.size())
            .arg(length - bytes.size())
            .arg(where),
        false);
}

void AnalyzerTab::sendReport()
{
    if (!m_send->isEnabled())
        return;

    const UsbInterfaceInfo *iface = sendInterface();
    if (!iface) {
        m_sendStatus->setText(tr("No interface to send to."));
        m_sendStatus->setStyleSheet(QStringLiteral("color: #c03030;"));
        return;
    }

    const QByteArray bytes = parseHexBytes(m_sendData->text());
    // leftJustified rather than resize(), whose new bytes are uninitialised, and
    // it never truncates unless asked to — so a sequence longer than the report
    // could not be quietly cut down even if the guard above went missing.
    const QByteArray report = bytes.leftJustified(m_sendLength->value(), '\0');

    IUsbDeviceHidInterface *device = m_device->openHidInterface(*iface);
    if (!device) {
        m_sendStatus->setText(tr("Could not open %1: %2")
                                  .arg(iface->displayName(), m_device->lastError()));
        m_sendStatus->setStyleSheet(QStringLiteral("color: #c03030;"));
        return;
    }

    // On the GUI thread. IUsbDeviceHidInterface warns that a module with a worker should not
    // stall the UI on a transfer, but this is one ioctl on an explicit button
    // press: a thread would buy nothing, and a device that hangs would hang the
    // window either way.
    const auto type = static_cast<ReportType>(m_sendType->currentData().toInt());
    const bool ok = (type == ReportType::Output) ? device->write(report)
                                                 : device->setFeatureReport(report);

    if (!ok) {
        m_sendStatus->setText(tr("Send failed: %1").arg(device->lastError()));
        m_sendStatus->setStyleSheet(QStringLiteral("color: #c03030;"));
        return;
    }

    // Capture is left exactly as the user set it. With Listen on this report is
    // already in the log, because the source taps every handle the context owns;
    // with it off, this line is the only account of it. Ticking Listen from here
    // would open every interface and can flood the log at kilohertz, which is
    // too large a side effect for pressing Send.
    m_sendStatus->setStyleSheet(QString());
    m_sendStatus->setText(m_listen->isChecked()
                              ? tr("Sent %1 bytes.").arg(report.size())
                              : tr("Sent %1 bytes. Tick Listen to see traffic in the log.")
                                    .arg(report.size()));
}

void AnalyzerTab::populateReportsTable()
{
    m_reports->removeRows(0, m_reports->rowCount());
    if (!m_device)
        return;

    // Descriptors come from enumeration, which reads them from sysfs, so this
    // table is populated even when the device node itself cannot be opened.
    for (const UsbInterfaceInfo &iface : m_device->info().interfaces) {
        const ReportDescriptor &descriptor = iface.hid.descriptor;

        if (descriptor.isEmpty()) {
            auto *name = new QStandardItem(iface.displayName());
            auto *note = new QStandardItem(tr("no readable report descriptor"));
            m_reports->appendRow({name, note, new QStandardItem, new QStandardItem,
                                  new QStandardItem, new QStandardItem});
            continue;
        }

        for (const ReportInfo &report : descriptor.reports()) {
            QList<QStandardItem *> row;
            row << new QStandardItem(iface.displayName());
            row << new QStandardItem(descriptor.usesReportIds() ? hex8(report.id)
                                                                : tr("(none)"));
            row << new QStandardItem(reportTypeName(report.type));
            row << new QStandardItem(QString::number(descriptor.transferLength(report)));
            row << new QStandardItem(QString::number(report.fields.size()));
            row << new QStandardItem(QStringLiteral("%1 / %2")
                                         .arg(hex16(iface.hid.usagePage), hex16(iface.hid.usage)));

            row[ReportBytesColumn]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row[ReportFieldsColumn]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_reports->appendRow(row);
        }
    }

    m_reportsView->resizeColumnsToContents();
}

void AnalyzerTab::populateFilterChoices()
{
    const QSignalBlocker blockReport(m_reportFilter);
    const QSignalBlocker blockDirection(m_directionFilter);
    const QSignalBlocker blockInterface(m_interfaceFilter);

    m_reportFilter->clear();
    m_reportFilter->addItem(tr("All"), QVariant());

    // Offer the IDs the device actually declares. Filtering by a report ID that
    // no descriptor mentions is a good way to stare at an empty log.
    QList<quint8> ids;
    if (m_device) {
        for (const UsbInterfaceInfo &iface : m_device->info().interfaces) {
            if (!iface.hid.descriptor.usesReportIds())
                continue;
            for (const ReportInfo &report : iface.hid.descriptor.reports()) {
                if (!ids.contains(report.id))
                    ids.append(report.id);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    for (quint8 id : ids) {
        m_reportFilter->addItem(hex8(id), QVariant::fromValue(uint(id)));
    }

    m_directionFilter->clear();
    m_directionFilter->addItem(tr("All"), QVariant());
    for (const TrafficDirection direction :
         {TrafficDirection::In, TrafficDirection::Out, TrafficDirection::FeatureGet,
          TrafficDirection::FeatureSet}) {
        m_directionFilter->addItem(trafficDirectionName(direction),
                                   QVariant::fromValue(int(direction)));
    }

    m_interfaceFilter->clear();
    m_interfaceFilter->addItem(tr("All"), QVariant());
    if (m_device) {
        for (const UsbInterfaceInfo &iface : m_device->info().interfaces)
            m_interfaceFilter->addItem(iface.displayName(), iface.sysPath);
    }
}

void AnalyzerTab::applyFilter()
{
    PacketFilter filter;

    const QVariant reportId = m_reportFilter->currentData();
    if (reportId.isValid())
        filter.reportId = quint8(reportId.toUInt());

    const QVariant direction = m_directionFilter->currentData();
    if (direction.isValid())
        filter.direction = static_cast<TrafficDirection>(direction.toInt());

    filter.interfaceId = m_interfaceFilter->currentData().toString();

    m_proxy->setFilter(filter);
}

void AnalyzerTab::setListening(bool listening)
{
    if (listening) {
        if (m_source->start())
            return;

        // Nearly always a permissions problem on /dev/hidraw*, so say so
        // plainly rather than leaving a checkbox that silently does nothing.
        const QSignalBlocker blocker(m_listen);
        m_listen->setChecked(false);
        QMessageBox::warning(this, tr("Cannot capture"),
                             tr("Could not start capturing on this device.\n\n%1")
                                 .arg(m_source->lastError()));
        return;
    }

    m_source->stop();
    m_log->flush();
}

void AnalyzerTab::updateStatistics()
{
    const quint64 dropped = m_log->totalDropped();
    QString text = tr("%1 packets").arg(m_log->totalReceived());
    if (dropped > 0)
        text += tr(" — %1 dropped (buffer holds %2)").arg(dropped).arg(m_log->capacity());
    m_statistics->setText(text);
}

} // namespace hidcenter
