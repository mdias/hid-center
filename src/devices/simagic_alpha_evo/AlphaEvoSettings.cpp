// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoSettings.h"

#include "AlphaEvoAnimator.h"
#include "AlphaEvoBlocks.h"
#include "AlphaEvoLedProtocol.h"
#include "AlphaEvoIo.h"
#include "AlphaEvoTelemetry.h"
#include "LedModeStore.h"
#include "core/Logging.h"
#include "core/usb/IUsbDevice.h"
#include "core/usb/IUsbDeviceInterface.h"

#include <QStringList>
#include <QThread>

#include <cstring>
#include <utility>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// How often the status report is re-read, so the UI follows changes made
/// elsewhere — another program, the kernel driver's sysfs files, or the base's
/// own controls. Change this one value to re-pace polling; it is deliberately a
/// single constant so it can be lifted into the UI later.
constexpr int kStatusPollIntervalMs = 500;

/// Top-level usage of a joystick and a gamepad. The kernel driver reaches the
/// base through whichever HID interface it binds as an input device, so this is
/// the strongest hint we have when the descriptor does not declare 0x81.
constexpr quint16 kGenericDesktopPage = 0x01;
constexpr quint16 kJoystickUsage = 0x04;
constexpr quint16 kGamepadUsage = 0x05;

bool contains(const QVector<UsbInterfaceInfo> &interfaces, const UsbInterfaceInfo &wanted)
{
    for (const UsbInterfaceInfo &iface : interfaces) {
        if (iface.sysPath == wanted.sysPath)
            return true;
    }
    return false;
}

} // namespace

AlphaEvoSettings::AlphaEvoSettings(IUsbDevice *device, QObject *parent)
    : QObject(parent)
    , m_usbDevice(device)
{
    // All of these cross a thread boundary as queued signal arguments.
    qRegisterMetaType<StatusReport>();
    qRegisterMetaType<StatusSnapshot>();
    qRegisterMetaType<SettingValues>();
    qRegisterMetaType<SimagicTelemetry>();
    qRegisterMetaType<LedRingColours>();
    qRegisterMetaType<LedMode>();
    qRegisterMetaType<LedModeSet>();
    qRegisterMetaType<WheelConfig>();

    m_ledModes = defaultLedModes();

    // Read once, and here rather than in probe(), so the answer survives every
    // branch below it — including a base that never answered the configuration
    // protocol, where a missing force-feedback driver is one of the more
    // plausible things to have gone wrong.
    //
    // Once is enough because binding or unbinding a driver destroys and
    // recreates the device's hidraw node, which re-enumerates the device and so
    // rebuilds this object. There is nothing a timer here could notice that a
    // fresh construction does not.
    if (m_usbDevice)
        m_driver = forceFeedbackDriverFor(m_usbDevice->info());

    if (!m_driver.known)
        qCInfo(hidcenterDevice) << "Cannot tell which driver handles force feedback for this base.";
    else if (m_driver.forceFeedback)
        qCInfo(hidcenterDevice).noquote() << tr("Force feedback is handled by %1.").arg(m_driver.driver);
    else if (m_driver.driver.isEmpty())
        qCWarning(hidcenterDevice) << "No driver is bound to this base: it has no force feedback.";
    else
        qCWarning(hidcenterDevice).noquote()
            << tr("%1 is bound to this base but exposes no force feedback.").arg(m_driver.driver);

    // Before probe(), and whatever probe() goes on to decide. The clock only
    // computes, so it is as useful with no base behind it as with one — and
    // AlphaEvoTelemetry, built moments after this, expects to find it.
    startAnimationThread();
}

AlphaEvoSettings::~AlphaEvoSettings()
{
    // The animator first. It is the one thing that posts to the I/O thread on
    // its own initiative, so stopping it before the I/O thread means no frame
    // can be queued onto a worker that is on its way out.
    stopAnimationThread();
    stopIoThread();
}

