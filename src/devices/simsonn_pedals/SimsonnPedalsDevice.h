// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include "core/usb/IUsbDevice.h"
#include "core/plugin/IDevice.h"

namespace hidcenter::simsonn_pedals {

class BrakeOptionsRead;
class CalibrationSession;
class ConfigRead;
class FirmwareProbe;
class ParamsPoll;
class PedalInput;
class SettingsSave;

/// One connected SIMSONN pedal set.
class SimsonnPedalsDevice : public IDevice
{
    Q_OBJECT

public:
    SimsonnPedalsDevice(IUsbDevice *context, QString modelName, QObject *parent = nullptr);

    bool initialize(QString *error) override;
    QVector<DevicePage> pages() override;
    QVector<DeviceInfoField> infoFields() const override;

private:
    IUsbDevice *m_context = nullptr;
    QString m_modelName;

    /// The interface the configuration protocol will use, identified during
    /// initialize(). Empty when the device exposes no vendor-defined interface.
    std::optional<UsbInterfaceInfo> m_vendorInterface;

    /// The one thing this module asks the hardware for so far. Owned here rather
    /// than by the Status page, and started in initialize(), because the version
    /// is wanted in the device header whether or not that tab is ever opened —
    /// and because a page that is closed and rebuilt should not re-probe.
    FirmwareProbe *m_probe = nullptr;

    /// The pedal positions, for the graphs on the settings page. Owned here for
    /// the same reason as the probe: a page that is closed and rebuilt should not
    /// take the reading with it. It stays switched off until a page shows it.
    PedalInput *m_pedals = nullptr;

    /// The raw sensor counts, for the Calibration page. Built here but never
    /// started here: asking for them puts the device into a 500 report/s stream,
    /// so only a page that is on screen turns it on. Null when the device has no
    /// vendor interface to ask.
    ParamsPoll *m_params = nullptr;

    /// The stored configuration blob: the response curves and deadzones the
    /// Main Settings page shows, and the calibration endpoints the Calibration
    /// page draws. Read once at connection, because nothing but the pedals
    /// themselves can change it and both pages want it as soon as they are
    /// opened. Null when the device has no vendor interface to ask.
    ConfigRead *m_config = nullptr;

    /// A calibration run, if one is ever started. Owned here rather than by the
    /// page so that switching tabs in the middle of pressing the pedals does not
    /// throw the measurement away.
    CalibrationSession *m_calibration = nullptr;

    /// The brake's elastomer and throttle cut, which the blob does not carry.
    /// Read at connection alongside it.
    BrakeOptionsRead *m_brakeOptions = nullptr;

    /// A save from the Main Settings page. Owned here for the same reason as the
    /// calibration: closing the tab while the read-back is under way should not
    /// lose the answer. Null when the device has no vendor interface to write to.
    SettingsSave *m_settingsSave = nullptr;
};

} // namespace hidcenter::simsonn_pedals
