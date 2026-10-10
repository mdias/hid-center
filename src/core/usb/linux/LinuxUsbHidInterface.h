// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>

#include <atomic>

#include "core/usb/IUsbDeviceHidInterface.h"

class QSocketNotifier;

namespace hidcenter {

/// An open /dev/hidrawN node.
///
/// Input reports are driven by a QSocketNotifier on the file descriptor rather
/// than by a polling thread, so they are read on the thread that owns this
/// object — the GUI thread — and no caller has to poll for them.
///
/// Transfers, though, may be issued from one worker thread while that notifier
/// keeps running here; see the threading note on IUsbDeviceInterface. Two mutexes make
/// that safe, and they are the only synchronisation in the class:
///
/// - `m_ioMutex` is held across every use of the descriptor in a transfer, and
///   by closeDevice() before it closes. Unplugging therefore waits for an ioctl
///   already in flight instead of pulling the descriptor out from under it and
///   leaving the worker to ioctl an fd number the kernel has since reissued.
/// - `m_errorMutex` guards m_lastError, which either thread can write.
///
/// onReadable() deliberately takes neither. Holding m_ioMutex there would park
/// the GUI thread on every input report for as long as a write takes, which is
/// exactly the stall the worker thread exists to avoid; a concurrent read() and
/// ioctl() on one descriptor is safe, and closeDevice() is only ever reached
/// from this thread.
class LinuxUsbHidInterface final : public IUsbDeviceHidInterface
{
    Q_OBJECT

public:
    /// Open `interfaceInfo.hid.devicePath`. On failure the object is constructed but
    /// isOpen() returns false and lastError() explains why; callers should use
    /// LinuxUsbDevice::openInterface(), which checks this for them.
    explicit LinuxUsbHidInterface(const UsbInterfaceInfo &interfaceInfo, QObject *parent = nullptr);
    ~LinuxUsbHidInterface() override;

    bool isOpen() const override { return m_fd >= 0; }

    bool write(const QByteArray &report) override;
    bool writeInterrupt(quint8 endpoint, const QByteArray &data,
                        int timeoutMs = kDefaultTransferTimeoutMs) override;
    bool readInterrupt(quint8 endpoint, QByteArray &out,
                       int timeoutMs = kDefaultTransferTimeoutMs) override;
    bool getFeatureReport(quint8 reportId, QByteArray &out) override;
    bool getFeatureReport(quint8 reportId, int length, QByteArray &out) override;
    bool setFeatureReport(const QByteArray &report) override;

    const ReportDescriptor &descriptor() const override { return m_descriptor; }
    const UsbInterfaceInfo &interfaceInfo() const override { return m_interface; }
    QString lastError() const override;

    /// Read the raw report descriptor of an already-open fd. Also used during
    /// enumeration to learn an interface's top-level usage without keeping the
    /// node open.
    static QByteArray readRawDescriptor(int fd, QString *error = nullptr);

private Q_SLOTS:
    void onReadable();

private:
    void closeDevice();

    /// Record why the last operation failed. Safe from either thread.
    void setError(const QString &message);

    /// setError() for a failed syscall, using the current errno, and log it.
    /// Returns the message so the caller can announce it *after* releasing
    /// m_ioMutex — a directly connected slot may issue another transfer.
    QString fail(const QString &context);

    /// Largest plausible transfer for `reportId`, from the descriptor when it
    /// declares one and a generous fallback when it does not.
    int featureBufferSize(quint8 reportId) const;

    UsbInterfaceInfo m_interface;
    ReportDescriptor m_descriptor;

    /// The interrupt IN endpoint every input report arrives on, or 0 for a
    /// transport with no endpoints. Resolved once, because it is read on the
    /// notifier's path and a device can produce a thousand reports a second.
    quint8 m_inEndpoint = 0;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_readBuffer;

    /// Guards m_fd against being closed under an in-flight transfer. Held only
    /// for the duration of one ioctl or write, never by the input-report path.
    QMutex m_ioMutex;

    /// Atomic because isOpen() is read from both threads without the mutex; the
    /// mutex above is what orders a close against a transfer, not this.
    std::atomic<int> m_fd = -1;

    mutable QMutex m_errorMutex;
    QString m_lastError;
};

} // namespace hidcenter
