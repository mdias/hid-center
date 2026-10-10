// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/MainWindow.h"
#include "core/DeveloperMode.h"
#include "core/Logging.h"
#include "core/analyzer/UsbPacket.h"
#include "core/usb/IUsbBackend.h"
#include "core/log/LogModel.h"
#include "core/plugin/PluginManager.h"
#include "core/telemetry/TelemetryFrame.h"
#include "core/telemetry/TelemetryRouter.h"
#include "core/telemetry/plugin/TelemetryPluginManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QTextStream>

using namespace hidcenter;

namespace {

/// Dump every HID device and its parsed reports, then exit.
///
/// This exists to check the report-descriptor parser against real hardware
/// without needing one of the supported devices — descriptors are read from
/// sysfs, so any mouse or keyboard will do. Cross-check the output against
/// `usbhid-dump` or `hid-decode`.
int dumpDevices()
{
    QTextStream out(stdout);

    std::unique_ptr<IUsbBackend> backend = createUsbBackend();
    if (!backend) {
        QTextStream(stderr) << "No USB backend is available on this platform.\n";
        return 1;
    }

    const QVector<UsbDeviceInfo> devices = backend->enumerate();
    out << devices.size() << " device(s) via the " << backend->name() << " backend\n\n";

    for (const UsbDeviceInfo &device : devices) {
        out << device.id.toString() << "  " << device.describe() << "\n";
        out << "  path   : " << device.devicePath << "\n";
        if (!device.serial.isEmpty())
            out << "  serial : " << device.serial << "\n";

        if (device.busNumber > 0) {
            out << "  usb    : " << QStringLiteral("/dev/bus/usb/%1/%2")
                                        .arg(device.busNumber, 3, 10, QLatin1Char('0'))
                                        .arg(device.deviceNumber, 3, 10, QLatin1Char('0'))
                << "\n";
        }

        for (const UsbInterfaceInfo &iface : device.interfaces) {
            const ReportDescriptor &descriptor = iface.hid.descriptor;
            out << "  " << iface.displayName() << "  class "
                << QStringLiteral("%1").arg(iface.interfaceClass, 2, 16, QLatin1Char('0'))
                << "  driver " << (iface.kernelDriver.isEmpty() ? QStringLiteral("(none)")
                                                                : iface.kernelDriver)
                << (iface.driver.driver.isEmpty() ? QString()
                                                  : QStringLiteral(" / ") + iface.driver.driver)
                << "\n";

            for (const UsbEndpointInfo &endpoint : iface.endpoints)
                out << "      ep " << endpoint.describe() << "\n";

            if (!iface.isHid()) {
                out << "      no HID node — nothing here speaks reports\n";
                continue;
            }

            out << "      usage "
                << QStringLiteral("%1:%2")
                       .arg(iface.hid.usagePage, 4, 16, QLatin1Char('0'))
                       .arg(iface.hid.usage, 4, 16, QLatin1Char('0'))
                << "  (" << descriptor.raw().size() << " descriptor bytes, "
                << descriptor.reports().size() << " reports"
                << (descriptor.usesReportIds() ? ", uses report ids" : ", no report ids")
                << ")\n";

            for (const ReportInfo &report : descriptor.reports()) {
                out << "      "
                    << QStringLiteral("%1 id 0x%2  %3 bytes  %4 field(s)")
                           .arg(reportTypeName(report.type), -7)
                           .arg(report.id, 2, 16, QLatin1Char('0'))
                           .arg(descriptor.transferLength(report), 3)
                           .arg(report.fields.size())
                    << "\n";

                for (const ReportField &field : report.fields) {
                    out << "          "
                        << QStringLiteral("bit %1..%2  %3 x %4 bits  usage %5:%6  [%7..%8]  %9")
                               .arg(field.bitOffset)
                               .arg(field.bitOffset + field.totalBits() - 1)
                               .arg(field.count)
                               .arg(field.bitSize)
                               .arg(field.usagePage, 4, 16, QLatin1Char('0'))
                               .arg(field.usage, 4, 16, QLatin1Char('0'))
                               .arg(field.logicalMin)
                               .arg(field.logicalMax)
                               .arg(field.flagsDescription())
                        << "\n";
                }
            }
        }
        out << "\n";
    }

    return 0;
}

void registerMetaTypes()
{
    qRegisterMetaType<UsbDeviceInfo>("hidcenter::UsbDeviceInfo");
    qRegisterMetaType<UsbPacket>("hidcenter::UsbPacket");
    qRegisterMetaType<TrafficDirection>("hidcenter::TrafficDirection");

    // A frame crosses from a source's own thread to the router on this one.
    qRegisterMetaType<TelemetryFrame>("hidcenter::TelemetryFrame");
}

/// Make the application's own info-level output visible by default.
///
/// The log window is only useful if there is something in it, and a device
/// module's messages — above all the hex a dry-run write *would* have sent —
/// are meant for the user, not for a developer who knew to set an env var.
///
/// Applied only when QT_LOGGING_RULES is unset, because setFilterRules() takes
/// precedence over the environment: doing this unconditionally would quietly
/// override whatever the user asked for on the command line.
void applyDefaultLoggingRules()
{
    if (!qEnvironmentVariableIsEmpty("QT_LOGGING_RULES"))
        return;
    QLoggingCategory::setFilterRules(QStringLiteral("hidcenter.*.info=true"));
}

/// True when --dump-hid appears in the arguments. Checked before constructing an
/// application object, so the dump works over SSH with no display.
bool wantsDump(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--dump-hid") == 0)
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("hid-center"));
    QCoreApplication::setApplicationName(QStringLiteral("hid-center"));
    QCoreApplication::setApplicationVersion(QStringLiteral(HIDCENTER_VERSION));

    registerMetaTypes();

    if (wantsDump(argc, argv)) {
        QCoreApplication app(argc, argv);
        return dumpDevices();
    }

    QApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("hid-center"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/hid-center.svg")));

    applyDefaultLoggingRules();
    // Before anything else logs, so the log window opens with the full record of
    // plugin loading and device enumeration rather than starting halfway in.
    LogModel::install();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Configure supported USB HID devices."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("dump-hid"),
                      QCoreApplication::translate(
                          "main", "List every HID device with its parsed reports, then exit.")});
    parser.addOption({QStringLiteral("dev"),
                      QCoreApplication::translate(
                          "main", "Enable developer tools (protocol analyzer, dry run).")});
    parser.process(app);
    setDeveloperMode(parser.isSet(QStringLiteral("dev")));

    auto plugins = std::make_unique<PluginManager>();
    plugins->loadPlugins();

    // Its own loader and its own directory: a telemetry source is not a device,
    // and neither loader should be sifting through the other's modules.
    auto telemetryPlugins = std::make_unique<TelemetryPluginManager>();
    telemetryPlugins->loadPlugins();

    // Listening starts here rather than when the Telemetry page is first opened:
    // a wheelbase is fed from the moment a game starts sending, whether or not
    // anyone has gone looking at the numbers.
    auto telemetry = std::make_unique<TelemetryRouter>(telemetryPlugins->sourcePlugins());
    telemetry->start();

    std::unique_ptr<IUsbBackend> backend = createUsbBackend();
    if (backend && !backend->startMonitoring())
        qCWarning(hidcenterApp, "Hotplug detection is unavailable; the device list will not update.");

    MainWindow window(plugins.get(), backend.get(), telemetry.get());
    window.show();

    return app.exec();
}