QVector<UsbInterfaceInfo> AlphaEvoSettings::probeOrder() const
{
    QVector<UsbInterfaceInfo> ordered;
    if (!m_usbDevice)
        return ordered;

    // HID interfaces only. The configuration protocol is feature reports, which
    // need a hidraw node; the device's other interfaces are now listed too, and
    // probing one would be a guaranteed failed open every time the page is built.
    QVector<UsbInterfaceInfo> interfaces;
    for (const UsbInterfaceInfo &iface : m_usbDevice->info().interfaces) {
        if (iface.isHid())
            interfaces.append(iface);
    }

    // Best evidence: the descriptor itself declares the status report.
    for (const UsbInterfaceInfo &iface : interfaces) {
        if (iface.hid.descriptor.find(kStatusReportId, ReportType::Feature))
            ordered.append(iface);
    }

    // Next best: the interface the OS binds as a joystick, which is the one the
    // kernel driver talks to.
    for (const UsbInterfaceInfo &iface : interfaces) {
        if (contains(ordered, iface))
            continue;
        if (iface.hid.usagePage == kGenericDesktopPage
            && (iface.hid.usage == kJoystickUsage || iface.hid.usage == kGamepadUsage)) {
            ordered.append(iface);
        }
    }

    // Then everything else: device-specific protocols are frequently undeclared, so a
    // guess that fails costs one harmless read.
    for (const UsbInterfaceInfo &iface : interfaces) {
        if (!contains(ordered, iface))
            ordered.append(iface);
    }

    return ordered;
}

bool AlphaEvoSettings::probe(QString *error)
{
    if (!m_usbDevice) {
        if (error)
            *error = tr("No device.");
        return false;
    }

    if (m_usbDevice->isOffline()) {
        m_offline = true;

        // Seed from a zeroed report put through each setting's own clamp, which
        // lifts the two angles to their minimum of 90° and leaves everything
        // else at zero. Inventing plausible factory defaults would be worse: they
        // would look like facts about the protocol, and they are not.
        m_status = {};
        m_status.reportId = kStatusReportId;
        for (const SettingSpec &spec : settingSpecs())
            setValue(m_status, spec.id, clampToSpec(spec.id, 0));

        qCInfo(hidcenterDevice) << "No wheelbase behind this page: showing an offline preview. "
                                "Nothing will be read from or written to a device.";

        // Anything handed in before it was known there is no base: logged now,
        // the way it would have been sent now.
        const quint8 pending = std::exchange(m_pendingWheelChunks, 0);
        for (int chunk = 0; chunk < kWheelConfigChunkCount; ++chunk) {
            if (pending & (1u << chunk))
                writeWheelConfigAsync(m_wheelConfig, chunk);
        }
        return true;
    }

    const QVector<UsbInterfaceInfo> candidates = probeOrder();
    if (candidates.isEmpty()) {
        if (error)
            *error = tr("No HID interfaces found.");
        return false;
    }

    QStringList failures;
    for (const UsbInterfaceInfo &iface : candidates) {
        // Claimed rather than opened outright, so a candidate that turns out not
        // to speak this protocol can be let go of again. It used to be opened and
        // left that way for the session — and an open interface is not free, the
        // kernel delivering every input report it carries to every reader.
        //
        // The one this loop settles on keeps its claim for as long as this object
        // lives, which is what stops anything else closing it: AlphaEvoIo holds
        // the same handle as a bare pointer and drives it from the I/O thread.
        // The claim goes when this object is destroyed, and that happens after
        // the destructor has joined that thread.
        IUsbDeviceHidInterface *hid = m_usbDevice->openHidFor(this, iface);
        if (!hid) {
            failures << tr("%1: %2").arg(iface.displayName(), m_usbDevice->lastError());
            continue;
        }

        // Blocking, but this is the one read that has to happen before there is
        // anything to show, and it runs while the device page is being built.
        StatusSnapshot snapshot;
        QString readError;
        if (!readStatus(hid, snapshot, &readError)) {
            failures << tr("%1: %2").arg(iface.displayName(), readError);
            m_usbDevice->releaseFor(this, iface);
            continue;
        }

        m_hidInterface = hid;
        m_interface = iface;
        m_status = snapshot.settings;
        m_baseStatus = snapshot.live;
        m_haveBaseStatus = snapshot.liveValid;

        connect(hid, &IUsbDeviceHidInterface::disconnected, this, [this] {
            m_hidInterface = nullptr;
            stopIoThread();
        });

        // A last-ditch stop, not a guarantee: destroyed() is emitted from
        // ~QObject, by which point a transfer already running against this
        // handle is using a half-destroyed object. What actually keeps that from
        // happening is the teardown order in DeviceView, which deletes the
        // module — and so joins this thread — before the context that owns the
        // handles. This only stops the worker starting anything new.
        connect(hid, &QObject::destroyed, this, [this] {
            m_hidInterface = nullptr;
            stopIoThread();
        });

        qCInfo(hidcenterDevice).noquote()
            << tr("SIMAGIC configuration protocol answered on %1 (firmware %2)")
                   .arg(iface.displayName())
                   .arg(firmwareVersion());

        openLedInterface();

        startIoThread();
        return true;
    }

    if (error) {
        *error = tr("The wheelbase did not answer on any HID interface.\n\n%1")
                     .arg(failures.join(QLatin1Char('\n')));
    }
    return false;
}

