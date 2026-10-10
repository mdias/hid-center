// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

#include <optional>

#include "AlphaEvoProtocol.h"
#include "AlphaEvoSpecs.h"
#include "AlphaEvoWheelConfig.h"
#include "LedModes.h"
#include "SimagicLeds.h"
#include "SimagicTelemetry.h"
#include "core/usb/UsbDeviceInfo.h"
#include "core/usb/HidDriverInfo.h"

class QThread;

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
class IUsbDeviceInterface;
} // namespace hidcenter

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoAnimator;
class AlphaEvoIo;

/// An Alpha Evo's configuration, as the UI sees it.
///
/// This half lives on the GUI thread and holds no more than the last status read
/// and the dry-run flag. Every transfer happens on an AlphaEvoIo on its own
/// thread — see that class for why — so nothing here blocks, and results come
/// back as signals rather than return values.
///
/// The one thing that is *not* asynchronous is probe(), which opens interfaces.
/// Opening has to stay on the GUI thread: a handle installs a QSocketNotifier,
/// which belongs to the thread that created it.
class AlphaEvoSettings : public QObject
{
    Q_OBJECT

public:
    explicit AlphaEvoSettings(IUsbDevice *device, QObject *parent = nullptr);
    ~AlphaEvoSettings() override;

    /// Find the interface that answers the configuration protocol, read the
    /// first status from it, and start the I/O thread. False means the device
    /// cannot be configured; `error` says why.
    ///
    /// An offline context short-circuits all of that: there is nothing to probe,
    /// so it succeeds immediately against an in-memory status report and starts
    /// no thread. See isOffline().
    bool probe(QString *error);

    bool isReady() const { return m_hidInterface != nullptr || m_offline; }

    /// True when there is no wheelbase behind this, and the status report is
    /// simply a host-side scratchpad.
    ///
    /// Not the same as a dry run. Dry run gates *writes* on a device that is
    /// there and still being polled; offline means no transfer of any kind ever
    /// happens, because the I/O thread was never started.
    bool isOffline() const { return m_offline; }

    /// True while a write is in flight. The page uses this to lock its controls,
    /// so a second batch cannot be queued behind the first.
    bool isBusy() const { return m_busy; }

    const StatusReport &status() const { return m_status; }
    int value(Setting setting) const;

    /// What the base is *doing*, from report 0x23 — temperatures, torque, the
    /// running state. Only meaningful while hasBaseStatus() is true.
    ///
    /// Separate from status() rather than folded into it because the two change
    /// on completely different schedules: settings change when someone changes
    /// them, and a motor temperature changes on its own. See baseStatusRefreshed().
    const BaseStatusReport &baseStatus() const { return m_baseStatus; }

    /// False until a base has answered report 0x23, and again if it stops.
    ///
    /// Expected to be false on hardware that does not carry that report at all;
    /// nothing else about this object changes when it is.
    bool hasBaseStatus() const { return m_haveBaseStatus; }

    /// Upper bound for Setting::AngleLock, which the base clamps to the current
    /// maximum rotation rather than to a fixed value.
    int effectiveMaximum(Setting setting) const;

    /// The firmware number (the low three digits of the raw field), and the raw 16-bit field behind it.
    int firmwareVersion() const;
    quint16 firmwareRaw() const;

    /// Every interface the base exposes, whether or not this module uses it.
    ///
    /// The whole list rather than the one in use, because which interfaces exist
    /// and what endpoints they carry is exactly the kind of fact the Status page
    /// is for — and an interface with no driver bound to it, which is invisible
    /// everywhere else in the OS, is the one most worth seeing.
    QVector<UsbInterfaceInfo> interfaces() const;

    /// The interface the configuration protocol answered on, or nothing until
    /// probe() has succeeded.
    std::optional<UsbInterfaceInfo> activeInterface() const { return m_interface; }

    /// Which kernel driver is giving this base force feedback, if any.
    ///
    /// This base does not declare force feedback through the standard HID
    /// descriptors, so it has none unless an out-of-tree driver is bound — and
    /// nothing in the configuration protocol reveals whether one is. The base
    /// answers every setting perfectly either way, and the wheel stays limp.
    ///
    /// Read once when this object is built, from what enumeration already found
    /// — no open device needed, which is why it still answers after a failed
    /// probe. It cannot go stale: binding or unbinding a driver re-enumerates
    /// the device, and that rebuilds this object.
    HidDriverInfo forceFeedbackDriver() const { return m_driver; }

