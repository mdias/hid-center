// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CalibrationPage.h"

#include "../CalibrationSession.h"
#include "../ConfigRead.h"
#include "../ParamsPoll.h"
#include "PedalBar.h"
#include "../Pedals.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace hidcenter::simsonn_pedals {
namespace {

/// Shown where a value would go before one has arrived.
const auto kNoValue = QStringLiteral("—");

/// Between "begin a run" and the two buttons that end one. Wide enough that the
/// two groups read as separate things and that a slip on one cannot land on the
/// other.
constexpr int kButtonGroupGap = 24;

/// How long Save stays shut after a run begins.
///
/// Long enough to outlast anything a single intent can produce — a double click,
/// a key held a beat too long, a mouse that bounced — and short enough that
/// nobody calibrating a pedal will ever notice it, since the first thing they do
/// is reach for the pedals. A write to the hardware is worth one second of not
/// being able to make it by accident.
constexpr int kSaveDelayMs = 1000;

/// The stored calibration of one pedal, in words.
QString storedCalibration(const ConfigRead::AxisCalibration &calibration, bool isBrake)
{
    QString text = QObject::tr("Calibrated %1 to %2")
                       .arg(calibration.physicalMinimum)
                       .arg(calibration.physicalMaximum);
    if (isBrake) {
        // The physical top, and deliberately not effective maximum. This is a force, and
        // the force at the end of the travel is not changed by the device
        // throwing some of that travel away: a deadzone moves where the *output*
        // reaches 100 %, not how hard the pedal is being pressed there. Same
        // reason the live reading counts from physical minimum rather than from
        // effective minimum.
        //
        // So this is not the figure the "Maximum force" box holds. That box is
        // defined against the effective top — it divides by `1 - ldz/100` going
        // in, which the end deadzone coming back out very nearly undoes — so
        // 18 kg is 18.9 kg of actual load cell on a unit with 5 % at each end.
        text += QObject::tr("  ·  full brake at %1 kg")
                    .arg(brakeForceKg(calibration.physicalMaximum), 0, 'f', 1);
    }
    return text;
}

/// What a run is going to store for one pedal, and where that came from.
///
/// Four different sentences because there are four different situations, and
/// which one a pedal is in is the thing somebody about to press Save needs to
/// know: a pedal that was measured, one that barely moved and is about to be
/// written off as unused, one whose thumb was dragged, and one that is simply
/// not being touched by this run at all.
QString pendingCalibration(const CalibrationSession &session, int axis,
                           const std::pair<qint16, qint16> &endpoints, bool isBrake)
{
    using Source = CalibrationSession::Source;
    const Source source = session.sourceFor(axis);

    QString text;
    switch (source) {
    case Source::Unmoved: {
        const std::optional<std::pair<int, int>> travel = session.measured(axis);
        return QObject::tr("Moved only %1 counts: saved as unused, %2 to %3")
            .arg(travel ? travel->second - travel->first : 0)
            .arg(kRawMinimum)
            .arg(kRawMaximum);
    }
    case Source::Measured:
        text = QObject::tr("Measured %1 to %2").arg(endpoints.first).arg(endpoints.second);
        break;
    case Source::Edited:
        text = QObject::tr("Set by hand to %1 to %2")
                   .arg(endpoints.first)
                   .arg(endpoints.second);
        break;
    case Source::Stored:
        text = QObject::tr("Untouched at %1 to %2 — not written")
                   .arg(endpoints.first)
                   .arg(endpoints.second);
        break;
    }

    // Saving redefines what a full brake is, so the force at the new top of the
    // travel is worth showing while there is still a chance to press harder or to
    // drag the thumb somewhere else. The endpoint itself, for the reason
    // storedCalibration() gives — and it is also the number that gets written, so
    // nothing here moves when it is.
    if (isBrake) {
        text += source == Source::Stored
            ? QObject::tr("  ·  full brake stays at %1 kg")
                  .arg(brakeForceKg(endpoints.second), 0, 'f', 1)
            : QObject::tr("  ·  full brake would become %1 kg")
                  .arg(brakeForceKg(endpoints.second), 0, 'f', 1);
    }
    return text;
}

/// Where `raw` falls across the pedal's **effective** range, as a percentage, or
/// nothing when the device has stored a range with nothing in it.
///
/// The effective range and not the physical one, because this is meant to be the
/// same number the Main Settings graph shows: effective minimum and effective maximum are
/// exactly where the device starts reporting 0 % and 100 %, so a pedal sitting
/// inside its start deadzone reads 0 % here as it does there. Clamped for the
/// same reason — travel past an endpoint is output the device is throwing away,
/// and calling it 103 % would suggest otherwise.
std::optional<double> outputPercent(const ConfigRead::AxisCalibration &calibration, int raw)
{
    const double span = double(calibration.effectiveMaximum) - calibration.effectiveMinimum;
    if (span <= 0.0)
        return {};
    return qBound(0.0, (double(raw) - calibration.effectiveMinimum) / span * 100.0, 100.0);
}

/// The same, spelled out for a tooltip: the two ranges and why they differ.
QString storedCalibrationDetail(const ConfigRead::AxisCalibration &calibration)
{
    return QObject::tr(
               "Physical travel: %1 to %2 (what a calibration measures, and where the "
               "thumbs sit).\n"
               "Effective range: %3 to %4 (where the pedal reads 0 % and 100 %).\n"
               "Deadzones: %5 % at the start and %6 % at the end, taken off the physical "
               "travel to give the effective range.")
        .arg(calibration.physicalMinimum)
        .arg(calibration.physicalMaximum)
        .arg(calibration.effectiveMinimum)
        .arg(calibration.effectiveMaximum)
        .arg(calibration.startDeadzone)
        .arg(calibration.endDeadzone);
}

} // namespace

