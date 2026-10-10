// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "StatusPage.h"

#include <QCheckBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "TraceGraph.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

/// The same red as the status line at the foot of the settings page, and as a
/// critical line in the log.
const auto kAlarmStyle = QStringLiteral("color: #c03030;");

/// Wide enough to read beside a line of text, small enough not to shout.
constexpr int kIconSize = 16;

/// Shown where a fact would go when there is no fact to show. An em dash rather
/// than an empty cell, so the row reads as "nothing here" rather than as a
/// label that failed to fill in.
const auto kNoValue = QStringLiteral("—");

/// Floor for the trace, below which the graph is too short to read a signal off
/// and only its grid is really visible. It expands past this to fill the page.
constexpr int kTraceMinimumHeight = 120;

/// The narrowest the gap between the two columns of readings is allowed to get.
/// Wider than the layout's own spacing, which is tuned for the distance between
/// a label and its value and is far too close to read as the start of a new
/// column.
constexpr int kFactColumnGapMinimum = 24;

/// And the widest. The gap grows with the window so the columns sit apart rather
/// than huddled at the left, but only this far: measured on a 1600 px window, an
/// unbounded gap leaves some 660 px of empty box between a value and the label
/// of the row beside it, and two columns that far apart stop reading as rows at
/// all. Beyond this the surplus goes to the trailing edge, where empty space
/// costs nothing.
constexpr int kFactColumnGapMaximum = 220;

/// A byte as `0x1f`: an identifier or a code rather than a quantity, so it wants
/// reading against the protocol notes rather than compared or added up.
///
/// Deliberately not through QLocale, which is right for the counts and
/// temperatures beside these and would put digit grouping into a hex code.
QString toHexByte(quint8 value)
{
    return QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0'));
}

/// How often the rate and span readouts are refreshed. They follow a *measured*
/// rate, so they change on their own rather than only when something is clicked;
/// twice a second is as fast as a number worth reading should move.
constexpr int kTraceReadoutHz = 2;

/// Gap between the columns of the interface table. Wider than the default, which
/// runs the driver name into the endpoint list.
constexpr int kInterfaceColumnSpacing = 18;

} // namespace

