// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <array>
#include <optional>

#include "ConfigRead.h"
#include "PedalWrite.h"
#include "Pedals.h"
#include "core/usb/UsbDeviceInfo.h"

namespace hidcenter {
class IUsbDevice;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

class BrakeOptionsRead;
class ConfigWrite;

/// Writing the Main Settings page to the pedals.
///
/// **Three ways in, used in this order.**
///
/// 1. **The whole blob**, report `0x04`, only when a filter level changed: that
///    setting lives nowhere but the blob, which is how the device stores it.
///    The blob sent is the one read, with every edited field of every
///    pedal patched in, so it agrees with the commands that follow.
/// 2. **One `<letter>_pf` per pedal** whose curve, deadzones or — on the brake —
///    maximum force differ from what the device holds. That command carries the
///    pedal's whole state, so the calibration goes back unchanged, except on
///    the brake, whose top of travel *is* its maximum force.
/// 3. **The brake's named settings**: `damping_max` when its force moved,
///    `damping_type` when the elastomer changed, `brakeCutoff` when the throttle
///    cut did — each only when needed.
///
/// **Effective range.** Re-derived only for a pedal whose deadzones or physical
/// range moved; one whose curve alone changed keeps the trio the device already
/// has, for the reason CalibrationSession gives about axes whose range predates
/// their deadzones.
///
/// **The brake keeps its force in kilograms.** Moving its start deadzone moves
/// where full output falls, so the top of travel is recomputed to
/// keep the force figure where it was.
///
/// **The write is checked.** Nothing written here is acknowledged, so both reads
/// are run again afterwards and compared against what went out.
///
/// Owned by the device rather than by the page, like CalibrationSession, so that
/// a tab closed in the middle of the read-back does not lose the answer.
class SettingsSave : public QObject
{
    Q_OBJECT

public:
    /// Idle covers both "never run" and "finished"; message() says which.
    enum class State { Idle, WritingBlob, Verifying };

    /// One pedal's editable settings, as the device stores them.
    struct PedalSettings
    {
        std::array<qint8, ConfigRead::kCurvePoints> curve {};
        quint8 startDeadzone = 0;  ///< start deadzone size.
        quint8 endDeadzone = 0;    ///< end deadzone size, measured inward from the top.
        quint8 filter = 0;         ///< `Filter`, 0..ConfigRead::kFilterMaximum.

        bool operator==(const PedalSettings &) const = default;
    };

    /// The settings only the brake has.
    struct BrakeSettings
    {
        int forceKg = 0;

        /// Nothing when they could not be read, in which case they are neither
        /// shown as editable nor written.
        std::optional<Elastomer> elastomer;
        std::optional<ThrottleCut> throttleCut;

        bool operator==(const BrakeSettings &) const = default;
    };

    struct Settings
    {
        std::array<PedalSettings, kPedalCount> pedals {};
        BrakeSettings brake;

        bool operator==(const Settings &) const = default;
    };

    SettingsSave(IUsbDevice *device, ConfigRead *config, BrakeOptionsRead *brakeOptions,
                 QObject *parent = nullptr);

    State state() const { return m_state; }
    bool isRunning() const { return m_state != State::Idle; }

    /// What just happened, in one sentence. Empty when there is nothing to say.
    QString message() const { return m_message; }

    /// True when message() is a complaint rather than news.
    bool failed() const { return m_failed; }

    /// What the device holds, or nothing before the blob has been read. The
    /// brake's elastomer and throttle cut are nothing until their own read is in.
    std::optional<Settings> stored() const;

    /// Write whatever differs from stored(), then read everything back and check.
    void save(const UsbInterfaceInfo &interface, const Settings &settings);

Q_SIGNALS:
    /// The state or the message changed.
    void changed();

private Q_SLOTS:
    void onBlobWritten();
    void onReadBack();

private:
    /// Steps 2 and 3, and then the read-back.
    void sendCommands();
    void verify();
    void finish(const QString &message, bool failed);

    QPointer<IUsbDevice> m_device;
    QPointer<ConfigRead> m_config;
    QPointer<BrakeOptionsRead> m_brakeOptions;
    ConfigWrite *m_blobWrite = nullptr;

    State m_state = State::Idle;
    QString m_message;
    bool m_failed = false;

    std::optional<UsbInterfaceInfo> m_interface;

    /// What save() is writing, kept for the commands that follow the blob and
    /// for the read-back to be compared against.
    Settings m_written {};
    Settings m_before {};
    std::array<std::optional<PedalRecord>, kPedalCount> m_records {};
};

} // namespace hidcenter::simsonn_pedals
