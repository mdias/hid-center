// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/telemetry/plugin/ITelemetrySource.h"

class QUdpSocket;

namespace hidcenter::rfactor2 {

/// Listens for rFactor 2 / Le Mans Ultimate telemetry datagrams and turns them
/// into TelemetryFrames.
///
/// The wire format is in ProtocolRf2.h — sixteen reserved bytes followed by one
/// TelemInfoV01, which is the struct the sim hands its own plugins. Both the
/// format and the decoding live in this module and nowhere else: that is the
/// whole point of a source being a plugin, and it is why nothing in hidcenter-core
/// compiles a 300-line port of a vendor header any more.
///
/// Lives on the thread TelemetrySourceModule gives it. It holds no pointer to
/// the hub and cannot reach one — see ITelemetrySource.
class Rf2Source : public ITelemetrySource
{
    Q_OBJECT

public:
    explicit Rf2Source(QObject *parent = nullptr);

    void start() override;
    void stop() override;

private:
    void readPendingDatagrams();

    QUdpSocket *m_socket = nullptr;

    /// The last wrong datagram length complained about. A sender whose struct
    /// does not match ours is wrong 90 times a second, and one log line per
    /// packet would bury everything else in the window.
    qint64 m_rejectedSize = -1;
};

} // namespace hidcenter::rfactor2
