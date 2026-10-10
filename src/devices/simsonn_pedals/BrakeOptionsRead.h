// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <optional>

#include "PedalWrite.h"
#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
} // namespace hidcenter

namespace hidcenter::simsonn_pedals {

/// Reads the two brake settings the configuration blob does not carry: which
/// elastomer is fitted, and the brake point that cuts the throttle.
///
/// Both are kept by the device outside the blob and asked for by name, exactly
/// on every connect: `read_damping_type`, answered
/// `06 'O' 'K' <s|m|h>`, then `readBrakeCutoff`, answered
/// `06 'B' 'C' 'O' 'K' <enabled> <tenths, uint16 LE>`.
///
/// **One question at a time.** The elastomer's answer carries no name of its
/// own — an `OK` and a letter — so it can be told from the other
/// `OK` replies only by which question was last asked. So nothing else is asked
/// until it is answered, and the second question goes out only after that.
class BrakeOptionsRead : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Reading, Ready, Failed };

    explicit BrakeOptionsRead(IUsbDevice *device, QObject *parent = nullptr);

    /// Ask. Does nothing once a read has been started; refresh() asks again.
    void start(const UsbInterfaceInfo &interface);
    void refresh();

    State state() const { return m_state; }
    QString error() const { return m_error; }

    /// Valid in the Ready state.
    std::optional<Elastomer> elastomer() const;
    std::optional<ThrottleCut> throttleCut() const;

Q_SIGNALS:
    void stateChanged();

private Q_SLOTS:
    void onInputReport(const QByteArray &report);
    void resend();
    void expire();

private:
    enum class Question { Elastomer, ThrottleCut };

    void ask(Question question);
    void finish(State state, const QString &error);

    QPointer<IUsbDevice> m_device;
    IUsbDeviceHidInterface *m_interface = nullptr;
    std::optional<UsbInterfaceInfo> m_interfaceInfo;

    State m_state = State::Idle;
    QString m_error;
    Question m_question = Question::Elastomer;

    std::optional<Elastomer> m_elastomer;
    std::optional<ThrottleCut> m_throttleCut;

    QTimer *m_resend = nullptr;
    QTimer *m_deadline = nullptr;
};

} // namespace hidcenter::simsonn_pedals