    /// When set, a write logs what it would have sent and returns without
    /// touching the device.
    bool isDryRun() const { return m_dryRun; }
    void setDryRun(bool dryRun);

    /// Queue a write. Returns immediately; watch applyStarted/applyFinished.
    /// Ignored while another write is in flight.
    void applyAsync(const SettingValues &changes);

    /// The clock behind the telemetry stream and the wheel's LED animations.
    ///
    /// Never null and alive for this object's whole life, hardware or not: it
    /// only computes, so the pages that read it stay useful offline. The two GUI
    /// façades over it — AlphaEvoTelemetry and WheelLedController — connect to
    /// it directly, and every one of those connections crosses a thread and so
    /// is queued.
    AlphaEvoAnimator *animator() const { return m_animator; }

    /// Hand the animator a new set of mode settings. Returns immediately.
    ///
    /// Not blocked by a settings write in flight, for the same reason telemetry
    /// is not: the busy gate exists to serialise the settings read-modify-write,
    /// and an LED packet reads nothing first. Dropping a colour change because
    /// the FFB page happened to be mid-apply would lose it silently.
    ///
    /// Offline this composes the active mode's packet and logs it, so the page
    /// stays useful with no hardware present — once per change, not once per
    /// animation frame, which would be a log of nothing else.
    void setLedModesAsync(const LedModeSet &modes);

    /// Show `mode` on the wheel while telemetry is asking for nothing. This is
    /// the LED page previewing what is being edited; see AlphaEvoAnimator.
    void setLedPreviewAsync(bool previewing, LedMode mode);

    /// Which mode the wheel is in, as the animator last reported it.
    LedMode activeLedMode() const { return m_activeLedMode; }

    /// Send one report of the steering wheel's configuration, `chunk` 0 to 3 of
    /// `config`. Returns immediately.
    ///
    /// The caller says which report, because the caller knows which bytes it
    /// changed, and only the report holding them needs to go.
    ///
    /// **Held until there is somewhere to send it.** Called before probe() has
    /// started the I/O thread, the report is remembered and goes out the moment
    /// the thread starts, which is how a value restored at start-up reaches the
    /// wheel. Offline it is composed and logged instead, once per change.
    ///
    /// Not gated on a settings write in flight, for the reason setLedModesAsync()
    /// is not.
    void writeWheelConfigAsync(const WheelConfig &config, int chunk);

Q_SIGNALS:
    /// The *settings* changed. Not emitted when a read returns identical bytes,
    /// so an idle device costs the UI nothing.
    ///
    /// Deliberately says nothing about the live telemetry below. This one wakes
    /// MainSettingsPage into walking every control and rebuilds the device's info
    /// fields, which is right for a value someone changed and wrong twice a
    /// second for a motor that warmed up.
    void statusRefreshed();

    /// The live telemetry changed, or arrived, or stopped arriving.
    ///
    /// Fires on nearly every poll on a running base, because that is what live
    /// means. Only the Status page listens.
    void baseStatusRefreshed();

    /// A read failed. Emitted once per run of failures, not once per poll.
    void readFailed(const QString &error);

    void applyStarted();
    void applyFinished(bool ok, const QString &error, int blocksWritten, bool dryRun);

    /// The dry-run gate moved. The checkbox lives on the FFB Settings page, but
    /// it governs telemetry too, so the Telemetry page's account of what is
    /// happening has to follow it.
    void dryRunChanged(bool dryRun);

    /// LED writes started failing, or started working again. Relayed from the
    /// I/O thread unchanged; see AlphaEvoIo::ledColoursSent, and note that this
    /// is the edge rather than one per write.
    void ledColoursSent(bool ok, const QString &error, int framesSent, int framesTotal,
                        bool dryRun);

    /// The wheel is now showing this mode. Relayed from the animator.
    void ledModeChanged(hidcenter::simagic_alpha_evo::LedMode mode);

