// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "StatusPage.h"

#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include "../FirmwareProbe.h"

namespace hidcenter::simsonn_pedals {
namespace {

/// The same red the Alpha Evo module's Status page uses for a failed condition,
/// and the log uses for a critical line.
const auto kAlarmStyle = QStringLiteral("color: #c03030;");

/// Wide enough to read beside a line of text, small enough not to shout.
constexpr int kIconSize = 16;

/// Shown where a fact would go when there is no fact to show. An em dash rather
/// than an empty cell, so the row reads as "nothing here" rather than as a label
/// that failed to fill in.
const auto kNoValue = QStringLiteral("—");

/// Gap between the columns of the interface table. Wider than the default, which
/// runs the driver name into the endpoint list.
constexpr int kInterfaceColumnSpacing = 18;

} // namespace

StatusPage::StatusPage(const QString &modelName, const UsbDeviceInfo &device,
                       const std::optional<UsbInterfaceInfo> &vendorInterface,
                       FirmwareProbe *probe, QWidget *parent)
    : QWidget(parent)
    , m_device(device)
    , m_vendorInterface(vendorInterface)
    , m_probe(probe)
{
    auto *layout = new QVBoxLayout(this);

    // --- The condition -----------------------------------------------------
    // One, not the two the wheelbase needs: there is no force-feedback driver to
    // check on a pedal set. Whether the configuration protocol answers is the
    // whole question here, and the other likely cause of it not answering — a
    // node that cannot be opened — is shown per interface further down.
    {
        auto *row = new QHBoxLayout;

        m_conditionIcon = new QLabel(this);
        m_conditionIcon->setObjectName(QStringLiteral("protocolIcon"));
        // Reserved whether or not a glyph is showing, so the sentence does not
        // step left when there is nothing to say about it.
        m_conditionIcon->setFixedWidth(kIconSize);
        row->addWidget(m_conditionIcon);

        m_conditionText = new QLabel(this);
        m_conditionText->setObjectName(QStringLiteral("protocolLabel"));
        m_conditionText->setWordWrap(true);
        row->addWidget(m_conditionText, 1);

        layout->addLayout(row);
    }

    layout->addSpacing(12);

    buildDevice(layout, modelName);
    buildInterfaces(layout);

    layout->addStretch(1);

    refreshFirmware();

    if (m_probe)
        connect(m_probe, &FirmwareProbe::stateChanged, this, &StatusPage::refreshFirmware);
}

void StatusPage::buildDevice(QVBoxLayout *layout, const QString &modelName)
{
    auto *box = new QGroupBox(tr("Device"), this);
    auto *form = new QFormLayout(box);

    // A label dropping above its value would put this box out of step with the
    // interface table below it, and it is never squeezed hard enough to need to.
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);

    const auto addRow = [box, form](const QString &label, const QString &objectName,
                                    const QString &text) {
        auto *value = new QLabel(box);
        value->setObjectName(objectName);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setText(text);
        form->addRow(label, value);
        return value;
    };

    addRow(tr("Model:"), QStringLiteral("modelValue"), modelName);

    // The one row here read from the device rather than from its descriptors, so
    // the only one that is empty until the probe has answered — and the only one
    // held, because it is the only one that changes.
    m_firmware = addRow(tr("Firmware:"), QStringLiteral("firmwareValue"), kNoValue);

    // Zero is not a VID, so "0000:0000" would be a fact this page invented. It
    // happens when there is no enumeration behind the page at all.
    addRow(tr("USB id:"), QStringLiteral("usbIdValue"),
           m_device.id.isValid() ? m_device.id.toString() : kNoValue);

    // Both are what the device says about itself in its string descriptors, and
    // either can be absent: not every device declares a serial, and an offline
    // preview has no descriptors at all.
    addRow(tr("Product:"), QStringLiteral("productValue"),
           m_device.product.isEmpty() ? kNoValue : m_device.product);
    addRow(tr("Serial:"), QStringLiteral("serialValue"),
           m_device.serial.isEmpty() ? kNoValue : m_device.serial);

    layout->addWidget(box);
}

