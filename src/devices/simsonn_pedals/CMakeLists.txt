# SPDX-FileCopyrightText: 2026 Micael Dias
# SPDX-License-Identifier: GPL-3.0-or-later

hidcenter_add_device_plugin(
    NAME simsonn_pedals
    SOURCES
        SimsonnPedalsPlugin.cpp
        SimsonnPedalsPlugin.h
        SimsonnPedalsDevice.cpp
        SimsonnPedalsDevice.h
        ui/StatusPage.cpp
        ui/StatusPage.h
        FirmwareProbe.cpp
        FirmwareProbe.h
        ConfigRead.cpp
        ConfigRead.h
        ui/MainSettingsPage.cpp
        ui/MainSettingsPage.h
        ui/CalibrationPage.cpp
        ui/CalibrationPage.h
        CalibrationSession.cpp
        CalibrationSession.h
        ConfigWrite.cpp
        ConfigWrite.h
        BrakeOptionsRead.cpp
        BrakeOptionsRead.h
        SettingsSave.cpp
        SettingsSave.h
        PedalWrite.cpp
        PedalWrite.h
        ui/PedalBar.cpp
        ui/PedalBar.h
        ui/CurveEditor.cpp
        ui/CurveEditor.h
        ui/RangeSlider.cpp
        ui/RangeSlider.h
        PedalInput.cpp
        PedalInput.h
        Pedals.h
        ParamsPoll.cpp
        ParamsPoll.h
)