CalibrationPage::CalibrationPage(const QString &modelName,
                                 const std::optional<UsbInterfaceInfo> &vendorInterface,
                                 ParamsPoll *params, ConfigRead *config,
                                 CalibrationSession *session, QWidget *parent)
    : QWidget(parent)
    , m_params(params)
    , m_config(config)
    , m_session(session)
    , m_vendorInterface(vendorInterface)
{
    auto *layout = new QVBoxLayout(this);

    auto *title = new QLabel(QStringLiteral("<h2>%1</h2>").arg(modelName.toHtmlEscaped()), this);
    layout->addWidget(title);

    auto *explanation = new QLabel(
        tr("<b>Raw sensor values</b>, from %1 to %2. The two thumbs on each bar are the "
           "calibrated ends of that pedal's travel: a pedal that stops short of its upper "
           "thumb never reaches full output. <b>Drag a thumb</b> to set an end by hand.")
            .arg(kRawMinimum)
            .arg(kRawMaximum),
        this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto *saving = new QLabel(
        tr("<b>Saving writes to the pedals.</b> <i>Start Calibration</i> records how far "
           "each pedal travels and moves the thumbs to match; <i>Cancel</i> discards it. "
           "<i>Save Calibration</i> stores the measured travel on the pedals, keeping their "
           "curves and deadzones. Recalibrating the brake also changes its full-brake "
           "force."),
        this);
    saving->setWordWrap(true);
    layout->addWidget(saving);

    auto *dragging = new QLabel(
        tr("Dragging a thumb also starts a calibration, with the same <i>Save</i> and "
           "<i>Cancel</i>, but only for that pedal; the others are left unchanged. During "
           "a measuring calibration, a dragged thumb stays where you put it."),
        this);
    dragging->setWordWrap(true);
    layout->addWidget(dragging);

    m_notice = new QLabel(this);
    m_notice->setWordWrap(true);
    m_notice->setEnabled(false);
    layout->addWidget(m_notice);

    auto *box = new QGroupBox(tr("Live raw values"), this);
    auto *form = new QFormLayout(box);

    for (const PedalSpec &pedal : kPedals) {
        Row row;

        row.bar = new PedalBar(box);
        row.bar->setRange(kRawMinimum, kRawMaximum);

        // Dragging a thumb goes to the session rather than being kept here: it
        // begins a calibration, and there is one object that decides what a
        // calibration is. The axis is captured, not the row index, because it is
        // the axis every one of the session's questions is asked about.
        const int axis = pedal.axis;
        connect(row.bar, &PedalBar::calibrationEdited, this, [this, axis](int low, int high) {
            if (m_session && m_vendorInterface)
                m_session->adjustEndpoint(*m_vendorInterface, axis, low, high);
        });

        row.stored = new QLabel(box);
        row.stored->setEnabled(false);

        auto *cell = new QVBoxLayout;
        cell->setContentsMargins(0, 0, 0, 0);
        cell->addWidget(row.bar);
        cell->addWidget(row.stored);
        form->addRow(tr(pedal.title), cell);

        m_rows.append(row);
    }

    // The brake is a load cell, so its count is a force and can be said in
    // kilograms. The factor behind that is derived rather than weighed, which is
    // worth saying where the number is read and not only in the source.
    m_rows[kBrakePedal].bar->setToolTip(
        tr("The brake is a load cell, so its raw value is proportional to force: about "
           "%1 counts per kg above %2. This is an estimate, not checked against a known "
           "weight.\n\nDeadzones do not affect it.")
            .arg(kBrakeCountsPerKg, 0, 'f', 0)
            .arg(kBrakeZeroCount));

    layout->addWidget(box);

    auto *buttons = new QHBoxLayout;

    m_start = new QPushButton(tr("Start Calibration"), this);
    m_start->setToolTip(tr("Record how far each pedal travels. Nothing is saved until you save."));
    connect(m_start, &QPushButton::clicked, this, [this] {
        if (m_session && m_vendorInterface)
            m_session->start(*m_vendorInterface);
    });
    buttons->addWidget(m_start);
    buttons->addSpacing(kButtonGroupGap);

    m_save = new QPushButton(tr("Save Calibration"), this);
    m_save->setToolTip(tr("Save the measured travel to the pedals and check it was stored. "
                          "Unavailable for a second after a calibration begins."));
    connect(m_save, &QPushButton::clicked, this, [this] {
        if (m_session && m_vendorInterface)
            m_session->save(*m_vendorInterface);
    });
    buttons->addWidget(m_save);

    m_saveDelay = new QTimer(this);
    m_saveDelay->setSingleShot(true);
    m_saveDelay->setInterval(kSaveDelayMs);
    connect(m_saveDelay, &QTimer::timeout, this, &CalibrationPage::refreshSession);

    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->setToolTip(tr("Discard the measurement and leave the pedals unchanged."));
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_session)
            m_session->cancel();
    });
    buttons->addWidget(m_cancel);

    buttons->addStretch(1);
    layout->addLayout(buttons);

    // Below the buttons, not above them. A message that appears when a run
    // starts would otherwise push the whole row down at the exact moment
    // somebody is about to click in it.
    m_sessionMessage = new QLabel(this);
    m_sessionMessage->setWordWrap(true);
    layout->addWidget(m_sessionMessage);

    layout->addStretch(1);

    if (m_params) {
        connect(m_params, &ParamsPoll::paramsChanged, this, &CalibrationPage::refreshValues);
        connect(m_params, &ParamsPoll::stateChanged, this, &CalibrationPage::refreshNotice);
    }
    if (m_config) {
        connect(m_config, &ConfigRead::stateChanged, this, [this] {
            refreshCalibration();
            // Whether a calibration can be started at all turns on the blob
            // having been read, so the buttons follow this too.
            refreshSession();
            refreshNotice();
        });
    }
    if (m_session) {
        connect(m_session, &CalibrationSession::changed, this, [this] {
            refreshEndpoints();
            refreshSession();
        });
    }

    refreshValues();
    refreshCalibration();
    refreshSession();
    refreshNotice();
}