void AlphaEvoSettings::openLedInterface()
{
    // Here rather than in AlphaEvoIo, and before the I/O thread exists, because
    // opening is a GUI-thread act: it reaches into the device's own bookkeeping
    // and reparents the handle onto it. The same reason probe() itself cannot be
    // asynchronous.
    //
    // A failure is not a failure of the device. The base configures perfectly
    // without this; it is the ring that goes dark, and the LED page is where
    // that is reported.
    m_ledInterface = m_usbDevice->openInterfaceForEndpoint(kLedEndpoint);
    if (!m_ledInterface) {
        qCWarning(hidcenterDevice).noquote()
            << tr("The wheel LEDs cannot be reached: %1").arg(m_usbDevice->lastError());
        return;
    }

    // Both halves of the same guarantee the configuration handle gets in
    // probe(), and for the same reason: the worker holds this pointer too, and
    // it must not be the one interface whose going away leaves the I/O thread
    // running against a handle that is gone.
    connect(m_ledInterface, &IUsbDeviceInterface::disconnected, this, [this] {
        m_ledInterface = nullptr;
        stopIoThread();
    });
    connect(m_ledInterface, &QObject::destroyed, this, [this] {
        m_ledInterface = nullptr;
        stopIoThread();
    });

    qCInfo(hidcenterDevice).noquote()
        << tr("Wheel LED frames go to endpoint 0x%1 on %2.")
               .arg(kLedEndpoint, 2, 16, QLatin1Char('0'))
               .arg(m_ledInterface->interfaceInfo().displayName());
}

void AlphaEvoSettings::startIoThread()
{
    m_ioThread = new QThread(this);
    m_ioThread->setObjectName(QStringLiteral("alpha-evo-io"));

    m_io = new AlphaEvoIo(m_usbDevice, m_hidInterface, m_ledInterface, ledDeviceModel());
    m_io->moveToThread(m_ioThread);
    connect(m_ioThread, &QThread::finished, m_io, &QObject::deleteLater);

    connect(m_io, &AlphaEvoIo::statusRead, this, &AlphaEvoSettings::onStatusRead);
    connect(m_io, &AlphaEvoIo::readFailed, this, &AlphaEvoSettings::readFailed);
    connect(m_io, &AlphaEvoIo::applyFinished, this, &AlphaEvoSettings::onApplyFinished);
    connect(m_io, &AlphaEvoIo::ledColoursSent, this, &AlphaEvoSettings::ledColoursSent);
    connect(m_io, &AlphaEvoIo::wheelConfigSent, this, &AlphaEvoSettings::wheelConfigSent);

    // Worker to worker, both queued because the two live on different threads.
    // The acknowledgement going back is what holds the animator to one frame in
    // flight; see AlphaEvoAnimator::ledFrameReady.
    connect(m_animator, &AlphaEvoAnimator::telemetryReady, m_io, &AlphaEvoIo::sendTelemetry);
    connect(m_animator, &AlphaEvoAnimator::ledFrameReady, m_io, &AlphaEvoIo::sendLedFrame);
    connect(m_io, &AlphaEvoIo::ledFrameSent, m_animator, &AlphaEvoAnimator::frameConsumed);

    m_ioThread->start();

    QMetaObject::invokeMethod(m_io, "setDryRun", Qt::QueuedConnection, Q_ARG(bool, m_dryRun));
    QMetaObject::invokeMethod(m_io, "startPolling", Qt::QueuedConnection,
                              Q_ARG(int, kStatusPollIntervalMs));

    // The wheel configuration handed in before there was a thread to send it,
    // which at start-up is the value restored from settings. After the dry-run flag,
    // so a dry run is honoured from the first write.
    for (int chunk = 0; chunk < kWheelConfigChunkCount; ++chunk) {
        if (!(m_pendingWheelChunks & (1u << chunk)))
            continue;
        QMetaObject::invokeMethod(m_io, "sendWheelConfigReport", Qt::QueuedConnection,
                                  Q_ARG(hidcenter::simagic_alpha_evo::WheelConfig, m_wheelConfig),
                                  Q_ARG(int, chunk));
    }
    m_pendingWheelChunks = 0;

    // Now there is somewhere for a frame to go, and something to acknowledge it.
    QMetaObject::invokeMethod(m_animator, "setDeviceAttached", Qt::QueuedConnection,
                              Q_ARG(bool, true));
}