StatusPage::StatusPage(AlphaEvoSettings *settings, AlphaEvoForceTrace *forceTrace,
                       const QString &probeError, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_forceTrace(forceTrace)
{
    auto *layout = new QVBoxLayout(this);

    // --- Conditions --------------------------------------------------------
    // First, and in this order: a base with no driver bound is limp however well
    // it is configured. The protocol row is last because the row above it is one
    // of its likely causes. The other, a node that cannot be opened, is shown per
    // interface in the table further down.
    m_driver = addConditionRow(layout, QStringLiteral("ffDriver"));
    m_protocol = addConditionRow(layout, QStringLiteral("protocol"));

    updateDriverRow();
    updateProtocolRow(probeError);

    layout->addSpacing(12);

    // --- Facts -------------------------------------------------------------
    // Two columns rather than one tall stack. There are ten readings here and
    // width going spare, and the height they were taking is height the force
    // trace below them would rather have — it is the one thing on this page that
    // grows.
    //
    // Read down the left, then down the right: the left column is what this base
    // *is* and how it is doing, the right is every number with a unit on it. Two
    // independent forms are the shape of that read — each sizes its own label
    // column against its own labels, which is why the values in one do not start
    // where the values in the other do, and should not.
    auto *box = new QGroupBox(tr("Device"), this);
    auto *columns = new QHBoxLayout(box);

    const auto makeColumn = [] {
        auto *form = new QFormLayout;

        // Explicit: a label dropping above its value in one column and not in the
        // other would put the two out of step by a row, and neither column is
        // ever squeezed hard enough to need it — they take the width they ask
        // for and the surplus goes elsewhere.
        form->setRowWrapPolicy(QFormLayout::DontWrapRows);
        return form;
    };

    auto *identity = makeColumn();
    auto *readings = makeColumn();

    // Each column takes the width its own content asks for, and the space left
    // over goes first to the gap between them and then, once that has had its
    // fill, to the trailing edge. Giving the columns themselves a share instead
    // ties the gap to the window width, which reads well up to about a thousand
    // pixels and badly after that — see kFactColumnGapMaximum.
    auto *gutter = new QWidget(box);
    gutter->setObjectName(QStringLiteral("factColumnGap"));
    gutter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    gutter->setMinimumWidth(kFactColumnGapMinimum);
    gutter->setMaximumWidth(kFactColumnGapMaximum);

    // Both take a share of the surplus, so the gap opens up as the window widens
    // until it reaches its maximum and the trailing edge inherits the rest. The
    // stretch factor is what does that: an expanding size policy alone loses to
    // any item that has one, and the gap would never leave its minimum.
    columns->addLayout(identity);
    columns->addWidget(gutter, 1);
    columns->addLayout(readings);
    columns->addStretch(1);

    const auto addRow = [box](QFormLayout *form, const QString &label,
                              const QString &objectName) {
        auto *value = new QLabel(box);
        value->setObjectName(objectName);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, value);
        return value;
    };

    // Left. The first two are true whether or not the base is running; the rest
    // are what it is doing right now, and change on nearly every poll.
    m_firmware = addRow(identity, tr("Firmware:"), QStringLiteral("firmwareValue"));
    m_wheelId = addRow(identity, tr("Wheel id:"), QStringLiteral("wheelIdValue"));
    m_state = addRow(identity, tr("State:"), QStringLiteral("baseStateValue"));

    // The error code is the one live reading that is a *verdict* rather than a
    // measurement, so it carries a glyph like the three conditions at the top of
    // the page: a tick when the base reports nothing wrong, a warning when it
    // does. The number stays, because which fault it is still matters.
    {
        auto *errorRow = new QHBoxLayout;

        // A nested layout keeps the style's own margins unless told otherwise,
        // which would indent this row and make it taller than its neighbours.
        // The same call TelemetryPage makes on its nested row, for that reason.
        errorRow->setContentsMargins(0, 0, 0, 0);

        // Value first, glyph after it. The condition rows lead with their icons,
        // but this one sits among four plain readings, and an icon in front would
        // indent its number away from theirs — one ragged row in five reads as a
        // mistake rather than as emphasis.
        m_error = new QLabel(box);
        m_error->setObjectName(QStringLiteral("baseErrorValue"));
        m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
        errorRow->addWidget(m_error);

        m_errorIcon = new QLabel(box);
        m_errorIcon->setObjectName(QStringLiteral("baseErrorIcon"));
        m_errorIcon->setFixedWidth(kIconSize);
        errorRow->addWidget(m_errorIcon);

        // Takes the slack, so the pair stays left against the other rows rather
        // than the icon drifting out to the far edge of the form.
        errorRow->addStretch(1);

        identity->addRow(tr("Error:"), errorRow);
    }

    // Right: everything carrying a unit.
    m_motorTemp = addRow(readings, tr("Motor temperature:"), QStringLiteral("motorTempValue"));
    m_mosfetTemp = addRow(readings, tr("MOSFET temperature:"), QStringLiteral("mosfetTempValue"));
    m_maxTorque = addRow(readings, tr("Max torque:"), QStringLiteral("maxTorqueValue"));
    m_busVoltage = addRow(readings, tr("Bus voltage:"), QStringLiteral("busVoltageValue"));

    // Sits among the measurements because that is what it is, but it is the one
    // row here read from the settings report rather than from 0x23, so it is
    // filled in by refreshFacts() rather than by its neighbours' refresh.
    m_powerSupplyVoltage =
        addRow(readings, tr("Power supply voltage:"), QStringLiteral("powerSupplyVoltageValue"));

    layout->addWidget(box);

    buildInterfaces(layout);

    // --- The force trace ----------------------------------------------------
    // Last, and where the page's trailing stretch used to be, so it takes the
    // space nothing else wanted. It is also the only thing here that is a
    // measurement rather than a fact, which is why it is below the facts and not
    // among them.
    buildForceTrace(layout);

    refreshFacts();
    refreshLiveFacts();

    // The facts follow the settings report, which is where the firmware and the
    // supply voltage are read from. The live rows change constantly and follow a
    // signal of their own, so a warming motor does not wake everything else.
    //
    // The facts follow *both*, though, because losing the base is announced on
    // the live signal alone: without this, a firmware and a supply voltage that
    // nothing is confirming any more would sit on the page reading as current.
    if (m_settings) {
        connect(m_settings, &AlphaEvoSettings::statusRefreshed, this, &StatusPage::refreshFacts);
        connect(m_settings, &AlphaEvoSettings::baseStatusRefreshed, this,
                &StatusPage::refreshLiveFacts);
        connect(m_settings, &AlphaEvoSettings::baseStatusRefreshed, this,
                &StatusPage::refreshFacts);
    }
}

