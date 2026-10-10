// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CalibrationSession.h"

#include "ParamsPoll.h"
#include "PedalWrite.h"

#include "core/usb/IUsbDevice.h"

namespace hidcenter::simsonn_pedals {

CalibrationSession::CalibrationSession(IUsbDevice *device, ParamsPoll *params,
                                       ConfigRead *config, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_params(params)
    , m_config(config)
{
    if (m_params)
        connect(m_params, &ParamsPoll::paramsChanged, this, &CalibrationSession::observe);
    if (m_config)
        connect(m_config, &ConfigRead::stateChanged, this, &CalibrationSession::onConfigRead);
}

// --- Measuring --------------------------------------------------------------

void CalibrationSession::start(const UsbInterfaceInfo &interface)
{
    if (isRunning())
        return;

    // The save has to hand the device back the pedal's curve and deadzones
    // untouched, and there is nowhere to get them but the blob. Starting a run
    // that could not be saved would waste the user's pedal-pressing.
    if (!m_config || m_config->state() != ConfigRead::State::Ready) {
        finish(tr("Cannot calibrate: the pedals' settings have not been read."),
               true);
        return;
    }

    m_interfaceInfo = interface;
    m_travel = {};
    m_edited = {};
    m_watching = true;
    m_state = State::Measuring;
    m_failed = false;
    m_message = tr("Calibrating: press each pedal slowly through its full travel, then save. "
                   "Pedals you leave alone are saved as unused; a pedal whose thumb you "
                   "drag keeps the value you set.");

    // Whatever the pedals read at this instant is the first sample, so a pedal
    // that is never touched still has a travel — an empty one.
    observe();
    Q_EMIT changed();
}

void CalibrationSession::cancel()
{
    if (!isRunning())
        return;

    finish(tr("Calibration cancelled. Nothing was saved."), false);
}

void CalibrationSession::adjustEndpoint(const UsbInterfaceInfo &interface, int axis, int low,
                                       int high)
{
    // Nothing doing while a save is in flight: the values it is checking itself
    // against are already out on the wire.
    if (m_state != State::Idle && m_state != State::Measuring)
        return;

    const int index = indexOf(axis);
    if (index < 0)
        return;

    if (m_state == State::Idle) {
        // The same requirement as start(), for the same reason: a save has to
        // hand back the pedal's curve and deadzones, and they live in the blob.
        if (!m_config || m_config->state() != ConfigRead::State::Ready) {
            finish(tr("Cannot change the calibration: the pedals' settings have not been read."),
                   true);
            return;
        }

        // Not m_watching: this run is exactly the endpoints somebody moves by
        // hand, and the pedals they do not touch are to be left alone rather
        // than measured from wherever they happen to be resting.
        m_interfaceInfo = interface;
        m_travel = {};
        m_edited = {};
        m_watching = false;
        m_state = State::Measuring;
        m_failed = false;
        m_message = tr("Setting the calibration by hand: drag the thumbs, then save. Pedals you "
                       "do not touch are left unchanged.");
    }

    m_edited[index] = std::pair {low, high};
    Q_EMIT changed();
}

void CalibrationSession::observe()
{
    // Not while a run begun by dragging a thumb is going: watching would move
    // the other two pedals' endpoints, which is what that run exists not to do.
    if (m_state != State::Measuring || !m_watching || !m_params)
        return;

    bool moved = false;
    for (int i = 0; i < kPedalCount; ++i) {
        const std::optional<qint16> raw = m_params->rawAxis(kPedals[i].axis);
        if (!raw)
            continue;

        Travel &travel = m_travel[i];
        if (!travel.seen) {
            travel.seen = true;
            travel.lowest = *raw;
            travel.highest = *raw;
            moved = true;
            continue;
        }
        if (*raw < travel.lowest) {
            travel.lowest = *raw;
            moved = true;
        }
        if (*raw > travel.highest) {
            travel.highest = *raw;
            moved = true;
        }
    }

    // Only when a bound actually moved: this runs at 60 Hz and a pedal being
    // held still has nothing new to redraw.
    if (moved)
        Q_EMIT changed();
}

int CalibrationSession::indexOf(int axis)
{
    for (int i = 0; i < kPedalCount; ++i) {
        if (kPedals[i].axis == axis)
            return i;
    }
    return -1;
}

std::optional<std::pair<int, int>> CalibrationSession::measured(int axis) const
{
    const int index = indexOf(axis);
    if (index < 0 || !m_travel[index].seen)
        return {};
    return std::pair {m_travel[index].lowest, m_travel[index].highest};
}

std::optional<std::pair<int, int>> CalibrationSession::storedEndpoints(int axis) const
{
    if (!m_config)
        return {};
    const std::optional<ConfigRead::AxisCalibration> stored = m_config->axis(axis);
    if (!stored)
        return {};
    return std::pair {int(stored->physicalMinimum), int(stored->physicalMaximum)};
}

bool CalibrationSession::moved(int axis) const
{
    const std::optional<std::pair<int, int>> travel = measured(axis);
    return travel && travel->second - travel->first >= kMovementThreshold;
}

CalibrationSession::Source CalibrationSession::sourceFor(int axis) const
{
    const int index = indexOf(axis);
    if (index < 0)
        return Source::Stored;
    if (m_edited[index])
        return Source::Edited;
    if (!m_watching || !m_travel[index].seen)
        return Source::Stored;
    return moved(axis) ? Source::Measured : Source::Unmoved;
}

std::pair<qint16, qint16> CalibrationSession::endpointsFor(int axis) const
{
    const int index = indexOf(axis);
    switch (index < 0 ? Source::Stored : sourceFor(axis)) {
    case Source::Edited:
        return {qint16(m_edited[index]->first), qint16(m_edited[index]->second)};

    case Source::Measured:
        return {qint16(m_travel[index].lowest), qint16(m_travel[index].highest)};

    case Source::Unmoved:
        return {qint16(kRawMinimum), qint16(kRawMaximum)};

    case Source::Stored:
        // The device's own, so that "leave this pedal alone" and "write this
        // pedal what it already has" are the same set of numbers, and the
        // read-back after a save can be compared against them either way.
        if (const std::optional<std::pair<int, int>> stored = storedEndpoints(axis))
            return {qint16(stored->first), qint16(stored->second)};
        break;
    }
    return {qint16(kRawMinimum), qint16(kRawMaximum)};
}

// --- Saving -----------------------------------------------------------------

void CalibrationSession::save(const UsbInterfaceInfo &interface)
{
    if (m_state != State::Measuring)
        return;

    m_interfaceInfo = interface;
    m_state = State::Saving;
    m_message = tr("Saving the calibration…");
    m_failed = false;
    Q_EMIT changed();

    for (int i = 0; i < kPedalCount; ++i)
        m_written[i] = endpointsFor(kPedals[i].axis);

    for (int i = 0; i < kPedalCount; ++i) {
        const PedalSpec &pedal = kPedals[i];

        // A pedal already holding what this would write is not written. Partly
        // because a write that changes nothing is a write worth not making, and
        // partly because `<x>_pf` carries the whole pedal: rewriting one would
        // replace the device's effective trio with this host's derivation of it,
        // which for an axis whose range predates the deadzones is a change
        // nobody asked for. m_written still holds its values, so the read-back
        // checks it like any other.
        const std::optional<std::pair<int, int>> stored = storedEndpoints(pedal.axis);
        if (stored && stored->first == m_written[i].first
            && stored->second == m_written[i].second)
            continue;

        if (writePedal(pedal))
            continue;
        finish(tr("Could not save the calibration: %1")
                   .arg(m_device ? m_device->lastError() : QString()),
               true);
        return;
    }

    // Nothing comes back from a `<letter>_pf`, so the device is asked for its
    // configuration again and the answer is compared against what went out. Only
    // that makes "saved" mean anything.
    m_state = State::Verifying;
    m_message = tr("Checking the saved calibration…");
    Q_EMIT changed();
    m_config->refresh();
}

bool CalibrationSession::writePedal(const PedalSpec &pedal)
{
    if (!m_device || !m_interfaceInfo || !m_config)
        return false;

    // The curve points and deadzones straight back out of the blob. A
    // calibration has no opinion about either, and this command is
    // all-or-nothing: whatever is not copied across is whatever the device is
    // told to forget.
    std::optional<PedalRecord> record = storedRecord(*m_config, pedal.axis);
    if (!record)
        return false;

    const std::pair<qint16, qint16> endpoints = endpointsFor(pedal.axis);
    record->calibration.physicalMinimum = endpoints.first;
    record->calibration.physicalMaximum = endpoints.second;
    deriveEffectiveRange(*record);

    return writePedalRecord(m_device, this, *m_interfaceInfo, pedal, *record);
}

void CalibrationSession::onConfigRead()
{
    if (m_state != State::Verifying || !m_config)
        return;

    switch (m_config->state()) {
    case ConfigRead::State::Ready:
        if (storedMatches()) {
            finish(tr("Calibration saved successfully."),
                   false);
        } else {
            // Worth saying plainly rather than softening: the device was written
            // to and did not end up where it was asked to, which is a different
            // and worse situation than a write that failed outright.
            finish(tr("An error occurred while saving the calibration."),
                   true);
        }
        return;

    case ConfigRead::State::Failed:
        finish(tr("The calibration was saved but could not be verified. %1")
                   .arg(m_config->error()),
               true);
        return;

    case ConfigRead::State::Idle:
    case ConfigRead::State::Reading:
        return;
    }
}

bool CalibrationSession::storedMatches() const
{
    for (int i = 0; i < kPedalCount; ++i) {
        const std::optional<ConfigRead::AxisCalibration> stored = m_config->axis(kPedals[i].axis);
        if (!stored || stored->physicalMinimum != m_written[i].first
            || stored->physicalMaximum != m_written[i].second)
            return false;
    }
    return true;
}

void CalibrationSession::finish(const QString &message, bool failed)
{
    m_state = State::Idle;
    m_message = message;
    m_failed = failed;
    // Everything the run accumulated goes with it, so the next one cannot
    // inherit a thumb somebody dragged before cancelling.
    m_watching = false;
    m_travel = {};
    m_edited = {};
    Q_EMIT changed();
}

} // namespace hidcenter::simsonn_pedals
