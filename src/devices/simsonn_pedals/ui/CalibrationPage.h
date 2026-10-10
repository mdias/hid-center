// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QWidget>

#include <optional>

#include "core/usb/UsbDeviceInfo.h"

#include "../CalibrationSession.h"
#include "../ConfigRead.h"

class QLabel;
class QPushButton;
class QTimer;

namespace hidcenter::simsonn_pedals {

class ParamsPoll;
class PedalBar;

/// The pedals as the device's own sensors see them, against the calibration it
/// has stored for them.
///
/// One bar per pedal, showing the live **raw count** — not the position the axis
/// report gives, which the device has already run through its calibration and
/// response curve, but the number underneath it. That is what a calibration
/// needs: the endpoints it stores are raw counts, so they can only be read off a
/// raw reading. The two thumbs on each bar are those stored endpoints, so the
/// live value can be read against what the device believes.
///
/// Two exchanges feed it, and they are different in kind. The stored calibration
/// is read **once** (ConfigRead) because nothing changes it while the page is
/// open. The live counts arrive in the params report, which the device sends
/// only while it is being asked (ParamsPoll) and then at some 500 reports a
/// second — far too much to keep running for a tab nobody is looking at, so the
/// asking starts and stops with this page being shown.
///
/// **The endpoints are also editable**, in two ways that share one set of
/// buttons. *Start Calibration* watches every pedal and stores what it sees;
/// dragging a thumb sets one endpoint of one pedal by hand and leaves the rest of
/// them alone. Either way nothing reaches the device until *Save Calibration*,
/// and *Cancel* puts the bars back to what it holds.
///
/// **A calibration is all this page writes.** Not the tare, not the deadzones and
/// not the response curves, which is why a save has to copy a pedal's curve and
/// deadzones back out of the blob: `<x>_pf` carries a pedal's whole state, so
/// anything not handed back is thrown away. The whole-blob write, report `0x04`,
/// is not used.
class CalibrationPage : public QWidget
{
    Q_OBJECT

public:
    /// `params`, `config` and `vendorInterface` may each be absent; the page
    /// builds either way and says what it is missing. Neither object is owned
    /// here — both outlive this page, so closing and reopening the tab does not
    /// disturb them or make the blob be read again.
    CalibrationPage(const QString &modelName,
                    const std::optional<UsbInterfaceInfo> &vendorInterface, ParamsPoll *params,
                    ConfigRead *config, CalibrationSession *session,
                    QWidget *parent = nullptr);

protected:
    /// Asking for the params is what starts a 500 report/s stream, so it is done
    /// only while this page is on screen — and the device keeps sending for up to
    /// 30 s after the last request whatever we do, which is why hiding the page
    /// does not make the numbers wrong so much as make them stop.
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refreshValues();
    void refreshCalibration();
    void refreshNotice();

    /// Which buttons are offered, and what the rows say about themselves. Driven
    /// entirely by the session's state, so there is one place that decides
    /// whether a calibration is running.
    void refreshSession();

    /// What the thumbs and the line under each bar should show: what the device
    /// holds when nothing is running, what has been measured when something is.
    void refreshEndpoints();

    /// One pedal's row: the bar, the line under it saying what the device has
    /// stored for that pedal, and that stored calibration itself.
    struct Row
    {
        PedalBar *bar = nullptr;
        QLabel *stored = nullptr;

        /// Kept here rather than asked of ConfigRead each time, because the live
        /// values are redrawn sixty times a second and need it for the
        /// percentage. Empty until the blob has been read.
        std::optional<ConfigRead::AxisCalibration> calibration;
    };

    ParamsPoll *m_params = nullptr;
    ConfigRead *m_config = nullptr;
    CalibrationSession *m_session = nullptr;
    std::optional<UsbInterfaceInfo> m_vendorInterface;

    QList<Row> m_rows;

    QPushButton *m_start = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_cancel = nullptr;

    /// Holds Save shut for a moment after a run begins. Start and Save sit on
    /// the same row and a calibration is begun by pressing one of them, so the
    /// gap between them is not the only thing standing between an intended click
    /// and a write to the hardware — this is the other.
    QTimer *m_saveDelay = nullptr;

    /// What the session was doing last time the buttons were worked out, so that
    /// entering a run can be told from being in one.
    CalibrationSession::State m_lastState {};

    /// How the calibration run is going. Separate from m_notice, which is about
    /// the reading not working; this one is about the run.
    QLabel *m_sessionMessage = nullptr;

    /// Says why the bars are empty. Hidden when they are not.
    QLabel *m_notice = nullptr;
};

} // namespace hidcenter::simsonn_pedals
