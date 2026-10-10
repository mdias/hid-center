# SPDX-FileCopyrightText: 2026 Micael Dias
# SPDX-License-Identifier: GPL-3.0-or-later

# hidcenter_add_device_plugin(NAME <name> SOURCES <src>...)
#
# Declares one device-family module. The module is built as a MODULE library
# (dlopen'd, never linked against), dropped into the build tree's plugin
# directory so the app finds it without installing, and linked to hidcenter-core.
#
# Adding support for a new device family should need nothing more than a new
# directory under src/devices/ and one call to this function.
function(hidcenter_add_device_plugin)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "hidcenter_add_device_plugin: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "hidcenter_add_device_plugin(${ARG_NAME}): SOURCES is required")
    endif()

    set(target "hidcenter-${ARG_NAME}")

    add_library(${target} MODULE ${ARG_SOURCES})

    target_link_libraries(${target} PRIVATE hidcenter-core Qt6::Widgets)

    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${HIDCENTER_PLUGIN_SUBDIR}"
        # A MODULE has no SONAME/version decoration; keep the file name predictable.
        PREFIX "lib")

    install(TARGETS ${target}
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/hid-center/${HIDCENTER_PLUGIN_SUBDIR}"
            COMPONENT plugins)
endfunction()

# hidcenter_add_telemetry_source_plugin(NAME <name> SOURCES <src>...)
#
# Declares one telemetry-source module: a sim's wire format, its socket and the
# decoding between them. Built the same way as a device module, but dropped into
# a directory of its own so that neither loader ever has to consider the other's
# files.
#
# Links Qt6::Network rather than Qt6::Widgets — a source has no UI. hidcenter-core
# brings the rest, including TelemetryFrame and the two interfaces.
#
# Adding support for a new sim should need nothing more than a new directory
# under src/telemetry_sources/ and one call to this function.
function(hidcenter_add_telemetry_source_plugin)
    cmake_parse_arguments(ARG "" "NAME" "SOURCES" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "hidcenter_add_telemetry_source_plugin: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "hidcenter_add_telemetry_source_plugin(${ARG_NAME}): SOURCES is required")
    endif()

    set(target "hidcenter-telemetry-${ARG_NAME}")

    add_library(${target} MODULE ${ARG_SOURCES})

    target_link_libraries(${target} PRIVATE hidcenter-core Qt6::Network)

    set_target_properties(${target} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${HIDCENTER_TELEMETRY_PLUGIN_SUBDIR}"
        # A MODULE has no SONAME/version decoration; keep the file name predictable.
        PREFIX "lib")

    install(TARGETS ${target}
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/hid-center/${HIDCENTER_TELEMETRY_PLUGIN_SUBDIR}"
            COMPONENT plugins)
endfunction()
