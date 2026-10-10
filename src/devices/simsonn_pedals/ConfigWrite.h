// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

/// Writes a whole 1084-byte configuration blob back to the pedals, report `0x04`.
///
/// **Device-paced**, the opposite of the read. The host opens with `04 00`, and
/// from then on the device asks for each packet by number with `04 <n>`; the
/// host answers with `04 <n>` and that packet's slice of the blob, until it has
/// sent packet 18. A device that does not accept the blob (a firmware version
/// mismatch) says so afterwards with `04 FE`.
///
/// **Only ever the blob that was read, patched.** The blob carries far more than
/// this application edits — pin assignments, buttons, LEDs, the firmware version
/// the device checks — so the only safe thing to hand it is exactly what it gave
/// us, with the edited fields changed. ConfigRead::patchAxis() is how.
///
/// Nothing is acknowledged at the end, so a caller that wants to know what the
/// device kept reads the blob back.
class ConfigWrite : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Writing, Settling, Done, Failed };

    explicit ConfigWrite(IUsbDevice *device, QObject *parent = nullptr);

    State state() const { return m_state; }
    bool isRunning() const { return m_state == State::Writing || m_state == State::Settling; }

    /// Empty unless it failed.
    QString error() const { return m_error; }

    /// Send `blob`, which must be exactly ConfigRead::kBlobSize bytes.
    void start(const UsbInterfaceInfo &interface, const QByteArray &blob);

Q_SIGNALS:
    /// Done or Failed.
    void finished();

private Q_SLOTS:
    void onInputReport(const QByteArray &report);
    void resend();
    void expire();

private:
    void send(int packet);
    void finish(State state, const QString &error);

    QPointer<IUsbDevice> m_device;
    IUsbDeviceHidInterface *m_interface = nullptr;
    std::optional<UsbInterfaceInfo> m_interfaceInfo;

    State m_state = State::Idle;
    QString m_error;
    QByteArray m_blob;

    /// The packet last sent: 0 for the opening `04 00`, then 1..18.
    int m_sent = 0;

    QTimer *m_resend = nullptr;
    QTimer *m_deadline = nullptr;

    /// How long to listen for a refusal after the last packet.
    QTimer *m_settle = nullptr;
};

} // namespace hidcenter::simsonn_pedals
