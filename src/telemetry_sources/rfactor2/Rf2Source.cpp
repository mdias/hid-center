// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Rf2Source.h"

#include "ProtocolRf2.h"

#include "core/Logging.h"

#include <QHostAddress>
#include <QUdpSocket>

#include <cstring>

namespace hidcenter::rfactor2 {
namespace {

/// Whole degrees Celsius from the Kelvin the sim's temperature fields carry.
int kelvinToCelsius(double kelvin)
{
    return int(qRound(kelvin - 273.15));
}

/// The range an optimal tyre temperature has to land in to be believed.
///
/// mOptimalTemp is the one field read here with no unit comment in the vendor
/// header, so it is taken as Kelvin like its neighbours and then sanity-checked
/// rather than trusted. A range, not a `> 0` test: a sender filling this with
/// something we did not expect should read as silence — which the readout
/// answers with kTelemetryOptimalTyreTempC — rather than as a scale no tyre can
/// reach.
constexpr int kOptimalTyreTempMinC = 20;
constexpr int kOptimalTyreTempMaxC = 200;

QString describeEndpoint()
{
    return QStringLiteral("127.0.0.1:%1").arg(rf2::kRf2ListenPort);
}

/// One decoded packet, as the application sees it.
///
/// Composed fresh rather than read-modify-written from whatever was published
/// last: every field of a TelemetryFrame is filled in here, so there is nothing
/// to carry over, and building from nothing is one fewer thing to get wrong when
/// a field is added.
TelemetryFrame toFrame(const rf2::TelemInfoV01 &info)
{
    TelemetryFrame frame;

    // The true figure, with no ceiling of our own: an engine bouncing off its
    // limiter is briefly past mEngineMaxRPM, and rpmPercent() is where that
    // becomes a pinned bar rather than here where it would become a lost fact.
    frame.rpm = qMax(0, int(qRound(info.mEngineRPM)));

    // Zero is what the sim reports from menus, replays and anything else with no
    // car loaded — a frequent, ordinary case, not a corrupt packet.
    const int limiter = int(qRound(info.mEngineMaxRPM));
    frame.rpmMax = limiter > 0 ? limiter : kTelemetryRpmMax;
    frame.tractionControl = info.mTCActive;
    frame.abs = info.mABSActive;
    frame.pitLimiter = info.mSpeedLimiterActive;

    // No flag: this datagram is TelemInfoV01 alone, and the flag booleans live in
    // the scoring struct, which nothing here reads yet.

    for (int i = 0; i < TelemetryWheelCount; ++i) {
        const rf2::TelemWheelV01 &wheel = info.mWheel[i];
        TelemetryWheel &corner = frame.wheels[i];

        // Celsius already, on the vendor header's word — the one temperature in
        // this struct that is not Kelvin.
        corner.brakeTemp = kelvinToCelsius(wheel.mBrakeTemp);

        for (int band = 0; band < int(corner.tyreTemp.size()); ++band)
            corner.tyreTemp[band] = kelvinToCelsius(wheel.mTemperature[band]);

        const int optimal = kelvinToCelsius(wheel.mOptimalTemp);
        corner.optimalTyreTemp
            = optimal >= kOptimalTyreTempMinC && optimal <= kOptimalTyreTempMaxC ? optimal : 0;
    }

    return frame;
}

} // namespace

Rf2Source::Rf2Source(QObject *parent)
    : ITelemetrySource(parent)
{
}

void Rf2Source::start()
{
    if (m_socket)
        return;

    m_socket = new QUdpSocket(this);

    if (!m_socket->bind(QHostAddress::LocalHost, rf2::kRf2ListenPort)) {
        // Not fatal, and deliberately so: something else holding the port is a
        // reason to have no telemetry, not a reason to refuse to start.
        const QString reason = tr("Could not listen on %1 — %2")
                                   .arg(describeEndpoint(), m_socket->errorString());

        qCWarning(hidcenterTelemetry).noquote() << reason;

        delete m_socket;
        m_socket = nullptr;
        Q_EMIT availabilityChanged(false, reason);
        return;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &Rf2Source::readPendingDatagrams);

    qCInfo(hidcenterTelemetry).noquote()
        << QStringLiteral("Listening for rFactor 2 telemetry on %1, expecting %2-byte datagrams.")
               .arg(describeEndpoint())
               .arg(rf2::kRf2DatagramSize);

    Q_EMIT availabilityChanged(true, QString());
}

void Rf2Source::stop()
{
    if (!m_socket)
        return;

    delete m_socket;
    m_socket = nullptr;
    m_rejectedSize = -1;

    qCInfo(hidcenterTelemetry) << "Stopped listening for rFactor 2 telemetry.";

    Q_EMIT availabilityChanged(false, tr("Not listening."));
}

void Rf2Source::readPendingDatagrams()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray datagram(int(m_socket->pendingDatagramSize()), Qt::Uninitialized);
        const qint64 read = m_socket->readDatagram(datagram.data(), datagram.size());
        if (read < 0)
            return;

        if (read != rf2::kRf2DatagramSize) {
            // Once per distinct wrong size. A sender built against a different
            // version of the struct is wrong on every packet, and one line each
            // would drown the log — but the first line is exactly what tells you
            // the two ends disagree, and by how much.
            if (read != m_rejectedSize) {
                m_rejectedSize = read;
                qCWarning(hidcenterTelemetry).noquote()
                    << "Ignoring a" << read << "byte telemetry datagram;" << rf2::kRf2DatagramSize
                    << "expected. The sender's TelemInfoV01 does not match ours.";
            }
            continue;
        }

        // Copied out rather than cast in place: the datagram buffer carries no
        // alignment guarantee for the doubles inside, so a cast would be
        // undefined even where it happens to work.
        rf2::TelemInfoV01 info;
        std::memcpy(&info, datagram.constData() + sizeof(rf2::Rf2PacketHeader), sizeof(info));

        // The header is all zeroes for now and is not inspected. When it grows a
        // magic number, this is where a packet that is not ours gets dropped.

        // Every accepted packet, whether or not the values moved: the router
        // reads these as traffic, and a car at a steady idle is still a car
        // someone is driving.
        Q_EMIT frameReady(toFrame(info));
    }
}

} // namespace hidcenter::rfactor2
