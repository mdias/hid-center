// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include "AlphaEvoProtocol.h"
#include "AlphaEvoSpecs.h"
#include "AlphaEvoWheelConfig.h"
#include "SimagicLeds.h"
#include "SimagicTelemetry.h"

#include <array>

class QTimer;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
class IUsbDeviceInterface;
} // namespace hidcenter

namespace hidcenter::simagic_alpha_evo {

/// Every transfer with the wheelbase, on its own thread.
///
/// The ioctls behind these calls are synchronous, and the kernel can hold one
/// for as long as the base takes to answer, so none of them may run on the GUI
/// thread — a slow write would freeze the window.
///
/// Living on one worker thread also serialises them for free: the thread's event
/// loop runs one slot to completion before starting the next, so a poll can
/// never land between the read and the write of a read-modify-write, and nothing
/// in here needs a mutex.
///
/// Owned by AlphaEvoSettings, which is the only thing that talks to it and does
/// so entirely through queued invocations. Nothing here touches the UI.
class AlphaEvoIo : public QObject
{
    Q_OBJECT

public:
    /// Every handle stays owned by the IUsbDevice on the GUI thread; opening and
    /// closing them is not this class's business, only transfers are.
    ///
    /// **This is why the handles are passed in rather than asked for.** Opening
    /// one reaches into bookkeeping the device keeps on the GUI thread and
    /// reparents a QObject onto it, neither of which a worker may do. So
    /// AlphaEvoSettings opens what this class will need, before the thread it
    /// runs on exists, and hands the results over.
    ///
    /// Three of them, because one interface is no longer the whole device.
    /// `hidInterface` is where the configuration protocol lives — the settings,
    /// the status reads and the telemetry reports. `ledInterface` carries the
    /// ring's endpoint and is a different interface of the same base; it may be
    /// null, on a base that has no such endpoint or where it could not be
    /// claimed. `device` is the base itself, which is what reading anything
    /// about its other interfaces goes through, and what a future protocol on
    /// one of them would be opened for — from the GUI thread, as above.
    ///
    /// `deviceModel` is the BaseId byte the LED protocol puts in its header. It
    /// is fixed for the lifetime of the device, so it is settled once here
    /// rather than sent with every packet.
    explicit AlphaEvoIo(IUsbDevice *device, IUsbDeviceHidInterface *hidInterface,
                        IUsbDeviceInterface *ledInterface, quint8 deviceModel,
                        QObject *parent = nullptr);

public Q_SLOTS:
    /// Start re-reading the status on a timer. The timer is created here so it
    /// belongs to this thread. Stopping it is the thread's own shutdown.
    void startPolling(int intervalMs);

    /// While set, requestApply() logs what it would have sent and touches
    /// nothing. Copied in rather than read from the GUI thread, which also means
    /// it cannot change halfway through a batch.
    void setDryRun(bool dryRun);

    /// Write these settings: re-read the status, compose the blocks the changes
    /// touch, send them, and read back what the base actually took.
    void requestApply(const hidcenter::simagic_alpha_evo::SettingValues &changes);

    /// Send one telemetry report. Called at the module's telemetry rate — sixty
    /// times a second by default — so unlike requestApply() it neither reads
    /// first nor reads back: there is nothing on the base to preserve, and a
    /// round trip per report would cost more than the report is worth.
    ///
    /// Honours the dry run, but not through writeBlock(): that logs every send,
    /// and sixty lines a second would bury the log. See the .cpp.
    void sendTelemetry(const hidcenter::simagic_alpha_evo::SimagicTelemetry &data);

    /// Set the wheel's LED ring to these colours: one frame of whatever the
    /// animator is showing, or simply the colours, if it is showing something
    /// still.
    ///
    /// Composes the packet and sends it as one or more reports — five stops or
    /// more need two. Like sendTelemetry() it neither reads first nor reads
    /// back: the status report has no LED fields, so there is nothing on the
    /// base to preserve and nothing to confirm against.
    ///
    /// **Always answers with ledFrameSent(), on every path out**, including a
    /// dry run and including a failure. The animator composes no further frame
    /// until it hears that, so one missed acknowledgement stops the ring for
    /// good.
    ///
    /// Also restarts the refresh below, so a keepalive never lands immediately
    /// behind a frame that already said the same thing.
    void sendLedFrame(const hidcenter::simagic_alpha_evo::LedRingColours &colours);

    /// Send the colours again, unchanged.
    ///
    /// The base forgets them after kLedForgetTimeoutMs, so they only stay put
    /// while something keeps restating them. An animation is already restating
    /// them faster than that, so this is what holds a *still* mode lit — and
    /// what takes over the moment one stops moving.
    ///
    /// Silent: it resends bytes already logged, so it says nothing unless the
    /// outcome changes. Does nothing before the first sendLedFrame(), and
    /// nothing in a dry run, where there is no colour on the wheel to keep.
    void refreshLedColours();

