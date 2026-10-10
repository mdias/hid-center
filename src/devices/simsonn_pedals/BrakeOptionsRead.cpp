// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BrakeOptionsRead.h"

#include <QTimer>

#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"

namespace hidcenter::simsonn_pedals {
namespace {

constexpr char kReportCommand = 0x06;

/// The same pacing as the configuration read: an unanswered question goes again
/// every 250 ms, and the pair of them gets three seconds.
constexpr int kResendIntervalMs = 250;
constexpr int kDeadlineMs = 3000;

} // namespace

BrakeOptionsRead::BrakeOptionsRead(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
{
    m_resend = new QTimer(this);
    connect(m_resend, &QTimer::timeout, this, &BrakeOptionsRead::resend);

    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, &BrakeOptionsRead::expire);
}

void BrakeOptionsRead::start(const UsbInterfaceInfo &interface)
{
    if (m_state != State::Idle)
        return;

    // Not a failure, for the reason ConfigRead gives: an offline preview has
    // nothing to answer.
    if (!m_device || m_device->isOffline() || !interface.isHid()) {
        m_error = tr("Pedals not connected: the brake's elastomer and throttle cut cannot be read.");
        Q_EMIT stateChanged();
        return;
    }

    // Kept before the open, so that refresh() can try again after an open that
    // failed.
    m_interfaceInfo = interface;
    m_interface = m_device->openHidFor(this, interface);
    if (!m_interface) {
        finish(State::Failed, m_device->lastError());
        return;
    }

    connect(m_interface, &IUsbDeviceHidInterface::inputReport, this,
            &BrakeOptionsRead::onInputReport);
    connect(m_interface, &QObject::destroyed, this, [this] {
        m_interface = nullptr;
        if (m_state == State::Reading)
            finish(State::Failed, tr("The pedals disconnected before answering."));
    });

    m_state = State::Reading;
    m_error.clear();
    m_elastomer.reset();
    m_throttleCut.reset();
    m_deadline->start(kDeadlineMs);
    Q_EMIT stateChanged();
    ask(Question::Elastomer);
}

void BrakeOptionsRead::refresh()
{
    if (m_state == State::Reading || !m_interfaceInfo)
        return;

    const UsbInterfaceInfo interface = *m_interfaceInfo;
    m_state = State::Idle;
    start(interface);
}

void BrakeOptionsRead::ask(Question question)
{
    m_question = question;
    m_resend->start(kResendIntervalMs);
    if (!m_interface)
        return;

    const char *name =
        question == Question::Elastomer ? "read_damping_type" : "readBrakeCutoff";
    if (!m_interface->write(namedCommand(name)))
        m_error = tr("Could not request %1 from the pedals: %2").arg(QLatin1String(name),
                                                               m_interface->lastError());
}

void BrakeOptionsRead::resend()
{
    if (m_state == State::Reading)
        ask(m_question);
}

void BrakeOptionsRead::onInputReport(const QByteArray &report)
{
    if (m_state != State::Reading || report.size() < 8 || report.at(0) != kReportCommand)
        return;

    if (m_question == Question::Elastomer) {
        if (report.at(1) != 'O' || report.at(2) != 'K')
            return;
        const char type = report.at(3);
        if (type != 's' && type != 'm' && type != 'h')
            return;
        m_elastomer = Elastomer(type);
        ask(Question::ThrottleCut);
        return;
    }

    if (report.at(1) != 'B' || report.at(2) != 'C' || report.at(3) != 'O' || report.at(4) != 'K')
        return;

    ThrottleCut cut;
    cut.enabled = report.at(5) == 1;
    cut.tenths = int(quint16(quint8(report.at(6))) | quint16(quint8(report.at(7))) << 8);
    m_throttleCut = cut;

    qCInfo(hidcenterDevice).noquote()
        << QStringLiteral("SIMSONN: brake elastomer '%1', throttle cut %2 at %3 %")
               .arg(QChar(char(*m_elastomer)))
               .arg(cut.enabled ? QStringLiteral("on") : QStringLiteral("off"))
               .arg(cut.tenths / 10.0, 0, 'f', 1);
    finish(State::Ready, QString());
}

void BrakeOptionsRead::expire()
{
    if (m_state != State::Reading)
        return;
    finish(State::Failed,
           m_error.isEmpty() ? tr("The pedals did not report the elastomer or throttle cut.")
                             : m_error);
}

void BrakeOptionsRead::finish(State state, const QString &error)
{
    m_state = state;
    m_error = error;
    m_resend->stop();
    m_deadline->stop();

    if (m_device)
        m_device->releaseAllFor(this);
    if (m_interface)
        disconnect(m_interface, nullptr, this, nullptr);
    m_interface = nullptr;

    Q_EMIT stateChanged();
}

std::optional<Elastomer> BrakeOptionsRead::elastomer() const
{
    return m_state == State::Ready ? m_elastomer : std::nullopt;
}

std::optional<ThrottleCut> BrakeOptionsRead::throttleCut() const
{
    return m_state == State::Ready ? m_throttleCut : std::nullopt;
}

} // namespace hidcenter::simsonn_pedals