// --- Asking for it ----------------------------------------------------------

void CalibrationPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The blob is not asked for here: the device reads it once when it connects,
    // for this page and the settings page both. Only the params stream starts
    // and stops with this page, because only this page reads it and it is 500
    // reports a second. The two share the one interface quite happily — the
    // config read is host-paced and its replies carry their own report id, so
    // the stream does not swallow them.
    if (m_vendorInterface && m_params)
        m_params->start(*m_vendorInterface);
    // Whether either could be started at all is only known now, and it is what
    // the notice above the bars has to say.
    refreshNotice();
}

void CalibrationPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_params)
        m_params->stop();
}

// --- Showing it -------------------------------------------------------------

void CalibrationPage::refreshValues()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const std::optional<qint16> raw =
            m_params ? m_params->rawAxis(kPedals[i].axis) : std::optional<qint16>();

        if (!raw) {
            m_rows[i].bar->setValue({});
            m_rows[i].bar->setText(kNoValue);
            continue;
        }

        m_rows[i].bar->setValue(int(*raw));

        // The brake says kilograms where the others say a percentage: a load
        // cell's reading is a force, and how far along its travel that is means
        // much less than how hard it is being pressed. The others are positions
        // and the percentage is the whole point of them.
        QString text = QString::number(*raw);
        if (i == kBrakePedal) {
            text = tr("%1  ·  %2 kg").arg(*raw).arg(brakeForceKg(*raw), 0, 'f', 1);
        } else if (m_rows[i].calibration) {
            // No percentage at all until the stored range is known — there is
            // nothing to be a percentage *of*, and one worked out against the
            // sensor's full range would not be the number the rest of the
            // application shows.
            if (const std::optional<double> percent =
                    outputPercent(*m_rows[i].calibration, *raw))
                text = tr("%1  ·  %2 %").arg(*raw).arg(*percent, 0, 'f', 1);
        }
        m_rows[i].bar->setText(text);
    }

    // The first report to arrive is what answers "waiting for the first params
    // report", and nothing else tells the notice that. Only while something is
    // showing, so the common case — a page with no notice, updating at 60 Hz —
    // does not rebuild a sentence sixty times a second to throw it away.
    if (m_notice->isVisible())
        refreshNotice();
}