    /// Send one report of the steering wheel's configuration: `chunk` 0 to 3 of
    /// `config`.
    ///
    /// One report, not all four for every change: the caller knows
    /// which part of the block it changed, and the rest is only the same bytes
    /// again. Like the ring it neither reads first nor reads back, because the
    /// base has no copy of this block to give.
    ///
    /// Honours the dry run. Answers through wheelConfigSent(), on the edge only.
    void sendWheelConfigReport(const hidcenter::simagic_alpha_evo::WheelConfig &config, int chunk);

Q_SIGNALS:
    /// A status was read. Emitted for every successful read, including the ones
    /// that changed nothing; deciding what is new is the façade's job.
    ///
    /// Carries both reports. The live half may be absent — see StatusSnapshot —
    /// and that is not a failure, so this still fires.
    void statusRead(const hidcenter::simagic_alpha_evo::StatusSnapshot &snapshot);

    /// A read failed. Emitted once per run of failures, not once per poll.
    ///
    /// About report 0x81 only. Losing the live telemetry is reported to the log
    /// and shows on the page as empty fields, but it is not a read failure: the
    /// settings still arrived, and everything that depends on them still works.
    void readFailed(const QString &error);

    void applyFinished(bool ok, const QString &error, int blocksWritten, bool dryRun);

    /// LED writes started failing, or started working again.
    ///
    /// **The edge, not every write.** Frames arrive at up to twenty-five a
    /// second and nearly all of them succeed, so a signal per write would be a
    /// signal the UI could only throw away. This speaks when the answer changes,
    /// which is the only time the page has anything new to say.
    ///
    /// Both counts are reported because a multi-report packet can fail halfway,
    /// and half a packet is not the same as none of it: the wheel has been left
    /// holding a fragment of a message with no way for us to read back what it
    /// made of it. `framesSent == 0` means nothing left the host.
    void ledColoursSent(bool ok, const QString &error, int framesSent, int framesTotal,
                        bool dryRun);

    /// One frame was dealt with, however it went. See sendLedFrame().
    void ledFrameSent();

    /// Wheel configuration writes started failing, or started working again.
    /// The edge, not every write, for the reason ledColoursSent() gives: a
    /// dragged slider sends several a second.
    void wheelConfigSent(bool ok, const QString &error, bool dryRun);

private Q_SLOTS:
    void poll();

private:
    /// The one and only gate on writing. Every path that changes a setting goes
    /// through requestApply(), and requestApply() writes only through here.
    bool writeBlock(const QByteArray &block, const QString &description, QString *error);

    /// Compose and send m_ledColours. The one place LED bytes leave the host.
    ///
    /// `announce` is what separates a user's edit from a keepalive. Announced,
    /// it logs and reports back either way. Unannounced, it is silent while
    /// nothing changes and speaks only when writes start failing or recover —
    /// otherwise a line every three seconds, forever, would be the log.
    void writeLedColours(bool announce);

    /// The device, the interface the configuration protocol answers on, and the
    /// interface carrying the LED ring's endpoint. See the constructor for why
    /// all three are held, and why none of them is opened here.
    IUsbDevice *m_device = nullptr;
    IUsbDeviceHidInterface *m_hidInterface = nullptr;
    IUsbDeviceInterface *m_ledInterface = nullptr;

    quint8 m_deviceModel = 0;  ///< BaseId byte for the LED packet header.
    QTimer *m_pollTimer = nullptr;
    bool m_dryRun = false;  ///< Matches AlphaEvoSettings; see the note there.
    bool m_readFailing = false;

    /// The same once-per-run latch as m_readFailing, for the live telemetry
    /// report. Only the *logging* is latched, never the asking: a base that
    /// stalls report 0x23 costs one failed ioctl every poll, which is cheap, and
    /// asking every time is what lets a transient failure heal itself instead of
    /// leaving the page's fields dead until the app is restarted.
    bool m_liveReadFailing = false;

    /// When the last telemetry report went out. A gap in it is what marks the
    /// start of a new stream, and the start of a stream is the one report worth
    /// logging: at sixty a second, a line per send is not a log.
    QElapsedTimer m_lastTelemetrySend;

    /// Once per run of failures, exactly as m_readFailing does for reads.
    bool m_sendFailing = false;

    /// The same idea as m_lastTelemetrySend, for LED writes. Dragging the colour
    /// count through its range is a burst of packets and one log line each would
    /// be noise, so a burst logs its first packet and counts the rest.
    QElapsedTimer m_lastLedSend;
    int m_ledSendsSuppressed = 0;
    bool m_ledSendFailing = false;

    /// What the wheel was last told to show, and whether it has been told
    /// anything yet. Held here rather than read back from the page because the
    /// refresh has to work whatever the UI is doing, and because the base has no
    /// LED state to read back from in the first place.
    LedRingColours m_ledColours;
    bool m_haveLedColours = false;

    /// Restates m_ledColours before the base forgets them. Created on the first
    /// send, so like m_pollTimer it belongs to this thread rather than to
    /// whoever constructed the object.
    QTimer *m_ledRefreshTimer = nullptr;

    /// Burst logging and the failure latch for wheel configuration writes, the
    /// same pair the LED writes keep. Bursts are counted per report, so the
    /// brightness sent at start-up does not swallow the log line of the colours
    /// sent a moment after it.
    std::array<QElapsedTimer, kWheelConfigChunkCount> m_lastWheelConfigSend;
    std::array<int, kWheelConfigChunkCount> m_wheelConfigSendsSuppressed{};
    bool m_wheelConfigSendFailing = false;
};

} // namespace hidcenter::simagic_alpha_evo