void AlphaEvoSettings::startAnimationThread()
{
    m_animThread = new QThread(this);
    m_animThread->setObjectName(QStringLiteral("alpha-evo-anim"));

    m_animator = new AlphaEvoAnimator;
    m_animator->moveToThread(m_animThread);
    connect(m_animThread, &QThread::finished, m_animator, &QObject::deleteLater);

    connect(m_animator, &AlphaEvoAnimator::activeModeChanged, this,
            &AlphaEvoSettings::onActiveLedModeChanged);

    m_animThread->start();

    QMetaObject::invokeMethod(m_animator, "start", Qt::QueuedConnection,
                              Q_ARG(int, AlphaEvoTelemetry::kDefaultRateHz));
}

void AlphaEvoSettings::stopAnimationThread()
{
    if (!m_animThread)
        return;

    m_animThread->quit();
    m_animThread->wait();

    delete m_animThread;
    m_animThread = nullptr;
    m_animator = nullptr;  // Deleted by the thread's finished handler.
}

void AlphaEvoSettings::stopIoThread()
{
    if (!m_ioThread)
        return;

    // First, so no further frame is posted onto a queue that is about to be
    // joined — and so the animator drops the frame it is waiting to hear about,
    // which nothing is left to acknowledge.
    if (m_animator) {
        QMetaObject::invokeMethod(m_animator, "setDeviceAttached", Qt::QueuedConnection,
                                  Q_ARG(bool, false));
    }

    // quit() then wait(): the wait is what makes this safe to call while a
    // transfer is in flight, and the handle's own lock means that transfer
    // finishes rather than being cut off mid-ioctl.
    m_ioThread->quit();
    m_ioThread->wait();

    delete m_ioThread;
    m_ioThread = nullptr;
    m_io = nullptr;   // Deleted by the thread's finished handler.

    if (m_busy) {
        m_busy = false;
        Q_EMIT applyFinished(false, tr("The wheelbase was unplugged while writing."), 0, m_dryRun);
    }

    // Nothing is confirming the live readings any more, and a frozen temperature
    // reads exactly like a current one. Cleared here rather than in either
    // disconnect handler because this is where both of them meet.
    //
    // The settings are deliberately left alone: they are the last thing the base
    // was known to hold, which is still worth showing, and the pages that read
    // them already grey out when isReady() goes false.
    if (m_haveBaseStatus) {
        m_haveBaseStatus = false;
        Q_EMIT baseStatusRefreshed();
    }
}

void AlphaEvoSettings::onStatusRead(const StatusSnapshot &snapshot)
{
    // Two comparisons and two signals, because the two halves move at completely
    // different rates. Folding them together would mean a warming motor waking
    // every settings control twice a second.
    if (std::memcmp(&snapshot.settings, &m_status, sizeof(m_status)) != 0) {
        m_status = snapshot.settings;
        Q_EMIT statusRefreshed();
    }

    // Losing the report counts as a change: the page has to stop showing values
    // that are no longer being confirmed.
    const bool liveChanged = snapshot.liveValid != m_haveBaseStatus
        || (snapshot.liveValid
            && std::memcmp(&snapshot.live, &m_baseStatus, sizeof(m_baseStatus)) != 0);

    if (liveChanged) {
        m_baseStatus = snapshot.live;
        m_haveBaseStatus = snapshot.liveValid;
        Q_EMIT baseStatusRefreshed();
    }
}

