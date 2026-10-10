// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/usb/linux/LinuxUsbHidInterface.h"

#include "core/Logging.h"

#include <QMutexLocker>
#include <QSocketNotifier>

#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace hidcenter {
namespace {

/// hidraw accepts up to HID_MAX_BUFFER_SIZE per transfer; read into that much so
/// a single read() never truncates a report.
constexpr int kReadBufferSize = 4096;

/// Used when the descriptor declares no Feature report of the requested ID —
/// large enough for anything a HID device can send in one transfer.
constexpr int kFallbackFeatureSize = 256;

QString errnoString(int err)
{
    char buffer[256] = {};
    // The GNU strerror_r may return a pointer to its own static string rather
    // than filling the buffer, so use whatever it hands back.
    const char *message = strerror_r(err, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(message);
}

} // namespace

LinuxUsbHidInterface::LinuxUsbHidInterface(const UsbInterfaceInfo &interfaceInfo, QObject *parent)
    : IUsbDeviceHidInterface(parent)
    , m_interface(interfaceInfo)
{
    m_readBuffer.resize(kReadBufferSize);

    for (const UsbEndpointInfo &endpoint : m_interface.endpoints) {
        if (endpoint.isIn() && endpoint.type == UsbTransferType::Interrupt) {
            m_inEndpoint = endpoint.address;
            break;
        }
    }

    // O_NONBLOCK so the QSocketNotifier drives reads and nothing ever blocks the
    // GUI thread. O_RDWR because output reports go out through the same fd.
    m_fd = ::open(m_interface.hid.devicePath.toLocal8Bit().constData(),
                  O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        const int err = errno;
        QString message = QStringLiteral("Cannot open %1: %2")
                              .arg(m_interface.hid.devicePath, errnoString(err));
        if (err == EACCES || err == EPERM) {
            message += QStringLiteral(
                " — install udev/70-hid-center.rules and replug the device to grant access.");
        }
        setError(message);
        qCWarning(hidcenterHid) << message;
        return;
    }

    QString descriptorError;
    const QByteArray raw = readRawDescriptor(m_fd, &descriptorError);
    if (raw.isEmpty()) {
        qCWarning(hidcenterHid) << "No report descriptor for" << m_interface.hid.devicePath << ":"
                             << descriptorError;
    } else {
        QString parseError;
        m_descriptor = ReportDescriptor::parse(raw, &parseError);
        if (!parseError.isEmpty()) {
            // A partially decoded descriptor is still worth having: the analyzer
            // shows what it understood and the raw bytes remain available.
            qCWarning(hidcenterHid) << "Report descriptor of" << m_interface.hid.devicePath
                                 << "is malformed:" << parseError;
        }
    }

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &LinuxUsbHidInterface::onReadable);
    m_notifier->setEnabled(true);

    qCDebug(hidcenterHid) << "Opened" << m_interface.hid.devicePath << "with"
                       << m_descriptor.reports().size() << "report(s)";
}

LinuxUsbHidInterface::~LinuxUsbHidInterface()
{
    closeDevice();
}

QByteArray LinuxUsbHidInterface::readRawDescriptor(int fd, QString *error)
{
    int size = 0;
    if (::ioctl(fd, HIDIOCGRDESCSIZE, &size) < 0) {
        if (error)
            *error = QStringLiteral("HIDIOCGRDESCSIZE: %1").arg(errnoString(errno));
        return {};
    }
    if (size <= 0 || size > HID_MAX_DESCRIPTOR_SIZE) {
        if (error)
            *error = QStringLiteral("implausible descriptor size %1").arg(size);
        return {};
    }

    hidraw_report_descriptor descriptor = {};
    descriptor.size = quint32(size);
    if (::ioctl(fd, HIDIOCGRDESC, &descriptor) < 0) {
        if (error)
            *error = QStringLiteral("HIDIOCGRDESC: %1").arg(errnoString(errno));
        return {};
    }

    return QByteArray(reinterpret_cast<const char *>(descriptor.value), size);
}

