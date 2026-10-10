// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoIo.h"

#include "AlphaEvoBlocks.h"
#include "AlphaEvoLedProtocol.h"
#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceHidInterface.h"

#include <QStringList>
#include <QTimer>

#include <cstring>

namespace hidcenter::simagic_alpha_evo {
namespace {

QString toHex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' '));
}

/// Longer than a gap between reports at the slowest rate offered (20 Hz, so
/// 50 ms), short enough that stopping and restarting telemetry reads as two
/// streams rather than one.
constexpr int kTelemetryGapMs = 1000;

/// What counts as a gap between LED writes. Longer than the page's debounce, so
/// a continuous drag on the colour count reads as one burst, and short enough
/// that going back to the page later logs afresh.
constexpr int kLedGapMs = 1000;

/// The same, for wheel configuration writes. Longer than the controller's
/// throttle, so a slider held and dragged reads as one burst.
constexpr int kWheelConfigGapMs = 1000;

} // namespace

AlphaEvoIo::AlphaEvoIo(IUsbDevice *device, IUsbDeviceHidInterface *hidInterface,
                       IUsbDeviceInterface *ledInterface, quint8 deviceModel, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_hidInterface(hidInterface)
    , m_ledInterface(ledInterface)
    , m_deviceModel(deviceModel)
{
}

void AlphaEvoIo::startPolling(int intervalMs)
{
    if (!m_pollTimer) {
        // Created here rather than in the constructor: the constructor runs on
        // the GUI thread, and a timer belongs to the thread that started it.
        m_pollTimer = new QTimer(this);
        connect(m_pollTimer, &QTimer::timeout, this, &AlphaEvoIo::poll);
    }
    m_pollTimer->start(intervalMs);
}

void AlphaEvoIo::setDryRun(bool dryRun)
{
    if (m_dryRun == dryRun)
        return;

    m_dryRun = dryRun;

    // Treat the change as the start of a new telemetry stream, so the next
    // report logs a line under the new rule. Without this the log's last word on
    // telemetry stays "would send" while real reports are going to the base.
    m_lastTelemetrySend.invalidate();

    // Same reasoning for LED writes, which log per burst rather than per send.
    m_lastLedSend.invalidate();

    // Coming out of a dry run with colours already chosen: light the ring now
    // and say so. An animation would get there on its own next frame, but a
    // still mode would wait up to three seconds for the refresh, through a path
    // that logs nothing — leaving "would send" as the log's last word on LEDs
    // while the wheel is lit.
    //
    // Straight to the write rather than through sendLedFrame(), which would
    // acknowledge a frame the animator never sent and let one extra past its
    // gate.
    if (!m_dryRun && m_haveLedColours)
        writeLedColours(/*announce=*/true);
}

void AlphaEvoIo::poll()
{
    StatusSnapshot fresh;
    QString error;
    QString liveError;
    if (!readStatus(m_hidInterface, fresh, &error, /*readLive=*/true, &liveError)) {
        // Once per run of failures, not once every 500 ms — a device that has
        // stopped answering would otherwise bury everything else in the log.
        if (!m_readFailing) {
            m_readFailing = true;
            qCWarning(hidcenterDevice) << "Status read failed:" << error;
            Q_EMIT readFailed(error);
        }
        return;
    }

    if (m_readFailing) {
        m_readFailing = false;
        qCInfo(hidcenterDevice) << "Status reads recovered.";
    }

    // The same latch again, for the report that may simply not exist on this
    // base. Worth one line either way: whether report 0x23 is answered at all is
    // the open question about it, and the log is where that gets answered.
    if (!fresh.liveValid && !m_liveReadFailing) {
        m_liveReadFailing = true;
        qCWarning(hidcenterDevice) << "Base status read failed:" << liveError;
    } else if (fresh.liveValid && m_liveReadFailing) {
        m_liveReadFailing = false;
        qCInfo(hidcenterDevice) << "Base status reads recovered.";
    }

    Q_EMIT statusRead(fresh);
}

bool AlphaEvoIo::writeBlock(const QByteArray &block, const QString &description, QString *error)
{
    if (m_dryRun) {
        qCInfo(hidcenterDevice).noquote()
            << tr("Dry run — would send %1: %2").arg(description, toHex(block));
        return true;
    }

    qCInfo(hidcenterDevice).noquote() << tr("Sending %1: %2").arg(description, toHex(block));

    if (!m_hidInterface->setFeatureReport(block)) {
        if (error)
            *error = m_hidInterface->lastError();
        return false;
    }
    return true;
}

void AlphaEvoIo::sendTelemetry(const SimagicTelemetry &data)
{
    if (!m_hidInterface)
        return;

    const QByteArray report = composeTelemetry(data);

    // Deliberately not routed through writeBlock(). That logs a line per send,
    // which is right for a settings write — there are a handful of them, and
    // each is worth seeing — and wrong here, where sixty a second would push
    // every other message out of a log the view publishes at 10 Hz.
    //
    // So: one line per stream. A gap is what starts a new one, which means
    // turning telemetry off and on again logs afresh, and a steady stream logs
    // once however long it runs.
    const bool streamStarting =
        !m_lastTelemetrySend.isValid() || m_lastTelemetrySend.elapsed() > kTelemetryGapMs;
    m_lastTelemetrySend.restart();

    if (streamStarting) {
        qCInfo(hidcenterDevice).noquote()
            << tr("%1 the first telemetry report; the rest go unlogged: %2")
                   .arg(m_dryRun ? tr("Dry run — would send") : tr("Sending"), toHex(report));
    }

    if (m_dryRun)
        return;

    if (!m_hidInterface->setFeatureReport(report)) {
        // Once per run of failures. A base that has stopped accepting these is
        // wrong sixty times a second.
        if (!m_sendFailing) {
            m_sendFailing = true;
            qCWarning(hidcenterDevice) << "Telemetry send failed:" << m_hidInterface->lastError();
        }
        return;
    }

    if (m_sendFailing) {
        m_sendFailing = false;
        qCInfo(hidcenterDevice) << "Telemetry sends recovered.";
    }
}

void AlphaEvoIo::sendLedFrame(const LedRingColours &colours)
{
    m_ledColours = colours;
    m_haveLedColours = true;

    if (!m_ledRefreshTimer) {
        // Created here rather than in the constructor, for the same reason
        // startPolling() creates its own: a timer belongs to the thread that
        // started it, and the constructor runs on the GUI thread.
        m_ledRefreshTimer = new QTimer(this);
        connect(m_ledRefreshTimer, &QTimer::timeout, this, &AlphaEvoIo::refreshLedColours);
    }
    // start() on a running timer resets it, which is what keeps a refresh from
    // landing immediately behind a frame that just said the same thing. While an
    // animation is running it never fires at all: the frames are the keepalive,
    // and this only takes over once they stop.
    m_ledRefreshTimer->start(kLedRefreshIntervalMs);

    // Announced, so a dry run still says what it would have sent — and the burst
    // suppression inside is what keeps that to one line rather than twenty-five
    // a second, because a running animation never leaves a gap.
    writeLedColours(/*announce=*/true);

    // Unconditional, and the only exit from this function. The animator composes
    // no further frame until it hears this.
    Q_EMIT ledFrameSent();
}

void AlphaEvoIo::refreshLedColours()
{
    if (!m_haveLedColours)
        return;

    // A dry run has put no colour on the wheel, so there is none to keep alive.
    // The timer keeps running, so turning the dry run off starts refreshing on
    // the next tick without anything having to notice.
    if (m_dryRun)
        return;

    writeLedColours(/*announce=*/false);
}

void AlphaEvoIo::writeLedColours(bool announce)
{
    const bool dryRun = m_dryRun;

    // Both refusals below go through the same failure latch a write does. Frames
    // arrive at up to twenty-five a second, so anything said once per call would
    // be said twenty-five times a second for as long as the condition held.
    //
    // The interface carrying the endpoint, not the one the settings go over. A
    // base whose configuration protocol answers perfectly can still have no way
    // to reach the ring — that interface is claimed through /dev/bus/usb, which
    // needs its own udev rule — and the two failures are worth telling apart.
    if (!dryRun && !m_ledInterface) {
        if (!m_ledSendFailing) {
            m_ledSendFailing = true;
            Q_EMIT ledColoursSent(false, tr("The wheel LEDs cannot be reached."), 0, 0, dryRun);
        }
        return;
    }

    const QVector<QByteArray> reports = composeLedRingReports(m_ledColours, m_deviceModel);
    if (reports.isEmpty()) {
        // The composer only refuses when the payload will not fit the frames it
        // is allowed, which a static_assert rules out at the ceiling. Reaching
        // here is a bug in the encoder rather than something the user did.
        if (!m_ledSendFailing) {
            m_ledSendFailing = true;
            // int(), or QDebug prints the quint8 as a character rather than a count.
            qCWarning(hidcenterDevice) << "Could not compose an LED packet for"
                                    << int(m_ledColours.count) << "colours.";
            Q_EMIT ledColoursSent(false, tr("Could not compose the LED packet."), 0, 0, dryRun);
        }
        return;
    }

    const int total = int(reports.size());

    // Announced writes are logged per burst, for the same reason as telemetry:
    // holding the colour-count spin box emits a run of changes, and one line per
    // packet would push everything else out of the log. The first packet of a
    // burst is logged in full and the rest are counted, so the next burst can
    // say how many went unseen.
    //
    // Refreshes skip this entirely. They resend bytes that are already in the
    // log from the write that announced them, and at one every three seconds
    // they would be the log.
    if (announce) {
        const bool burstStarting = !m_lastLedSend.isValid() || m_lastLedSend.elapsed() > kLedGapMs;
        m_lastLedSend.restart();

        if (burstStarting) {
            QStringList frames;
            frames.reserve(total);
            for (const QByteArray &report : reports)
                frames.append(toHex(report));

            QString line = tr("%1 the wheel LED colours (%2 report(s)): %3")
                               .arg(dryRun ? tr("Dry run — would send") : tr("Sending"))
                               .arg(total)
                               .arg(frames.join(QLatin1String(" | ")));
            if (m_ledSendsSuppressed > 0)
                line += tr(" [%n earlier write(s) went unlogged]", nullptr, m_ledSendsSuppressed);
            qCInfo(hidcenterDevice).noquote() << line;
            m_ledSendsSuppressed = 0;
        } else {
            ++m_ledSendsSuppressed;
        }
    }

    if (dryRun) {
        // Nothing to report: a dry run cannot fail, and the log line above is
        // the whole account of it. Clearing the latch matters, though — a device
        // that was failing and is now merely being dry-run is no longer failing,
        // and the page's warning should not sit there through the whole session.
        m_ledSendFailing = false;
        return;
    }

    int sent = 0;
    QString error;
    for (const QByteArray &report : reports) {
        if (!m_ledInterface->writeInterrupt(kLedEndpoint, report)) {
            error = m_ledInterface->lastError();
            // Stop rather than pushing the rest. The message is already
            // unrecoverable, and if the cause is a base that has gone away the
            // remaining reports will fail too. Nothing retries behind the user's
            // back; the next edit or refresh sends the whole packet again.
            break;
        }
        ++sent;
    }

    // The failure latch is the whole of what reaches the page. While the outcome
    // is unchanged nothing is said at all; it speaks only when writes start
    // failing, so a wheel that has gone dark surfaces there, and when they
    // recover, so that message clears itself rather than sitting until the next
    // edit. Frames arrive too fast for any other rule to be usable.
    const bool ok = sent == total;
    const bool changed = !ok != m_ledSendFailing;  // failing now, versus failing before

    if (!ok && changed) {
        m_ledSendFailing = true;
        qCWarning(hidcenterDevice) << "LED write failed on report" << (sent + 1) << "of" << total
                                << ":" << error;
    } else if (ok && changed) {
        m_ledSendFailing = false;
        qCInfo(hidcenterDevice) << "LED writes recovered.";
    }

    if (changed)
        Q_EMIT ledColoursSent(ok, error, sent, total, false);
}

void AlphaEvoIo::sendWheelConfigReport(const WheelConfig &config, int chunk)
{
    const bool dryRun = m_dryRun;

    const QByteArray report = composeWheelConfigReport(config, chunk);
    if (report.isEmpty()) {
        // Only a caller passing a chunk that does not exist gets here, which is
        // a bug rather than something the user did.
        qCWarning(hidcenterDevice) << "No wheel configuration report" << chunk;
        return;
    }

    // Per burst, as the LED writes are: a dragged slider is several of these a
    // second, and the first of a run is the one worth reading in full.
    QElapsedTimer &lastSend = m_lastWheelConfigSend[std::size_t(chunk)];
    int &suppressed = m_wheelConfigSendsSuppressed[std::size_t(chunk)];
    const bool burstStarting = !lastSend.isValid() || lastSend.elapsed() > kWheelConfigGapMs;
    lastSend.restart();

    if (burstStarting) {
        QString line = tr("%1 wheel configuration report %2 (%3): %4")
                           .arg(dryRun ? tr("Dry run — would send") : tr("Sending"))
                           .arg(chunk)
                           .arg(describeWheelConfigChunk(config, chunk))
                           .arg(toHex(report));
        if (suppressed > 0)
            line += tr(" [%n earlier write(s) went unlogged]", nullptr, suppressed);
        qCInfo(hidcenterDevice).noquote() << line;
        suppressed = 0;
    } else {
        ++suppressed;
    }

    if (dryRun) {
        // As for the LEDs: a dry run cannot fail, and a failure that was
        // standing before it no longer is.
        if (m_wheelConfigSendFailing) {
            m_wheelConfigSendFailing = false;
            Q_EMIT wheelConfigSent(true, QString(), true);
        }
        return;
    }

    QString error;
    const bool ok = m_hidInterface && m_hidInterface->setFeatureReport(report);
    if (!ok)
        error = m_hidInterface ? m_hidInterface->lastError() : tr("The device is not open.");

    if (ok == !m_wheelConfigSendFailing)
        return;  // No change in how writes are going, so nothing to say.

    m_wheelConfigSendFailing = !ok;
    if (ok)
        qCInfo(hidcenterDevice) << "Wheel configuration writes recovered.";
    else
        qCWarning(hidcenterDevice) << "Wheel configuration write failed:" << error;

    Q_EMIT wheelConfigSent(ok, error, false);
}

void AlphaEvoIo::requestApply(const SettingValues &changes)
{
    const bool dryRun = m_dryRun;

    if (changes.isEmpty()) {
        Q_EMIT applyFinished(true, QString(), 0, dryRun);
        return;
    }

    if (!m_hidInterface) {
        Q_EMIT applyFinished(false, tr("The device is not open."), 0, dryRun);
        return;
    }

    // Re-read first. Each block carries bytes whose meaning is unknown and which
    // the base expects back unchanged, so they must come from the device's
    // current state rather than from whatever we last saw.
    //
    // Deliberately not announced: this is the state *before* the write, and
    // publishing it would snap every control back to the old value for as long
    // as the write takes, then forward again. The read-back below is the one the
    // UI wants.
    //
    // No live telemetry with it: this read exists only so the composers have the
    // unknown bytes to copy, and a second transfer per write would buy nothing.
    StatusSnapshot before;
    QString error;
    if (!readStatus(m_hidInterface, before, &error, /*readLive=*/false)) {
        Q_EMIT applyFinished(false, error, 0, dryRun);
        return;
    }
    const StatusReport &fresh = before.settings;

    struct BlockWrite
    {
        SettingBlock block;
        QByteArray (*compose)(const StatusReport &, const SettingValues &);
        const char *description;
    };

    static constexpr BlockWrite kWrites[] = {
        {SettingBlock::Settings1, &composeSettings1,
         QT_TR_NOOP("settings block 1 (force feedback)")},
        {SettingBlock::Settings2, &composeSettings2,
         QT_TR_NOOP("settings block 2 (rotation and detail)")},
        {SettingBlock::Settings3, &composeSettings3, QT_TR_NOOP("settings block 3 (ring light)")},
        {SettingBlock::Settings4, &composeSettings4, QT_TR_NOOP("settings block 4 (signal)")},
    };

    int written = 0;
    for (const BlockWrite &write : kWrites) {
        if (!touchesBlock(changes, write.block))
            continue;

        const QByteArray buffer = write.compose(fresh, changes);
        if (!writeBlock(buffer, tr(write.description), &error)) {
            Q_EMIT applyFinished(false, error, written, dryRun);
            return;
        }
        ++written;
    }

    // The base does not repaint its LEDs at the new brightness on its own; it
    // wants the colours again. Restating them is what does that, and is a no-op
    // when there are none yet or in a dry run.
    if (changes.contains(Setting::RingLightBrightness))
        refreshLedColours();

    // Read back so the UI reflects what the device actually took, including any
    // clamping it applied of its own. Skipped in dry run, where nothing changed.
    if (!dryRun) {
        // This one does take the telemetry: it goes to the UI, and arriving
        // without it would blank the page's live fields until the next poll.
        StatusSnapshot after;
        QString readError;
        if (readStatus(m_hidInterface, after, &readError))
            Q_EMIT statusRead(after);
        else
            qCWarning(hidcenterDevice) << "Could not read back after writing:" << readError;
    }

    Q_EMIT applyFinished(true, QString(), written, dryRun);
}

} // namespace hidcenter::simagic_alpha_evo
