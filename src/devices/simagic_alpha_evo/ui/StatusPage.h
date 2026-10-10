// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "AlphaEvoForceTrace.h"
#include "AlphaEvoSettings.h"

class QCheckBox;
class QGridLayout;
class QLabel;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace hidcenter::simagic_alpha_evo {

class TraceGraph;

/// What is true about one Alpha Evo, as opposed to what can be changed on it.
///
/// Two conditions at the top — the force-feedback driver, and whether the base
/// answers the configuration protocol — each either confirmed or flagged, so the
/// state of the wheelbase reads at a glance rather than as sentences. Then the
/// facts worth knowing about it underneath, in two columns read down and then
/// down again: what this base is and how it is doing on the left, every number
/// carrying a unit on the right. Two columns because there are ten of them and
/// the height one stack of them took is height the trace below would rather
/// have. Then every interface, each with whether its node can be read and
/// written.
///
/// The conditions, the facts and the interfaces are available without an open
/// handle, which is deliberate: this page is at its most useful exactly when the
/// others cannot work.
///
/// Below them sits the **force trace**, which is the one thing here that is not a
/// fact but a measurement — what the base is doing rather than what is true about
/// it. It is off until asked for, because it is fed at about a kilohertz, and it
/// draws nothing until an interface carrying that report is open. See
/// AlphaEvoForceTrace for why this page never opens one.
class StatusPage : public QWidget
{
    Q_OBJECT

public:
    /// `probeError` is empty when the base answered the configuration protocol,
    /// exactly as MainSettingsPage takes it. `forceTrace` may be null, in which
    /// case the trace and its controls are simply not built.
    explicit StatusPage(AlphaEvoSettings *settings, AlphaEvoForceTrace *forceTrace,
                        const QString &probeError, QWidget *parent = nullptr);

protected:
    /// The trace is only drawn while the tab is being looked at. Sampling is not
    /// affected — see TraceGraph::tick().
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    /// Follow a fresh status read, which is the only thing on this page that
    /// changes while it is open.
    void refreshFacts();

    /// Follow the base's live telemetry: report 0x23, which unlike the facts
    /// above changes on nearly every poll. Separate from refreshFacts() because
    /// it follows a separate signal — see AlphaEvoSettings::baseStatusRefreshed.
    void refreshLiveFacts();

    /// Keep the zoom buttons and the readouts in step with the graph.
    void refreshTraceControls();

    /// Start or stop the trace, from the only three things that decide it: the
    /// box, whether this is the tab on screen, and whether there is anything to
    /// read. Called from all three, so the answer is worked out in one place.
    void updateTraceRunState();

private:
    /// How a condition reads: confirmed, wrong, or not something we could check.
    enum class Severity { Ok, Alarm, Unknown };

    /// One condition: an icon that says which of the three states it is in, and
    /// the sentence explaining it.
    struct StatusRow
    {
        QLabel *icon = nullptr;
        QLabel *text = nullptr;
    };

    StatusRow addConditionRow(QVBoxLayout *layout, const QString &objectName);
    void setRow(const StatusRow &row, Severity severity, const QString &text);

    /// The glyph for a severity, or nothing at all for Unknown. Shared by the
    /// condition rows and the error reading, so the two cannot drift into using
    /// different icons for the same verdict.
    void setSeverityIcon(QLabel *icon, Severity severity) const;

    void updateDriverRow();
    void updateProtocolRow(const QString &probeError);

    /// The base's running state as a word. Falls back to the raw number outside
    /// 1..7, because that numbering has never been checked against a wheelbase.
    QString stateName(quint8 state) const;

    /// Every interface the base exposes, one row each, with the one carrying the
    /// configuration protocol picked out and a warning beside any whose node
    /// cannot be both read and written.
    ///
    /// Built once and never refreshed. Enumeration data cannot change underneath
    /// this page: a driver binding or unbinding, or access being granted,
    /// re-enumerates the device, which rebuilds the module and this page with it.
    /// The same reasoning the driver row above is built on.
    void buildInterfaces(QVBoxLayout *layout);

    /// One interface's access, in words, with a warning glyph and the fix in its
    /// tooltip when it is anything less than read and write.
    void buildAccessCell(QWidget *parent, QGridLayout *grid, int row, int column,
                         const UsbAccessInfo &access);

    /// The checkbox, the zoom buttons, the readouts and the graph. Built last,
    /// where the page's trailing stretch used to be.
    void buildForceTrace(QVBoxLayout *layout);

    AlphaEvoSettings *m_settings = nullptr;
    AlphaEvoForceTrace *m_forceTrace = nullptr;

    StatusRow m_driver;
    StatusRow m_protocol;

    QLabel *m_firmware = nullptr;

    /// The live half of the Device group. Every one of these is empty on a base
    /// that does not answer report 0x23, which no wheelbase has yet been seen to
    /// do — see BaseStatusReport.
    QLabel *m_state = nullptr;
    QLabel *m_error = nullptr;
    QLabel *m_errorIcon = nullptr;
    QLabel *m_motorTemp = nullptr;
    QLabel *m_mosfetTemp = nullptr;
    QLabel *m_maxTorque = nullptr;
    QLabel *m_wheelId = nullptr;
    QLabel *m_busVoltage = nullptr;
    QLabel *m_powerSupplyVoltage = nullptr;

    QCheckBox *m_traceToggle = nullptr;
    QToolButton *m_zoomIn = nullptr;
    QToolButton *m_zoomOut = nullptr;
    QLabel *m_traceSpan = nullptr;
    QLabel *m_traceStatus = nullptr;
    TraceGraph *m_trace = nullptr;

    /// Drives the rate and span readouts, which follow a *measured* rate and so
    /// change on their own rather than only when something is clicked. Runs only
    /// while the trace does and the page is being looked at.
    QTimer *m_readout = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
