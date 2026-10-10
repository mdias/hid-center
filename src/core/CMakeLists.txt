# SPDX-FileCopyrightText: 2026 Micael Dias
# SPDX-License-Identifier: GPL-3.0-or-later

# hidcenter-core MUST be SHARED.
#
# The app and every device module cast interface pointers (IDevicePlugin,
# IDevice, IUsbDevice) across the .so boundary. That works only if there is a
# single definition of each class's vtable and typeinfo in the process. A static
# core linked into both the executable and each module would give every one its
# own copy, and the casts would fail at runtime in ways that are miserable to
# debug. Keep this SHARED.
add_library(hidcenter-core SHARED
    Logging.cpp
    DeveloperMode.cpp
    usb/UsbDeviceInfo.cpp
    usb/HidDriverInfo.cpp
    usb/IUsbDeviceInterface.cpp
    usb/ReportDescriptor.cpp
    usb/UsbBackendFactory.cpp
    usb/UsbDeviceBase.cpp
    log/LogModel.cpp
    plugin/PluginManager.cpp
    analyzer/UsbPacket.cpp
    analyzer/UsbPacketSource.cpp
    analyzer/PacketLogModel.cpp
    telemetry/TelemetryFrame.cpp
    telemetry/TelemetryHub.cpp
    telemetry/TelemetryRouter.cpp
    telemetry/TelemetrySourceModule.cpp
    telemetry/plugin/TelemetryPluginManager.cpp

    # Headers are listed so AUTOMOC sees the Q_OBJECT classes that have no
    # matching .cpp of their own — the abstract interfaces, chiefly.
    Logging.h
    DeveloperMode.h
    usb/UsbDeviceInfo.h
    usb/HidDriverInfo.h
    usb/IUsbDevice.h
    usb/IUsbDeviceInterface.h
    usb/IUsbDeviceHidInterface.h
    usb/IUsbBackend.h
    usb/ReportDescriptor.h
    usb/UsbDeviceBase.h
    log/LogModel.h
    plugin/IDevice.h
    plugin/IDevicePlugin.h
    plugin/PluginManager.h
    analyzer/UsbPacket.h
    analyzer/PacketFilter.h
    analyzer/IPacketSource.h
    analyzer/UsbPacketSource.h
    analyzer/PacketLogModel.h
    telemetry/TelemetryFrame.h
    telemetry/TelemetryHub.h
    telemetry/TelemetryRouter.h
    telemetry/TelemetrySourceModule.h
    telemetry/plugin/ITelemetrySource.h
    telemetry/plugin/ITelemetrySourcePlugin.h
    telemetry/plugin/TelemetryPluginManager.h
)

generate_export_header(hidcenter-core
    BASE_NAME HIDCENTER_CORE
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/hidcenter/core_export.h")

target_include_directories(hidcenter-core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/hid-center>")

target_link_libraries(hidcenter-core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

# No Qt6::Network here, deliberately. Core used to own the telemetry socket and
# linked it privately for that; sockets now live in telemetry source modules,
# which link Network themselves. Nothing in core opens one any more, and if
# something here ever needs to again, that is worth noticing rather than
# inheriting.

target_compile_definitions(hidcenter-core PRIVATE
    HIDCENTER_INSTALL_PLUGIN_DIR="${HIDCENTER_INSTALL_PLUGIN_DIR}"
    HIDCENTER_PLUGIN_SUBDIR="${HIDCENTER_PLUGIN_SUBDIR}"
    HIDCENTER_INSTALL_TELEMETRY_PLUGIN_DIR="${HIDCENTER_INSTALL_TELEMETRY_PLUGIN_DIR}"
    HIDCENTER_TELEMETRY_PLUGIN_SUBDIR="${HIDCENTER_TELEMETRY_PLUGIN_SUBDIR}")

if(LINUX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)

    target_sources(hidcenter-core PRIVATE
        usb/linux/LinuxUsbHidInterface.cpp
        usb/linux/LinuxUsbInterface.cpp
        usb/linux/LinuxUsbDevice.cpp
        usb/linux/LinuxUsbBackend.cpp
        usb/linux/LinuxHidDriverInfo.cpp
        usb/linux/LinuxUsbHidInterface.h
        usb/linux/LinuxUsbInterface.h
        usb/linux/LinuxUsbDevice.h
        usb/linux/LinuxUsbBackend.h
        usb/linux/LinuxHidDriverInfo.h)

    # PRIVATE: libudev is an implementation detail of the Linux backend and must
    # not leak into the app or the device modules.
    target_link_libraries(hidcenter-core PRIVATE PkgConfig::LIBUDEV)
    target_compile_definitions(hidcenter-core PRIVATE HIDCENTER_BACKEND_LINUX)
elseif(WIN32)
    # Future: usb/windows/WindowsUsbDevice.cpp + WindowsUsbBackend.cpp built on
    # SetupAPI + hid.dll + WinUSB, implementing the same IUsbBackend / IUsbDevice
    # interfaces. UsbBackendFactory picks it up via HIDCENTER_BACKEND_WINDOWS.
    message(WARNING "No USB backend implemented for Windows yet; "
                    "hid-center will build but enumerate no devices.")
else()
    message(WARNING "No USB backend implemented for this platform; "
                    "hid-center will build but enumerate no devices.")
endif()

set_target_properties(hidcenter-core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

install(TARGETS hidcenter-core LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