void LinuxUsbHidInterface::onReadable()
{
    // Drain the fd: several reports can queue up between notifications, and
    // leaving any behind would show up as latency in the analyzer.
    while (m_fd >= 0) {
        const ssize_t n = ::read(m_fd, m_readBuffer.data(), size_t(m_readBuffer.size()));
        if (n > 0) {
            const QByteArray report(m_readBuffer.constData(), int(n));
            Q_EMIT inputReport(report);

            // Every HID input report is an interrupt IN on this interface's IN
            // endpoint; naming it is what lets the analyzer tell two interfaces'
            // streams apart by more than their node path.
            Q_EMIT transferred(TrafficDirection::In, m_inEndpoint, report);
            continue;
        }

        if (n == 0)
            return; // Nothing pending.

        const int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK)
            return;
        if (err == EINTR)
            continue;

        if (err == ENODEV || err == EIO) {
            // The device was unplugged. Stop the notifier before emitting, so a
            // slot cannot re-enter this function on a dead fd.
            qCDebug(hidcenterHid) << m_interface.hid.devicePath << "disconnected";
            closeDevice();
            Q_EMIT disconnected();
            return;
        }

        Q_EMIT errorOccurred(fail(QStringLiteral("read")));
        return;
    }
}

bool LinuxUsbHidInterface::write(const QByteArray &report)
{
    if (report.isEmpty()) {
        setError(QStringLiteral("Refusing to write an empty report"));
        return false;
    }

    ssize_t written = 0;
    QString failure;
    {
        // The descriptor is checked inside the lock, so a close that was waiting
        // on it cannot slip in between the check and the write.
        const QMutexLocker locker(&m_ioMutex);
        if (m_fd < 0) {
            setError(QStringLiteral("Device is not open"));
            return false;
        }
        written = ::write(m_fd, report.constData(), size_t(report.size()));
        if (written < 0)
            failure = fail(QStringLiteral("write"));
    }

    // Announced outside the lock: a slot reached by a direct connection is free
    // to turn round and issue another transfer, which would deadlock in here.
    if (!failure.isEmpty()) {
        Q_EMIT errorOccurred(failure);
        return false;
    }

    if (written != report.size()) {
        const QString message = QStringLiteral("Short write: %1 of %2 bytes")
                                    .arg(written)
                                    .arg(report.size());
        setError(message);
        Q_EMIT errorOccurred(message);
        return false;
    }

    Q_EMIT reportWritten(report);

    // The endpoint the kernel just used, as far as we can know it: an interface
    // with an interrupt OUT endpoint gets one, and one without went out through
    // the control pipe and reports endpoint 0.
    const UsbEndpointInfo *out = m_interface.interruptOutEndpoint();
    Q_EMIT transferred(TrafficDirection::Out, out ? out->address : 0, report);
    return true;
}

bool LinuxUsbHidInterface::writeInterrupt(quint8 endpoint, const QByteArray &data, int timeoutMs)
{
    // Nothing here waits on a timeout: the write below is the kernel's own
    // hidraw path and blocks for as long as it blocks. Accepted and ignored so
    // one call shape covers both routes; see LinuxUsbInterface, where it is real.
    Q_UNUSED(timeoutMs);

    // A hidraw node reaches exactly one endpoint — the one usbhid sends output
    // reports on — so this refuses anything else rather than quietly sending it
    // somewhere the caller did not ask for. An endpoint on another interface, or
    // this interface's IN endpoint, lands here.
    const UsbEndpointInfo *out = m_interface.interruptOutEndpoint();
    if (!out || out->address != endpoint) {
        setError(QStringLiteral("Endpoint 0x%1 is not reachable through %2; it sends output "
                                "reports on %3")
                     .arg(endpoint, 2, 16, QLatin1Char('0'))
                     .arg(m_interface.hid.devicePath,
                          out ? QStringLiteral("0x%1").arg(out->address, 2, 16, QLatin1Char('0'))
                              : QStringLiteral("no interrupt OUT endpoint")));
        return false;
    }

    // write() on the node *is* the interrupt OUT: usbhid submits an URB on this
    // endpoint and only falls back to a control SET_REPORT for an interface that
    // has none, which the check above has already ruled out.
    return write(data);
}