void StatusPage::refreshFirmware()
{
    const FirmwareProbe::State state =
        m_probe ? m_probe->state() : FirmwareProbe::State::Idle;
    const QString error = m_probe ? m_probe->error() : QString();

    switch (state) {
    case FirmwareProbe::State::Ready:
        m_firmware->setText(m_probe->versionString());
        m_firmware->setToolTip(tr("Firmware version / sub-version: %1. The hex digits spell the "
                                  "version.")
                                   .arg(m_probe->rawVersion()));
        setCondition(Severity::Ok, tr("Configuration protocol: answering"));
        return;

    case FirmwareProbe::State::Waiting:
        m_firmware->setText(kNoValue);
        m_firmware->setToolTip(QString());
        setCondition(Severity::Unknown, tr("Configuration protocol: asking…"));
        return;

    case FirmwareProbe::State::Failed:
        m_firmware->setText(kNoValue);
        m_firmware->setToolTip(QString());
        setCondition(Severity::Alarm,
                     tr("Configuration protocol: no answer. %1").arg(error));
        return;

    case FirmwareProbe::State::Idle:
        m_firmware->setText(kNoValue);
        m_firmware->setToolTip(QString());

        // Nothing was asked, so nothing failed. error() says why it was not
        // asked — no device, or no interface to ask on — and saying so is the
        // difference between a blank row and an unexplained one.
        setCondition(Severity::Unknown,
                     error.isEmpty() ? tr("Configuration protocol: not checked.")
                                     : tr("Configuration protocol: not checked — %1").arg(error));
        return;
    }
}

void StatusPage::setSeverityIcon(QLabel *icon, Severity severity) const
{
    switch (severity) {
    case Severity::Ok:
        // The theme's own glyphs rather than characters of ours, so they follow
        // the desktop's icon set and stay legible on a dark palette.
        icon->setPixmap(
            style()->standardIcon(QStyle::SP_DialogApplyButton).pixmap(kIconSize, kIconSize));
        return;

    case Severity::Alarm:
        icon->setPixmap(
            style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(kIconSize, kIconSize));
        return;

    case Severity::Unknown:
        // No glyph at all. A question mark would suggest something is wrong, and
        // nothing is: there was simply nothing to look at.
        icon->clear();
        return;
    }
}

void StatusPage::setCondition(Severity severity, const QString &text)
{
    m_conditionText->setText(text);
    setSeverityIcon(m_conditionIcon, severity);

    switch (severity) {
    case Severity::Ok:
        m_conditionText->setEnabled(true);
        m_conditionText->setStyleSheet(QString());
        break;

    case Severity::Alarm:
        m_conditionText->setEnabled(true);
        m_conditionText->setStyleSheet(kAlarmStyle);
        break;

    case Severity::Unknown:
        m_conditionText->setEnabled(false);
        m_conditionText->setStyleSheet(QString());
        break;
    }
}