void AlphaEvoSettings::onApplyFinished(bool ok, const QString &error, int blocksWritten,
                                       bool dryRun)
{
    m_busy = false;
    Q_EMIT applyFinished(ok, error, blocksWritten, dryRun);
}

void AlphaEvoSettings::applyOffline(const SettingValues &changes)
{
    // Compose the blocks anyway, purely to log them. Nothing consumes the bytes,
    // but running the real composers is what makes the offline log worth reading:
    // it shows exactly what a connected base would have received.
    int blocks = 0;
    const struct
    {
        SettingBlock block;
        QByteArray (*compose)(const StatusReport &, const SettingValues &);
        const char *name;
    } composers[] = {
        {SettingBlock::Settings1, composeSettings1, "settings 1"},
        {SettingBlock::Settings2, composeSettings2, "settings 2"},
        {SettingBlock::Settings3, composeSettings3, "settings 3"},
        {SettingBlock::Settings4, composeSettings4, "settings 4"},
    };

    for (const auto &entry : composers) {
        if (!touchesBlock(changes, entry.block))
            continue;
        qCInfo(hidcenterDevice).noquote()
            << QStringLiteral("Offline, would have written %1: %2")
                   .arg(QLatin1String(entry.name),
                        QString::fromLatin1(entry.compose(m_status, changes).toHex(' ')));
        ++blocks;
    }

    // The status report is the wheelbase here, so this is the write.
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        setValue(m_status, it.key(), it.value());

    Q_EMIT applyStarted();
    Q_EMIT statusRefreshed();
    Q_EMIT applyFinished(true, QString(), blocks, /*dryRun=*/true);
}

void AlphaEvoSettings::applyAsync(const SettingValues &changes)
{
    if (m_offline) {
        applyOffline(changes);
        return;
    }

    if (!m_io) {
        Q_EMIT applyFinished(false, tr("The device is not open."), 0, m_dryRun);
        return;
    }

    if (m_busy) {
        // Should not happen — the page locks its controls for the duration — so
        // say something rather than silently dropping the edits.
        qCWarning(hidcenterDevice) << "Ignoring a write requested while another is in flight.";
        return;
    }

    m_busy = true;
    Q_EMIT applyStarted();

    QMetaObject::invokeMethod(m_io, "requestApply", Qt::QueuedConnection,
                              Q_ARG(hidcenter::simagic_alpha_evo::SettingValues, changes));
}

quint8 AlphaEvoSettings::ledDeviceModel() const
{
    return m_usbDevice ? baseModelForProductId(m_usbDevice->info().id.productId)
                     : baseModelForProductId(0);
}

void AlphaEvoSettings::logLedColoursOffline()
{
    // The mode's own colours, which is the frame at the start of its cycle: an
    // animation offline has nothing to animate, and the colours the user chose
    // are the interesting thing to see the bytes for.
    const LedRingColours colours = m_ledModes.at(m_activeLedMode).colours;

    // A mode change and a settings change can both land on the same packet, and
    // the same bytes twice is not news.
    if (m_haveLoggedOffline && colours == m_loggedOfflineColours)
        return;
    m_loggedOfflineColours = colours;
    m_haveLoggedOffline = true;

    // Compose for real, purely to log it, for the same reason applyOffline()
    // does: the bytes are the interesting part, and they are exactly what a
    // connected base would have been sent.
    const QVector<QByteArray> reports = composeLedRingReports(colours, ledDeviceModel());
    if (reports.isEmpty()) {
        Q_EMIT ledColoursSent(false, tr("Could not compose the LED packet."), 0, 0, true);
        return;
    }

    QStringList frames;
    frames.reserve(reports.size());
    for (const QByteArray &report : reports)
        frames.append(QString::fromLatin1(report.toHex(' ')));

    qCInfo(hidcenterDevice).noquote()
        << QStringLiteral("Offline, would have written the wheel LED colours (%1 report(s)): %2")
               .arg(reports.size())
               .arg(frames.join(QLatin1String(" | ")));

    // No status to write back into, unlike applyOffline(): the ring widget is
    // the only place these colours live.
    Q_EMIT ledColoursSent(true, QString(), int(reports.size()), int(reports.size()),
                          /*dryRun=*/true);
}