bool LinuxUsbHidInterface::readInterrupt(quint8 endpoint, QByteArray &out, int timeoutMs)
{
    Q_UNUSED(endpoint);
    Q_UNUSED(out);
    Q_UNUSED(timeoutMs);

    // Deliberately refused rather than implemented. The notifier above is already
    // draining this interface's IN endpoint into inputReport(), and a second
    // reader would take packets from it at random — every report this one caught
    // is a report the signal never emits. Connect to inputReport() instead.
    setError(QStringLiteral("Input on %1 arrives through inputReport(); it cannot also be read "
                            "directly").arg(m_interface.hid.devicePath));
    return false;
}

int LinuxUsbHidInterface::featureBufferSize(quint8 reportId) const
{
    if (const ReportInfo *info = m_descriptor.find(reportId, ReportType::Feature)) {
        const int length = m_descriptor.transferLength(*info);
        if (length > 0)
            return length;
    }
    return kFallbackFeatureSize;
}

bool LinuxUsbHidInterface::getFeatureReport(quint8 reportId, QByteArray &out)
{
    return getFeatureReport(reportId, featureBufferSize(reportId), out);
}

bool LinuxUsbHidInterface::getFeatureReport(quint8 reportId, int length, QByteArray &out)
{
    if (length <= 0) {
        setError(QStringLiteral("Refusing to read a feature report of %1 bytes").arg(length));
        return false;
    }

    QByteArray buffer(length, Qt::Uninitialized);
    // hidraw takes the requested report ID in byte 0 and writes the response
    // back over the same buffer, ID included.
    buffer[0] = char(reportId);

    int result = 0;
    QString failure;
    {
        const QMutexLocker locker(&m_ioMutex);
        if (m_fd < 0) {
            setError(QStringLiteral("Device is not open"));
            return false;
        }
        result = ::ioctl(m_fd, HIDIOCGFEATURE(buffer.size()), buffer.data());
        if (result < 0)
            failure = fail(QStringLiteral("HIDIOCGFEATURE"));
    }

    if (!failure.isEmpty()) {
        Q_EMIT errorOccurred(failure);
        return false;
    }

    buffer.resize(result);
    out = buffer;
    Q_EMIT featureExchanged(TrafficDirection::FeatureGet, out);
    Q_EMIT transferred(TrafficDirection::FeatureGet, 0, out);
    return true;
}

bool LinuxUsbHidInterface::setFeatureReport(const QByteArray &report)
{
    if (report.isEmpty()) {
        setError(QStringLiteral("Refusing to send an empty feature report"));
        return false;
    }

    QByteArray buffer = report;
    QString failure;
    {
        const QMutexLocker locker(&m_ioMutex);
        if (m_fd < 0) {
            setError(QStringLiteral("Device is not open"));
            return false;
        }
        if (::ioctl(m_fd, HIDIOCSFEATURE(buffer.size()), buffer.data()) < 0)
            failure = fail(QStringLiteral("HIDIOCSFEATURE"));
    }

    if (!failure.isEmpty()) {
        Q_EMIT errorOccurred(failure);
        return false;
    }

    Q_EMIT featureExchanged(TrafficDirection::FeatureSet, report);
    Q_EMIT transferred(TrafficDirection::FeatureSet, 0, report);
    return true;
}

QString LinuxUsbHidInterface::lastError() const
{
    const QMutexLocker locker(&m_errorMutex);
    return m_lastError;
}

void LinuxUsbHidInterface::setError(const QString &message)
{
    const QMutexLocker locker(&m_errorMutex);
    m_lastError = message;
}

QString LinuxUsbHidInterface::fail(const QString &context)
{
    // errno is thread-local, so this is the failing thread's own error even when
    // the call came from a worker.
    const int err = errno;
    const QString message = QStringLiteral("%1 on %2 failed: %3")
                                .arg(context, m_interface.hid.devicePath, errnoString(err));
    setError(message);
    qCWarning(hidcenterHid) << message;
    return message;
}

void LinuxUsbHidInterface::closeDevice()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }

    // Waits for a transfer already in flight on a worker thread. Closing under
    // one would leave it ioctl'ing a descriptor number the kernel is free to
    // hand to the next thing that opens a file.
    const QMutexLocker locker(&m_ioMutex);
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

} // namespace hidcenter
