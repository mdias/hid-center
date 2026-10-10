// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsSave.h"

#include "BrakeOptionsRead.h"
#include "ConfigWrite.h"

#include "core/usb/IUsbDevice.h"

namespace hidcenter::simsonn_pedals {
namespace {

bool sameCalibration(const ConfigRead::AxisCalibration &a, const ConfigRead::AxisCalibration &b)
{
    return a.physicalMinimum == b.physicalMinimum && a.physicalMaximum == b.physicalMaximum
           && a.effectiveMinimum == b.effectiveMinimum
           && a.effectiveCentre == b.effectiveCentre
           && a.effectiveMaximum == b.effectiveMaximum && a.startDeadzone == b.startDeadzone
           && a.endDeadzone == b.endDeadzone;
}

} // namespace

SettingsSave::SettingsSave(IUsbDevice *device, ConfigRead *config,
                           BrakeOptionsRead *brakeOptions, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_config(config)
    , m_brakeOptions(brakeOptions)
{
    m_blobWrite = new ConfigWrite(device, this);
    connect(m_blobWrite, &ConfigWrite::finished, this, &SettingsSave::onBlobWritten);

    if (m_config)
        connect(m_config, &ConfigRead::stateChanged, this, &SettingsSave::onReadBack);
    if (m_brakeOptions)
        connect(m_brakeOptions, &BrakeOptionsRead::stateChanged, this, &SettingsSave::onReadBack);
}

std::optional<SettingsSave::Settings> SettingsSave::stored() const
{
    if (!m_config || m_config->state() != ConfigRead::State::Ready)
        return {};

    Settings settings;
    for (int i = 0; i < kPedalCount; ++i) {
        const std::optional<PedalRecord> record = storedRecord(*m_config, kPedals[i].axis);
        const std::optional<int> filter = m_config->filter(kPedals[i].axis);
        if (!record || !filter)
            return {};
        settings.pedals[i].curve = record->curve;
        settings.pedals[i].startDeadzone = record->calibration.startDeadzone;
        settings.pedals[i].endDeadzone = record->calibration.endDeadzone;
        settings.pedals[i].filter = quint8(*filter);
        if (i == kBrakePedal)
            settings.brake.forceKg = brakeForceKg(record->calibration);
    }

    if (m_brakeOptions) {
        settings.brake.elastomer = m_brakeOptions->elastomer();
        settings.brake.throttleCut = m_brakeOptions->throttleCut();
    }
    return settings;
}

void SettingsSave::save(const UsbInterfaceInfo &interface, const Settings &settings)
{
    if (isRunning())
        return;

    // Everything that is not being edited goes back as the device has it, and
    // there is nowhere to get that but the blob.
    const std::optional<Settings> before = stored();
    if (!m_device || !before) {
        finish(tr("Cannot save: the pedals' settings have not been read."),
               true);
        return;
    }

    m_interface = interface;
    m_written = settings;
    m_before = *before;
    m_failed = false;

    // Settings that could not be read are not written either: there is nothing
    // to say they would be a change, and nothing to check them against after.
    if (!m_before.brake.elastomer)
        m_written.brake.elastomer.reset();
    if (!m_before.brake.throttleCut)
        m_written.brake.throttleCut.reset();

    bool filterChanged = false;
    for (int i = 0; i < kPedalCount; ++i) {
        const PedalSpec &pedal = kPedals[i];
        const PedalSettings &wanted = m_written.pedals[i];
        std::optional<PedalRecord> record = storedRecord(*m_config, pedal.axis);
        if (!record) {
            finish(tr("Cannot save: the pedals hold no settings for the %1.")
                       .arg(tr(pedal.title).toLower()),
                   true);
            return;
        }

        ConfigRead::AxisCalibration &calibration = record->calibration;
        const bool deadzonesMoved = calibration.startDeadzone != wanted.startDeadzone
                                    || calibration.endDeadzone != wanted.endDeadzone;
        record->curve = wanted.curve;
        calibration.startDeadzone = wanted.startDeadzone;
        calibration.endDeadzone = wanted.endDeadzone;

        bool rangeMoved = false;
        if (i == kBrakePedal
            && (m_written.brake.forceKg != m_before.brake.forceKg
                || wanted.startDeadzone != m_before.pedals[i].startDeadzone)) {
            calibration.physicalMaximum = brakePhysicalMaximum(
                m_written.brake.forceKg, calibration.physicalMinimum, wanted.startDeadzone);
            rangeMoved = true;
        }
        if (deadzonesMoved || rangeMoved)
            deriveEffectiveRange(*record);

        m_records[i] = record;
        filterChanged = filterChanged || wanted.filter != m_before.pedals[i].filter;
    }

    if (!filterChanged) {
        sendCommands();
        return;
    }

    QByteArray blob = m_config->blob();
    for (int i = 0; i < kPedalCount; ++i) {
        ConfigRead::patchAxis(blob, kPedals[i].axis, m_records[i]->curve,
                              m_records[i]->calibration, m_written.pedals[i].filter);
    }

    m_state = State::WritingBlob;
    m_message = tr("Saving the settings…");
    Q_EMIT changed();
    m_blobWrite->start(interface, blob);
}

void SettingsSave::onBlobWritten()
{
    if (m_state != State::WritingBlob)
        return;

    if (m_blobWrite->state() != ConfigWrite::State::Done) {
        // A write that stopped part way may have left some packets behind, so
        // the device is read again either way: the page should end up showing
        // what the pedals hold, not what was hoped for.
        finish(tr("Could not save the settings: %1").arg(m_blobWrite->error()), true);
        if (m_config)
            m_config->refresh();
        return;
    }

    sendCommands();
}

void SettingsSave::sendCommands()
{
    const std::optional<PedalRecord> brakeBefore =
        storedRecord(*m_config, kPedals[kBrakePedal].axis);

    auto fail = [this](const QString &what) {
        // Commands before this one may already have landed; see onBlobWritten().
        finish(tr("Could not save %1: %2")
                   .arg(what, m_device ? m_device->lastError() : QString()),
               true);
        if (m_config)
            m_config->refresh();
        if (m_brakeOptions)
            m_brakeOptions->refresh();
    };

    for (int i = 0; i < kPedalCount; ++i) {
        const PedalSpec &pedal = kPedals[i];
        const std::optional<PedalRecord> stored = storedRecord(*m_config, pedal.axis);
        const PedalRecord &record = *m_records[i];
        if (stored && stored->curve == record.curve
            && sameCalibration(stored->calibration, record.calibration))
            continue;

        if (!writePedalRecord(m_device, this, *m_interface, pedal, record)) {
            fail(tr("the %1 settings").arg(tr(pedal.title).toLower()));
            return;
        }
    }

    const ConfigRead::AxisCalibration &brake = m_records[kBrakePedal]->calibration;

    // The same pair the `b_pf` just carried. It goes out every time the force
    // changes, and what else the firmware does on hearing
    // it is not known — so it is sent rather than second-guessed.
    if (brakeBefore
        && (brake.physicalMaximum != brakeBefore->calibration.physicalMaximum
            || brake.effectiveMaximum != brakeBefore->calibration.effectiveMaximum)) {
        if (!sendNamedCommand(m_device, this, *m_interface, "damping_max",
                              brakeForcePayload(brake))) {
            fail(tr("the maximum brake force"));
            return;
        }
    }

    if (m_written.brake.elastomer && m_written.brake.elastomer != m_before.brake.elastomer) {
        if (!sendNamedCommand(
                m_device, this, *m_interface, "damping_type",
                elastomerPayload(*m_written.brake.elastomer, m_written.brake.forceKg, brake))) {
            fail(tr("the elastomer"));
            return;
        }
    }

    if (m_written.brake.throttleCut && m_written.brake.throttleCut != m_before.brake.throttleCut) {
        if (!sendNamedCommand(m_device, this, *m_interface, "brakeCutoff",
                              throttleCutPayload(*m_written.brake.throttleCut))) {
            fail(tr("the throttle cut"));
            return;
        }
    }

    verify();
}

void SettingsSave::verify()
{
    m_state = State::Verifying;
    m_message = tr("Checking the saved settings…");
    Q_EMIT changed();

    m_config->refresh();
    if (m_brakeOptions && (m_written.brake.elastomer || m_written.brake.throttleCut))
        m_brakeOptions->refresh();
}

void SettingsSave::onReadBack()
{
    if (m_state != State::Verifying || !m_config)
        return;

    // Both reads have to be in before anything can be said.
    const bool brakeWanted = m_written.brake.elastomer || m_written.brake.throttleCut;
    if (m_config->state() == ConfigRead::State::Reading
        || (brakeWanted && m_brakeOptions
            && m_brakeOptions->state() == BrakeOptionsRead::State::Reading))
        return;

    if (m_config->state() != ConfigRead::State::Ready
        || (brakeWanted && m_brakeOptions
            && m_brakeOptions->state() != BrakeOptionsRead::State::Ready)) {
        const QString error = m_config->state() != ConfigRead::State::Ready
                                  ? m_config->error()
                                  : m_brakeOptions->error();
        finish(tr("The settings were saved but could not be verified. %1")
                   .arg(error),
               true);
        return;
    }

    if (stored() == m_written) {
        finish(tr("Settings saved."),
               false);
    } else {
        finish(tr("An error occurred while saving the settings. The controls now show what "
                  "the pedals hold."),
               true);
    }
}

void SettingsSave::finish(const QString &message, bool failed)
{
    m_state = State::Idle;
    m_message = message;
    m_failed = failed;
    Q_EMIT changed();
}

} // namespace hidcenter::simsonn_pedals