StatusPage::StatusRow StatusPage::addConditionRow(QVBoxLayout *layout, const QString &objectName)
{
    auto *row = new QHBoxLayout;
    StatusRow result;

    result.icon = new QLabel(this);
    result.icon->setObjectName(objectName + QStringLiteral("Icon"));
    // Reserved whether or not a glyph is showing, so the sentences stay aligned
    // instead of stepping left when one of them has nothing to say.
    result.icon->setFixedWidth(kIconSize);
    row->addWidget(result.icon);

    result.text = new QLabel(this);
    result.text->setObjectName(objectName + QStringLiteral("Label"));
    result.text->setWordWrap(true);
    row->addWidget(result.text, 1);

    layout->addLayout(row);
    return result;
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

void StatusPage::setRow(const StatusRow &row, Severity severity, const QString &text)
{
    row.text->setText(text);
    setSeverityIcon(row.icon, severity);

    switch (severity) {
    case Severity::Ok:
        row.text->setEnabled(true);
        row.text->setStyleSheet(QString());
        break;

    case Severity::Alarm:
        row.text->setEnabled(true);
        row.text->setStyleSheet(kAlarmStyle);
        break;

    case Severity::Unknown:
        row.text->setEnabled(false);
        row.text->setStyleSheet(QString());
        break;
    }
}

void StatusPage::updateDriverRow()
{
    const HidDriverInfo driver = m_settings ? m_settings->forceFeedbackDriver() : HidDriverInfo{};

    // Nothing to look at — an offline preview, or a platform without this in
    // sysfs. Saying "none" here would blame the user for a missing driver on a
    // wheelbase that is not even plugged in.
    if (!driver.known) {
        setRow(m_driver, Severity::Unknown,
               tr("Force feedback driver: not checked — no wheelbase is connected."));
        return;
    }

    if (driver.forceFeedback) {
        setRow(m_driver, Severity::Ok, tr("Force feedback driver: %1").arg(driver.driver));
        return;
    }

    if (driver.driver.isEmpty()) {
        setRow(m_driver, Severity::Alarm,
               tr("Force feedback driver: none, so there is no force feedback."));
    } else {
        // Naming what *is* bound is the difference between a dead end and
        // something the user can act on.
        setRow(m_driver, Severity::Alarm,
               tr("Force feedback driver: none. %1 is bound but provides no force "
                  "feedback.")
                   .arg(driver.driver));
    }
}

void StatusPage::updateProtocolRow(const QString &probeError)
{
    if (!m_settings || (!m_settings->isReady() && probeError.isEmpty())) {
        setRow(m_protocol, Severity::Unknown, tr("Configuration protocol: not checked."));
        return;
    }

    if (m_settings->isOffline()) {
        // Not a failure: there is deliberately nothing here to answer.
        setRow(m_protocol, Severity::Unknown,
               tr("Configuration protocol: not connected."));
        return;
    }

    if (!probeError.isEmpty()) {
        setRow(m_protocol, Severity::Alarm,
               tr("Configuration protocol: no answer, so settings cannot be shown or "
                  "changed. %1")
                   .arg(probeError));
        return;
    }

    setRow(m_protocol, Severity::Ok, tr("Configuration protocol: answering"));
}

void StatusPage::refreshFacts()
{
    const bool live = m_settings && m_settings->isReady() && !m_settings->isOffline();

    // Guarded the same way infoFields() guards them: a firmware of 0 read off a
    // zeroed status report is not a fact about anything.
    if (live) {
        m_firmware->setText(QLocale().toString(m_settings->firmwareVersion()));
        m_firmware->setToolTip(tr("Raw field: %1").arg(m_settings->firmwareRaw()));
    } else {
        m_firmware->setText(kNoValue);
        m_firmware->setToolTip(QString());
    }

    // Filled in here rather than beside the other readings it sits with, because
    // it is read from the settings report and not from 0x23. That report is
    // proven and fetched on every poll, so gating this on hasBaseStatus() would
    // hide a voltage that *was* read behind one this base may never answer.
    m_powerSupplyVoltage->setText(
        live ? tr("%1 V").arg(QLocale().toString(int(m_settings->status().powerSupplyVoltage)))
             : kNoValue);

}

QString StatusPage::stateName(quint8 state) const
{
    switch (state) {
    case 1: return tr("Aligning");
    case 2: return tr("Running");
    case 3: return tr("Stopped");
    case 4: return tr("Fault");
    case 5: return tr("Tuning");
    case 6: return tr("Self-test");
    case 7: return tr("Zeroing");
    default:
        // Not an error worth flagging: the numbering has never been checked, so an unexpected value is
        // more likely to mean the table is incomplete than that the base is
        // confused. Show what arrived and let the reader judge.
        return QLocale().toString(int(state));
    }
}

void StatusPage::refreshLiveFacts()
{
    // hasBaseStatus() is the whole question: report 0x23 is decoded but unproven,
    // and a base that ignores it leaves every row here empty while the rest of
    // the application carries on exactly as before.
    const bool live = m_settings && m_settings->isReady() && !m_settings->isOffline()
        && m_settings->hasBaseStatus();

    if (!live) {
        // Every row 0x23 feeds, the wheel id included: a value left behind by the
        // last report that arrived reads exactly like one that is still being
        // confirmed. The supply voltage is not in this list because it does not
        // come from 0x23 — see refreshFacts().
        for (QLabel *value : {m_state, m_error, m_motorTemp, m_mosfetTemp, m_maxTorque,
                              m_wheelId, m_busVoltage}) {
            value->setText(kNoValue);
        }

        // No verdict either: a tick would claim the base is healthy, and a
        // warning would claim it is not, when the truth is that it has not said.
        setSeverityIcon(m_errorIcon, Severity::Unknown);
        m_error->setStyleSheet(QString());
        return;
    }

    const BaseStatusReport &base = m_settings->baseStatus();
    const QLocale locale;

    m_state->setText(stateName(base.state));

    // The one field here that can be *wrong* rather than merely informative, so
    // it gets the glyph and the red the conditions above use when unhappy.
    const Severity errorSeverity = base.error == 0 ? Severity::Ok : Severity::Alarm;
    setSeverityIcon(m_errorIcon, errorSeverity);
    m_error->setText(toHexByte(base.error));
    m_error->setStyleSheet(errorSeverity == Severity::Alarm ? kAlarmStyle : QString());

    m_motorTemp->setText(tr("%1 °C").arg(locale.toString(int(base.motorTemp))));
    m_mosfetTemp->setText(tr("%1 °C").arg(locale.toString(int(base.mosfetTemp))));
    m_maxTorque->setText(tr("%1 Nm").arg(locale.toString(int(base.maxTorque))));
    m_wheelId->setText(toHexByte(base.wheelId));
    m_busVoltage->setText(tr("%1 V").arg(locale.toString(float(base.busVoltage) / 100.0f)));
}

// --- The force trace --------------------------------------------------------

void StatusPage::buildInterfaces(QVBoxLayout *layout)
{
    auto *box = new QGroupBox(tr("Interfaces"), this);
    box->setObjectName(QStringLiteral("interfacesBox"));

    auto *grid = new QGridLayout(box);
    grid->setHorizontalSpacing(kInterfaceColumnSpacing);
    grid->setVerticalSpacing(2);

    const QVector<UsbInterfaceInfo> interfaces =
        m_settings ? m_settings->interfaces() : QVector<UsbInterfaceInfo>();

    if (interfaces.isEmpty()) {
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

    // Which one the configuration protocol answered on. Marked rather than
    // listed on its own, because the point of this box is that the interface in
    // use is one of several and the others are worth seeing beside it.
    const std::optional<UsbInterfaceInfo> active =
        m_settings ? m_settings->activeInterface() : std::nullopt;

    int row = 1;
    for (const UsbInterfaceInfo &iface : interfaces) {
        const bool inUse = active && active->sysPath == iface.sysPath;

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
        // interface and a HID driver holds the node under it — and which is
        // which matters: the second is the one that decides force feedback.
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

        // An interface with no hidraw node is not a fault — the Alpha Evo has
        // one, and it is where the LED ring's endpoint lives — but it does
        // explain why nothing here speaks reports, and the row would otherwise be
        // silent about it. "hidraw" rather than "device": it is still reached
        // through a node, the usbfs one, and the access beside it is that node's.
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
    // Laid out like the Error reading, and for its reasons: value first so the
    // words line up down the column, glyph after, and no margins of the nested
    // layout's own to make this row taller than its neighbours.
    auto *layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);

    auto *text = new QLabel(parent);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(text);

    auto *icon = new QLabel(parent);
    icon->setFixedWidth(kIconSize);
    layout->addWidget(icon);

    layout->addStretch(1);
    grid->addLayout(layout, row, column);

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

    // A glyph only when something is wrong. Every interface on a working base
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

void StatusPage::buildForceTrace(QVBoxLayout *layout)
{
    if (!m_forceTrace) {
        // Nothing to draw from, so the page keeps the shape it always had.
        layout->addStretch(1);
        return;
    }

    layout->addSpacing(12);

    // --- The controls -------------------------------------------------------
    auto *controls = new QHBoxLayout;

    m_traceToggle = new QCheckBox(tr("Force trace"), this);
    m_traceToggle->setObjectName(QStringLiteral("forceTraceToggle"));
    m_traceToggle->setToolTip(tr("Graph the force the base is applying. Off by default because it "
                                 "reads about a thousand reports a second."));
    controls->addWidget(m_traceToggle);
    controls->addStretch(1);

    m_traceSpan = new QLabel(this);
    m_traceSpan->setObjectName(QStringLiteral("forceTraceSpan"));
    m_traceSpan->setToolTip(tr("Report rate, time span of the graph, and peak force."));
    controls->addWidget(m_traceSpan);

    const auto addZoomButton = [this, controls](const QString &text, const QString &objectName,
                                                const QString &tip) {
        auto *button = new QToolButton(this);
        button->setObjectName(objectName);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        controls->addWidget(button);
        return button;
    };

    m_zoomOut = addZoomButton(QStringLiteral("−"), QStringLiteral("forceZoomOut"),
                              tr("Show more time, in less detail."));
    m_zoomIn = addZoomButton(QStringLiteral("+"), QStringLiteral("forceZoomIn"),
                             tr("Show less time, in more detail."));

    layout->addLayout(controls);

    // --- Why there may be nothing to draw ------------------------------------
    m_traceStatus = new QLabel(this);
    m_traceStatus->setObjectName(QStringLiteral("forceTraceStatus"));
    m_traceStatus->setWordWrap(true);
    m_traceStatus->setEnabled(false);
    layout->addWidget(m_traceStatus);

    // --- The graph ----------------------------------------------------------
    m_trace = new TraceGraph(this);
    m_trace->setObjectName(QStringLiteral("forceTraceGraph"));

    // Set here rather than in TraceGraph: whether a widget grows is the layout's
    // business, not the widget's, which is the same call made for the LED ring.
    m_trace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_trace->setMinimumHeight(kTraceMinimumHeight);
    layout->addWidget(m_trace, 1);

    // Straight from the decode into the widget's intake buffer. addSample() only
    // buffers, so nothing on this path repaints and the report rate never reaches
    // the UI.
    connect(m_forceTrace, &AlphaEvoForceTrace::forceSampled, m_trace, &TraceGraph::addSample);

    // An unplug takes the source away, so the box is unticked as well as closed
    // off: it says what the user wants, and there is no longer anything to want.
    connect(m_forceTrace, &AlphaEvoForceTrace::sourceStateChanged, this, [this] {
        if (!m_forceTrace->isPossible() && m_traceToggle->isChecked()) {
            const QSignalBlocker blocked(m_traceToggle);
            m_traceToggle->setChecked(false);
        }
        updateTraceRunState();
    });

    connect(m_zoomIn, &QToolButton::clicked, m_trace, &TraceGraph::zoomIn);
    connect(m_zoomOut, &QToolButton::clicked, m_trace, &TraceGraph::zoomOut);
    connect(m_trace, &TraceGraph::zoomChanged, this, &StatusPage::refreshTraceControls);

    m_readout = new QTimer(this);
    m_readout->setInterval(1000 / kTraceReadoutHz);
    connect(m_readout, &QTimer::timeout, this, &StatusPage::refreshTraceControls);

    // State before the handler, as the dry-run box does and for the same reason:
    // initialising the box must not run a slot that touches what is not built.
    m_traceToggle->setChecked(m_forceTrace->isEnabled());
    connect(m_traceToggle, &QCheckBox::toggled, this, &StatusPage::updateTraceRunState);

    updateTraceRunState();
}

void StatusPage::updateTraceRunState()
{
    if (!m_trace || !m_forceTrace)
        return;

    // Three things decide it and they are all read here, so there is one place
    // the answer is worked out and no way for two of them to disagree.
    //
    // Being the tab on screen is one of them: reading a kilohertz for a graph
    // nobody is looking at is work for nothing, so leaving the tab stops the
    // reading at its source and lets go of the interface. What was on the graph
    // goes with it, which is why coming back starts from empty — the same fresh
    // start a zoom change gives, and for the same reason.
    const bool wanted = m_traceToggle->isChecked() && isVisible() && m_forceTrace->isPossible();

    m_forceTrace->setEnabled(wanted);

    // Asked, not assumed: the interface may have refused to open, and a graph
    // running against nothing would draw a flat line that looked like a reading.
    const bool running = m_forceTrace->isEnabled();
    m_trace->setRunning(running);

    if (running)
        m_readout->start();
    else
        m_readout->stop();

    refreshTraceControls();
}

void StatusPage::refreshTraceControls()
{
    if (!m_trace || !m_forceTrace)
        return;

    // Closed off when there is nothing to read and nothing that will bring it
    // back: no such report, an offline preview, or a wheel that was unplugged.
    m_traceToggle->setEnabled(m_forceTrace->isPossible());

    const QString why = m_forceTrace->sourceDescription();
    m_traceStatus->setText(why);
    m_traceStatus->setVisible(!why.isEmpty());

    m_zoomIn->setEnabled(m_trace->isRunning() && m_trace->canZoomIn());
    m_zoomOut->setEnabled(m_trace->isRunning() && m_trace->canZoomOut());

    const qreal rate = m_trace->sampleRateHz();
    const qreal span = m_trace->spanSeconds();
    if (rate <= 0.0 || span <= 0.0) {
        m_traceSpan->setText(kNoValue);
        return;
    }

    const QString spanText = span < 1.0
        ? tr("%1 ms").arg(QLocale().toString(qreal(std::lround(span * 1000.0)), 'f', 0))
        : tr("%1 s").arg(QLocale().toString(span, 'f', span < 10.0 ? 1 : 0));

    // The peak is what a force trace is usually being watched for, and it is the
    // one number that is hard to read off a moving graph by eye.
    const TraceSpan extent = m_trace->visibleExtent();
    const qreal peak = std::max(std::fabs(qreal(extent.lo)), std::fabs(qreal(extent.hi)));

    m_traceSpan->setText(tr("%1 Hz  ·  %2  ·  peak %3")
                             .arg(QLocale().toString(qreal(std::lround(rate)), 'f', 0), spanText,
                                  QLocale().toString(peak, 'f', 2)));
}

void StatusPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateTraceRunState();
}

void StatusPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateTraceRunState();
}

} // namespace hidcenter::simagic_alpha_evo
