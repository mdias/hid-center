# SPDX-FileCopyrightText: 2026 Micael Dias
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)

project(hid-center
    VERSION 0.1.0
    DESCRIPTION "Configurator for USB HID sim-racing devices"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hidden visibility keeps the plugin boundary honest: only symbols we explicitly
# export with HIDCENTER_CORE_EXPORT cross between the app, the core library and the
# device modules. It also matches the Windows dllexport model, so the eventual
# port does not turn up a pile of missing symbols.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(GenerateExportHeader)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(HidCenterPlugin)

# Everything lands in the build tree in the same relative layout as an install,
# so `build/hid-center` finds `build/plugins/*.so` without any install step.
#
# Telemetry sources sit in a directory of their own beneath it. Two loaders, two
# directories: neither ever has to consider — or reject, or log about — the
# other's files.
set(HIDCENTER_PLUGIN_SUBDIR "plugins")
set(HIDCENTER_TELEMETRY_PLUGIN_SUBDIR "plugins/telemetry")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Where installed plugins live; compiled into the app as a fallback search path.
set(HIDCENTER_INSTALL_PLUGIN_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/hid-center/${HIDCENTER_PLUGIN_SUBDIR}")
set(HIDCENTER_INSTALL_TELEMETRY_PLUGIN_DIR
    "${CMAKE_INSTALL_FULL_LIBDIR}/hid-center/${HIDCENTER_TELEMETRY_PLUGIN_SUBDIR}")

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

add_subdirectory(src/core)
add_subdirectory(src/svg)
add_subdirectory(src/app)
add_subdirectory(src/devices)
add_subdirectory(src/telemetry_sources)

install(FILES resources/hid-center.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES resources/hid-center.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

install(FILES udev/70-hid-center.rules
        DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/udev/rules.d"
        COMPONENT udev)

message(STATUS "hid-center ${PROJECT_VERSION}")
message(STATUS "  Qt              : ${Qt6_VERSION}")
message(STATUS "  build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "  plugins (build) : ${CMAKE_BINARY_DIR}/${HIDCENTER_PLUGIN_SUBDIR}")
message(STATUS "  plugins (install): ${HIDCENTER_INSTALL_PLUGIN_DIR}")
message(STATUS "  telemetry (build): ${CMAKE_BINARY_DIR}/${HIDCENTER_TELEMETRY_PLUGIN_SUBDIR}")
message(STATUS "  telemetry (install): ${HIDCENTER_INSTALL_TELEMETRY_PLUGIN_DIR}")
