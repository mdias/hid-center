# SPDX-FileCopyrightText: 2026 Micael Dias
# SPDX-License-Identifier: GPL-3.0-or-later

qt_add_executable(hid-center
    main.cpp
    MainWindow.cpp
    MainWindow.h
    DeviceListModel.cpp
    DeviceListModel.h
    DeviceModule.cpp
    DeviceModule.h
    DeviceView.cpp
    DeviceView.h
    HomePage.cpp
    HomePage.h
    AnalyzerTab.cpp
    AnalyzerTab.h
    LogPanel.cpp
    LogPanel.h
    LogWindow.cpp
    LogWindow.h
    TrafficIndicator.cpp
    TrafficIndicator.h

    # The Telemetry sidebar page: the tabbed page, the list of loaded sources,
    # its detachable Details panel and the second copy that goes in the pop-out
    # window, plus the two custom widgets they draw with.
    telemetry_page/TelemetryPage.cpp
    telemetry_page/TelemetryPage.h
    telemetry_page/TelemetrySourceList.cpp
    telemetry_page/TelemetrySourceList.h
    telemetry_page/TelemetryDetailsPanel.cpp
    telemetry_page/TelemetryDetailsPanel.h
    telemetry_page/TelemetryWindow.cpp
    telemetry_page/TelemetryWindow.h
    telemetry_page/RpmBar.cpp
    telemetry_page/RpmBar.h
    telemetry_page/WheelTempDisplay.cpp
    telemetry_page/WheelTempDisplay.h
)

qt_add_resources(hid-center hid-center-resources
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/hid-center.svg")

target_link_libraries(hid-center PRIVATE hidcenter-core Qt6::Widgets)

target_compile_definitions(hid-center PRIVATE HIDCENTER_VERSION="${PROJECT_VERSION}")

set_target_properties(hid-center PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    WIN32_EXECUTABLE OFF
    MACOSX_BUNDLE OFF)

install(TARGETS hid-center RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