void StatusPage::buildInterfaces(QVBoxLayout *layout)
{
    auto *box = new QGroupBox(tr("Interfaces"), this);
    box->setObjectName(QStringLiteral("interfacesBox"));

    auto *grid = new QGridLayout(box);
    grid->setHorizontalSpacing(kInterfaceColumnSpacing);
    grid->setVerticalSpacing(2);

    if (m_device.interfaces.isEmpty()) {
        // Offline, or a device that went away between enumeration and here. Say
        // so rather than leaving an empty box, which reads as a bug.
        auto *empty = new QLabel(tr("No device to list."), box);
        empty->setEnabled(false);
        grid->addWidget(empty, 0, 0);
        layout->addWidget(box);
        return;
    }

    const auto header = [box, grid](int column, const QString &text) {
        auto *label = new QLabel(text, box);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        grid->addWidget(label, 0, column);
    };

    header(0, tr("Interface"));
    header(1, tr("Class"));
    header(2, tr("Driver"));
    header(3, tr("Access"));
    header(4, tr("Endpoints"));

    int row = 1;
    for (const UsbInterfaceInfo &iface : m_device.interfaces) {
        // Which one the configuration protocol is spoken on. Marked rather than
        // listed on its own, because the point of this box is that the interface
        // in use is one of several and the others are worth seeing beside it.
        const bool inUse = m_vendorInterface && m_vendorInterface->sysPath == iface.sysPath;

        const auto cell = [box, grid, row](int column, const QString &text) {
            auto *label = new QLabel(text, box);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            grid->addWidget(label, row, column);
            return label;
        };

        QLabel *name = cell(0, iface.displayName());
        if (inUse) {
            QFont font = name->font();
            font.setBold(true);
            name->setFont(font);
            name->setToolTip(tr("The configuration protocol uses this interface."));
        }

        cell(1, usbInterfaceClassName(iface.interfaceClass));

        // Two drivers can be bound below one interface — usbhid holds the
        // interface and a HID driver holds the node under it — and naming both is
        // what distinguishes "nothing took this" from "something did, and it is
        // not what reads the pedals".
        QString driver = iface.kernelDriver.isEmpty() ? tr("none") : iface.kernelDriver;
        if (!iface.driver.driver.isEmpty() && iface.driver.driver != iface.kernelDriver)
            driver += QStringLiteral(" / ") + iface.driver.driver;
        cell(2, driver);

        buildAccessCell(box, grid, row, 3, iface.access);

        QStringList endpoints;
        endpoints.reserve(iface.endpoints.size());
        for (const UsbEndpointInfo &endpoint : iface.endpoints)
            endpoints << endpoint.describe();
        cell(4, endpoints.isEmpty() ? tr("none declared") : endpoints.join(QStringLiteral(", ")));

        // Not a fault in itself, but it does explain why nothing here speaks
        // reports — and on this device it is why the firmware version above may
        // be empty, since the whole protocol is reports on one such node.
        if (!iface.isHid()) {
            auto *note = new QLabel(tr("No hidraw node: no HID reports."), box);
            note->setEnabled(false);
            grid->addWidget(note, ++row, 0, 1, 5);
        }

        ++row;
    }

    // The last column takes the slack, so the others stay against the left edge
    // instead of spreading across whatever width the window happens to be.
    grid->setColumnStretch(4, 1);

    layout->addWidget(box);
}

void StatusPage::buildAccessCell(QWidget *parent, QGridLayout *grid, int row, int column,
                                 const UsbAccessInfo &access)
{
    // Value first so the words line up down the column, glyph after it, and no
    // margins of the nested layout's own to make this row taller than its
    // neighbours.
    auto *cell = new QHBoxLayout;
    cell->setContentsMargins(0, 0, 0, 0);

    auto *text = new QLabel(parent);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cell->addWidget(text);

    auto *icon = new QLabel(parent);
    icon->setFixedWidth(kIconSize);
    cell->addWidget(icon);

    cell->addStretch(1);
    grid->addLayout(cell, row, column);

    const bool full = access.readable && access.writable;

    if (!access.known)
        text->setText(kNoValue);
    else if (full)
        text->setText(tr("read and write"));
    else if (access.readable)
        text->setText(tr("read only"));
    else if (access.writable)
        text->setText(tr("write only"));
    else
        text->setText(tr("none"));

    // A glyph only when something is wrong. Every interface on a working device
    // reads and writes, and a column of ticks would say nothing a quiet column
    // does not — while making the one warning among them harder to spot.
    const bool lacking = access.known && !full;
    setSeverityIcon(icon, lacking ? Severity::Alarm : Severity::Unknown);
    if (!lacking)
        return;

    // Every shortfall has the same fix, so each says so. Being told only that
    // access is wrong leaves nowhere to go.
    const QString what = access.readable
        ? tr("Read-only: nothing can be sent to this interface.")
        : access.writable
        ? tr("Write-only: nothing this interface sends can be seen.")
        : tr("Neither readable nor writable.");
    const QString tip = what + QLatin1Char(' ')
        + tr("Install udev/70-hid-center.rules and replug the device.");

    text->setToolTip(tip);
    icon->setToolTip(tip);
}

} // namespace hidcenter::simsonn_pedals