void CalibrationPage::refreshCalibration()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rows[i].calibration = m_config ? m_config->axis(kPedals[i].axis)
                                         : std::optional<ConfigRead::AxisCalibration>();
    }

    // The bars carry a percentage worked out against the stored range, and the
    // thumbs carry the range itself, so both follow from this.
    refreshEndpoints();
    refreshValues();
}

void CalibrationPage::refreshEndpoints()
{
    // While a run is going, the thumbs are what the run would store rather than
    // what the device holds — that is the point of watching it happen. The stored
    // values are not lost by this: nothing has been written, so cancelling puts
    // them straight back. Which of the two a *given* pedal is showing is the
    // session's to say, since a run begun by dragging one thumb leaves the other
    // pedals showing the device's own numbers.
    const bool running = m_session && m_session->isRunning();

    for (int i = 0; i < m_rows.size(); ++i) {
        Row &row = m_rows[i];
        const int axis = kPedals[i].axis;
        const bool isBrake = i == kBrakePedal;

        if (running) {
            // Whatever the save would write, which for an unmoved pedal is the
            // whole range rather than the sliver it drifted through: the bar has
            // to agree with the sentence under it about what saving would do.
            const std::pair<qint16, qint16> endpoints = m_session->endpointsFor(axis);
            row.bar->setCalibration(std::pair {int(endpoints.first), int(endpoints.second)});
            row.stored->setText(pendingCalibration(*m_session, axis, endpoints, isBrake));
            row.stored->setToolTip(tr("Not saved yet. Cancel restores what the pedals hold."));
            continue;
        }

        if (!row.calibration) {
            row.bar->setCalibration({});
            row.stored->clear();
            row.stored->setToolTip({});
            continue;
        }

        row.bar->setCalibration(std::pair {int(row.calibration->physicalMinimum),
                                           int(row.calibration->physicalMaximum)});
        row.stored->setText(storedCalibration(*row.calibration, isBrake));
        row.stored->setToolTip(storedCalibrationDetail(*row.calibration));
    }
}

void CalibrationPage::refreshSession()
{
    using State = CalibrationSession::State;
    const State state = m_session ? m_session->state() : State::Idle;

    const bool measuring = state == State::Measuring;
    const bool busy = state == State::Saving || state == State::Verifying;

    // Entering a run, as opposed to being in one: the delay is armed once, not
    // restarted every time a pedal moves and the buttons are worked out again.
    if (measuring && m_lastState != State::Measuring)
        m_saveDelay->start();
    m_lastState = state;

    // **All three stay put, and are only enabled and disabled.** Hiding the ones
    // that do not apply reads better and is a trap: pressing Start puts Save
    // exactly where Start was, under a cursor that has not moved, and a button
    // that writes to hardware must never appear beneath a finger that was aiming
    // at something else. Found the hard way — a single click on Start came back
    // as a completed save.
    m_start->setEnabled(!measuring && !busy && m_session && m_vendorInterface && m_config
                        && m_config->state() == ConfigRead::State::Ready);
    m_save->setEnabled(measuring && !m_saveDelay->isActive());
    m_cancel->setEnabled(measuring);

    // The thumbs are draggable whether or not a run is going — dragging one is
    // one of the ways to begin a run — but not while a save is on the wire, when
    // moving an endpoint could not lead anywhere.
    const bool editable = m_session && m_vendorInterface && !busy;
    for (const Row &row : std::as_const(m_rows))
        row.bar->setEditable(editable);

    const QString message = m_session ? m_session->message() : QString();
    m_sessionMessage->setText(message);
    m_sessionMessage->setVisible(!message.isEmpty());
    // The one place on this page where something can have gone wrong with the
    // device rather than merely not started yet, so the one place worth a
    // colour. The same red the Status page uses for a failed condition.
    m_sessionMessage->setStyleSheet(m_session && m_session->failed()
                                        ? QStringLiteral("color: #c03030;")
                                        : QString());
}

void CalibrationPage::refreshNotice()
{
    QString reason;
    if (!m_vendorInterface)
        reason = tr("Configuration interface not found: raw values and calibration are "
                    "unavailable.");
    else if (!m_params || !m_config)
        reason = tr("The pedals are not being read.");
    else if (!m_params->error().isEmpty())
        reason = m_params->error();
    else if (m_config->state() == ConfigRead::State::Failed)
        reason = tr("Could not read the stored calibration, so the bars show no ends. %1")
                     .arg(m_config->error());
    else if (m_config->state() == ConfigRead::State::Reading)
        reason = tr("Reading the stored calibration… (%1 of %2)")
                     .arg(m_config->packetsRead() + 1)
                     .arg(m_config->packetCount());
    else if (!m_params->hasParams())
        reason = tr("Waiting for the pedals' first reading.");

    m_notice->setText(reason);
    m_notice->setVisible(!reason.isEmpty());
}

} // namespace hidcenter::simsonn_pedals
