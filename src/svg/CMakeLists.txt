# SPDX-FileCopyrightText: 2026 Micael Dias
# SPDX-License-Identifier: GPL-3.0-or-later

# hidcenter-svg: reads the geometry out of an SVG file, for modules that keep a
# drawing as SVG so it can be edited in an ordinary editor, but draw it
# themselves.
#
# Static, unlike hidcenter-core, because nothing from it crosses a .so boundary: a
# module reads a file into its own data, so each can carry its own copy. Link
# it PRIVATE.
add_library(hidcenter-svg STATIC
    SvgGeometry.cpp
    SvgGeometry.h
)

target_include_directories(hidcenter-svg
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>")

target_link_libraries(hidcenter-svg PUBLIC Qt6::Gui)
