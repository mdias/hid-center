// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "core/analyzer/PacketFilter.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStandardItemModel;
class QTableView;

namespace hidcenter {

class IUsbDevice;
class UsbPacketSource;
class PacketFilterProxyModel;
class PacketLogModel;

/// The USB protocol analyzer, appended as the last tab of every device.
///
/// This is generic application code rather than something device modules
/// provide: it needs no knowledge of a device to be useful, and a brand-new
/// module gets a working analyzer for free — which is exactly when it is needed
/// most.
///
/// What it can and cannot see: everything this application exchanges with the
/// device, plus every input report the device emits (the kernel broadcasts those
/// to all readers). It cannot see output reports written by *other* processes;
/// that needs a usbmon-based IPacketSource, which slots in without changing
/// anything here.
///
/// It also sends. The area below the log takes a report typed in the same hex
/// the log prints, pads it to the length the descriptor declares, and writes it
/// as an output or feature report — which is what turns the tab from a window on
/// a protocol into a way to work one out.
class AnalyzerTab : public QWidget
{
    Q_OBJECT

public:
    explicit AnalyzerTab(IUsbDevice *device, QWidget *parent = nullptr);

private Q_SLOTS:
    void setListening(bool listening);
    void applyFilter();
    void updateStatistics();

    /// Re-read the typed bytes, work out how long the report must be, and say
    /// whether it can be sent. Runs on every keystroke.
    void updateSendState();
    void sendReport();

private:
    void buildUi();
    void buildSendArea(QGroupBox *box);
    void populateReportsTable();
    void populateFilterChoices();

    /// The interface the send area is pointed at, or nullptr when there is none.
    const UsbInterfaceInfo *sendInterface() const;

    IUsbDevice *m_device = nullptr;
    UsbPacketSource *m_source = nullptr;
    PacketLogModel *m_log = nullptr;
    PacketFilterProxyModel *m_proxy = nullptr;
    QStandardItemModel *m_reports = nullptr;

    QCheckBox *m_listen = nullptr;
    QCheckBox *m_autoScroll = nullptr;
    QPushButton *m_clear = nullptr;
    QComboBox *m_reportFilter = nullptr;
    QComboBox *m_directionFilter = nullptr;
    QComboBox *m_interfaceFilter = nullptr;
    QLabel *m_statistics = nullptr;
    QTableView *m_reportsView = nullptr;
    QTableView *m_logView = nullptr;

    QGroupBox *m_sendBox = nullptr;
    QComboBox *m_sendInterface = nullptr;
    QComboBox *m_sendType = nullptr;
    QLineEdit *m_sendData = nullptr;
    QSpinBox *m_sendLength = nullptr;
    QPushButton *m_send = nullptr;
    QLabel *m_sendStatus = nullptr;
};

} // namespace hidcenter