void AlphaEvoSettings::writeWheelConfigAsync(const WheelConfig &config, int chunk)
{
    if (chunk < 0 || chunk >= kWheelConfigChunkCount)
        return;

    m_wheelConfig = config;

    if (m_offline) {
        // Composed for real, for the reason logLedColoursOffline() gives: the
        // bytes are the interesting part. The caller only calls on a change, so
        // a line per call is a line per change.
        qCInfo(hidcenterDevice).noquote()
            << QStringLiteral("Offline, would have written wheel configuration report %1 (%2): %3")
                   .arg(chunk)
                   .arg(describeWheelConfigChunk(config, chunk))
                   .arg(QString::fromLatin1(composeWheelConfigReport(config, chunk).toHex(' ')));
        return;
    }

    if (!m_io) {
        // Not there yet, or never will be if the base does not answer. Kept by
        // chunk rather than as a queue of reports: whatever goes out when the
        // thread starts should be the latest config, sent once.
        m_pendingWheelChunks |= quint8(1u << chunk);
        return;
    }

    // Deliberately not gated on m_busy. See the note on the declaration.
    QMetaObject::invokeMethod(m_io, "sendWheelConfigReport", Qt::QueuedConnection,
                              Q_ARG(hidcenter::simagic_alpha_evo::WheelConfig, config),
                              Q_ARG(int, chunk));
}

void AlphaEvoSettings::setLedModesAsync(const LedModeSet &modes)
{
    if (m_ledModes == modes)
        return;

    m_ledModes = modes;

    // Deliberately not gated on m_busy. See the note on the declaration.
    if (m_animator) {
        QMetaObject::invokeMethod(m_animator, "setModes", Qt::QueuedConnection,
                                  Q_ARG(hidcenter::simagic_alpha_evo::LedModeSet, modes));
    }

    if (m_offline)
        logLedColoursOffline();
}

void AlphaEvoSettings::setLedPreviewAsync(bool previewing, LedMode mode)
{
    if (!m_animator)
        return;

    QMetaObject::invokeMethod(m_animator, "setPreview", Qt::QueuedConnection,
                              Q_ARG(bool, previewing),
                              Q_ARG(hidcenter::simagic_alpha_evo::LedMode, mode));
}

void AlphaEvoSettings::onActiveLedModeChanged(LedMode mode)
{
    m_activeLedMode = mode;

    if (m_offline)
        logLedColoursOffline();

    Q_EMIT ledModeChanged(mode);
}

void AlphaEvoSettings::setDryRun(bool dryRun)
{
    if (m_dryRun == dryRun)
        return;
    m_dryRun = dryRun;
    qCInfo(hidcenterDevice) << (dryRun ? "Dry run enabled: writes will be logged, not sent."
                                    : "Dry run disabled: writes will reach the device.");

    if (m_io)
        QMetaObject::invokeMethod(m_io, "setDryRun", Qt::QueuedConnection, Q_ARG(bool, dryRun));

    Q_EMIT dryRunChanged(m_dryRun);
}

int AlphaEvoSettings::value(Setting setting) const
{
    return valueOf(m_status, setting);
}

int AlphaEvoSettings::effectiveMaximum(Setting setting) const
{
    const SettingSpec *spec = specFor(setting);
    if (!spec)
        return 0;

    // The base refuses an angle lock beyond its maximum rotation, so the useful
    // upper bound moves with that setting rather than sitting at 2520.
    if (setting == Setting::AngleLock)
        return qBound(spec->minimum, int(readLe(m_status.maxAngle)), spec->maximum);

    return spec->maximum;
}

int AlphaEvoSettings::firmwareVersion() const
{
    // Only the low three digits are the firmware number; the raw field carries more.
    return readLe(m_status.firmwareVersion) % 1000;
}

quint16 AlphaEvoSettings::firmwareRaw() const
{
    return readLe(m_status.firmwareVersion);
}

QVector<UsbInterfaceInfo> AlphaEvoSettings::interfaces() const
{
    return m_usbDevice ? m_usbDevice->info().interfaces : QVector<UsbInterfaceInfo>();
}

} // namespace hidcenter::simagic_alpha_evo
