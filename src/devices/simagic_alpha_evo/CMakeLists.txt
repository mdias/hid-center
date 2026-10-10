# SPDX-FileCopyrightText: 2026 Micael Dias
# SPDX-License-Identifier: GPL-3.0-or-later

hidcenter_add_device_plugin(
    NAME simagic-alpha-evo
    SOURCES
        SimagicAlphaEvoPlugin.cpp
        SimagicAlphaEvoPlugin.h
        SimagicAlphaEvoDevice.cpp
        SimagicAlphaEvoDevice.h
        ui/StatusPage.cpp
        ui/StatusPage.h
        ui/MainSettingsPage.cpp
        ui/MainSettingsPage.h
        ui/TelemetryPage.cpp
        ui/TelemetryPage.h
        ui/WheelbaseLedPage.cpp
        ui/WheelbaseLedPage.h
        WheelLedController.cpp
        WheelLedController.h
        WheelConfigController.cpp
        WheelConfigController.h
        AlphaEvoWheelConfig.h
        ui/LedRing.cpp
        ui/LedRing.h
        ui/WheelPage.cpp
        ui/WheelPage.h
        ui/WheelSchematic.cpp
        ui/WheelSchematic.h
        ui/WheelLayouts.cpp
        ui/WheelLayouts.h
        ui/WheelControlDialog.cpp
        ui/WheelControlDialog.h
        ui/WheelButtonControlDialog.cpp
        ui/WheelButtonControlDialog.h
        ui/WheelRpmLightsDialog.cpp
        ui/WheelRpmLightsDialog.h
        ui/WheelColourSwatches.cpp
        ui/WheelColourSwatches.h
        ui/WheelInputsControlDialog.cpp
        ui/WheelInputsControlDialog.h
        ui/ButtonMappingChoice.cpp
        ui/ButtonMappingChoice.h
        WheelButtonColours.h
        WheelLedTriggers.h
        WheelRpmLights.h
        ui/TraceGraph.cpp
        ui/TraceGraph.h
        LedModes.cpp
        LedModes.h
        LedModeStore.cpp
        LedModeStore.h
        SimagicLeds.h
        AlphaEvoProto3.h
        AlphaEvoLedProtocol.h
        AlphaEvoTelemetry.cpp
        AlphaEvoTelemetry.h
        AlphaEvoForceTrace.cpp
        AlphaEvoForceTrace.h
        AlphaEvoButtonInput.cpp
        AlphaEvoButtonInput.h
        ui/ClutchPaddlesView.cpp
        ui/ClutchPaddlesView.h
        ui/PressedButtonsView.cpp
        ui/PressedButtonsView.h
        SimagicTelemetry.h
        AlphaEvoSettings.cpp
        AlphaEvoSettings.h
        AlphaEvoIo.cpp
        AlphaEvoIo.h
        AlphaEvoAnimator.cpp
        AlphaEvoAnimator.h
        AlphaEvoBlocks.cpp
        AlphaEvoBlocks.h
        AlphaEvoSpecs.cpp
        AlphaEvoSpecs.h
        AlphaEvoProtocol.h
)

target_link_libraries(hidcenter-simagic-alpha-evo PRIVATE hidcenter-svg)

# The widgets and pages are under ui/, and include the module's other headers by
# their plain names.
target_include_directories(hidcenter-simagic-alpha-evo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The wheels the Wheel tab draws, one SVG each. See "Layouts are SVG files" in
# README.md for what they may contain.
qt_add_resources(hidcenter-simagic-alpha-evo simagic_alpha_evo_wheels
    PREFIX "/simagic_alpha_evo"
    FILES
        ui/wheels/gtc.svg
        ui/wheels/gt_neo.svg
)