    /// Wheel configuration writes started failing, or started working again.
    /// Relayed from the I/O thread unchanged; the edge, not every write.
    void wheelConfigSent(bool ok, const QString &error, bool dryRun);

private Q_SLOTS:
    void onStatusRead(const hidcenter::simagic_alpha_evo::StatusSnapshot &snapshot);
    void onApplyFinished(bool ok, const QString &error, int blocksWritten, bool dryRun);
    void onActiveLedModeChanged(hidcenter::simagic_alpha_evo::LedMode mode);

private:
    /// Candidate interfaces, best guess first. See the .cpp for the ordering.
    QVector<UsbInterfaceInfo> probeOrder() const;

    /// Claim the interface carrying the LED ring's endpoint, so the I/O thread
    /// has it to write through. Says so in the log either way and never fails
    /// the probe: the base configures perfectly with a dark ring.
    void openLedInterface();

    void startIoThread();

    /// Stop and join the I/O thread. Called from the destructor and whenever the
    /// handle it drives goes away, so the worker never outlives the descriptor.
    void stopIoThread();

    /// Start the clock. Called from the constructor, hardware or not: see
    /// animator().
    void startAnimationThread();
    void stopAnimationThread();

    /// The device, and the one interface of it this module currently drives.
    ///
    /// Two pointers rather than one because the configuration protocol is only
    /// part of what this base has to say: the LED ring wants an endpoint on
    /// another interface, and more of the device is coming. The device is what
    /// the rest is reached through; the interface is what today's feature
    /// reports go over.
    IUsbDevice *m_usbDevice = nullptr;
    IUsbDeviceHidInterface *m_hidInterface = nullptr;
    std::optional<UsbInterfaceInfo> m_interface;

    /// The interface carrying the LED ring's endpoint, which is not the one the
    /// configuration protocol answers on. Null when the base has no such
    /// endpoint or it could not be claimed.
    IUsbDeviceInterface *m_ledInterface = nullptr;

    QThread *m_ioThread = nullptr;
    AlphaEvoIo *m_io = nullptr;

    QThread *m_animThread = nullptr;
    AlphaEvoAnimator *m_animator = nullptr;

    HidDriverInfo m_driver;

    /// Apply changes to the in-memory status and report success, for when there
    /// is no wheelbase to send them to.
    void applyOffline(const SettingValues &changes);

    /// Compose the active mode's LED packet and log it, for when there is no
    /// wheelbase to send it to.
    ///
    /// Unlike applyOffline() there is nothing to write back into: the status
    /// report has no LED fields, and the ring widget is already the only copy of
    /// the colours. So this is the log and the signal, and nothing else.
    ///
    /// Driven by a *change* — new settings, or a different mode — rather than by
    /// the clock. An animation offline would otherwise log twenty-five packets a
    /// second forever, which is not a log.
    void logLedColoursOffline();

    /// The BaseId byte the LED packet header carries, from the product id.
    quint8 ledDeviceModel() const;

    StatusReport m_status = {};

    /// The live half of the last poll, and whether there was one. Kept apart from
    /// m_status so the memcmp that decides what changed can be made twice, once
    /// per signal.
    BaseStatusReport m_baseStatus = {};
    bool m_haveBaseStatus = false;

    // Off by default, as in AlphaEvoIo. The two must agree: startIoThread()
    // pushes this value to the worker as its first queued call, so a mismatch
    // would come right only by an ordering argument nobody should have to
    // re-derive from two lines in different files.
    bool m_dryRun = false;
    bool m_busy = false;
    bool m_offline = false;

    /// The last set pushed to the animator, and the mode it says is showing.
    /// Held only so the offline log has something to compose from.
    LedModeSet m_ledModes;
    LedMode m_activeLedMode = LedMode::Idle;
    LedRingColours m_loggedOfflineColours;
    bool m_haveLoggedOffline = false;

    /// The last wheel configuration handed in, and which of its reports are
    /// still waiting for an I/O thread to send them, one bit per chunk.
    WheelConfig m_wheelConfig;
    quint8 m_pendingWheelChunks = 0;
};

} // namespace hidcenter::simagic_alpha_evo
