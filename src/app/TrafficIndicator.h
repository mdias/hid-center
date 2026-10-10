// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QPoint>
#include <QWidget>

#include <optional>

#include "core/usb/IUsbDeviceHidInterface.h"

class QTimer;

namespace hidcenter {

/// Arrows showing traffic: down for what comes in, up for what goes out.
///
/// Two of them for a device, which is written to as well as read from; one for a
/// source that only ever arrives, such as telemetry. See Kind.
///
/// Four states per arrow, because "nothing to report", "nothing happening right
/// now" and "more than can be counted" are different things and must not look
/// alike:
///
/// - **unavailable** — hollow outline in the palette's muted tone. There is no
///   traffic source, or nothing open for it to observe.
/// - **idle** — filled, its own colour dimmed. Live, and quiet.
/// - **lit** — filled, its own colour vivid, for a moment after each packet.
/// - **saturated** — alternating between lit and idle several times a second,
///   for traffic too dense to ever let the glow expire.
///
/// Hollow versus filled is what separates the first from the rest; colour alone
/// would not survive a light theme, a dark theme and colour-blind viewers. The
/// colour is what names the direction, which is why an idle arrow keeps it.
///
/// Hovering an arrow gives that direction's throughput per second.
class TrafficIndicator : public QWidget
{
    Q_OBJECT

public:
    /// What this indicator is watching. Decides how many arrows it has and what
    /// its tooltips call the things it counts.
    ///
    /// One knob rather than three — arrow count, unit noun, per-arrow
    /// description — because there are two callers and no third in sight, and
    /// because it keeps every sentence whole inside the widget. Assembling
    /// "Incoming: %1 %2/s" from a noun supplied by a caller would hand
    /// translators a jigsaw of fragments.
    enum class Kind {
        HidReports,        ///< Both directions: a device is written to as well as read from.
        TelemetryPackets,  ///< Incoming only: telemetry arrives and nothing goes back.
    };

    explicit TrafficIndicator(Kind kind = Kind::HidReports, QWidget *parent = nullptr);

    /// Light the arrow this direction belongs to. Cheap enough to call for every
    /// packet on a device streaming input reports at a kilohertz.
    void noteTraffic(TrafficDirection direction);

    /// Grey both arrows out, with `reason` as the tooltip explaining why.
    void setAvailable(bool available, const QString &reason = {});

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

    /// Handles QEvent::ToolTip, so each arrow can answer for itself.
    bool event(QEvent *event) override;

private:
    /// One arrow's worth of state.
    struct Arrow
    {
        QElapsedTimer since;     ///< Last packet; invalid until one arrives.
        QElapsedTimer litSince;  ///< When it last went idle → lit; invalid while idle.
        quint64 count = 0;       ///< Packets so far in the open rate window.
        int rate = 0;            ///< Reports per second, from the last closed window.
        bool lit = false;        ///< What was last painted, blink phase included.
        bool saturated = false;  ///< Lit for so long that it has started blinking.
    };

    /// Whether there is a second arrow at all. Everything the kind changes —
    /// the width, the hover halves, what gets painted, what can still be lit —
    /// comes through here.
    bool showsOutgoing() const { return m_kind == Kind::HidReports; }

    void tick();
    void closeRateWindow();

    /// Recompute both arrows' lit and saturated flags. True when either changed,
    /// which is the only time a repaint is worth asking for.
    bool refreshState();

    /// Run the tick timer at the pace the current state needs: fast enough to
    /// sample a blink, slow otherwise.
    void repaceTicks();

    /// Where an arrow is drawn. Shared with paintEvent() so what is painted and
    /// what is hovered cannot drift apart.
    QRectF arrowRect(bool incoming) const;

    /// The half of the widget belonging to one arrow. Wider than the triangle on
    /// purpose: the glyphs are a few pixels across, and requiring a hit on one
    /// would make its tooltip nearly unreachable.
    QRect hoverRect(bool incoming) const;

    QString tooltipFor(bool incoming) const;

    Arrow m_in;
    Arrow m_out;

    /// Runs only while an arrow is lit. A single-shot timer restarted per packet
    /// would mean thousands of timer operations a second on a busy device; this
    /// way a packet costs one clock read and repaints happen only on a change.
    QTimer *m_decay = nullptr;

    /// Closes each rate window. Runs whenever the indicator is available, idle
    /// included — that is what lets a rate fall back to zero after traffic stops.
    QTimer *m_rate = nullptr;
    QElapsedTimer m_rateWindow;

    /// Which arrow a tooltip is currently showing for, so each closed rate window
    /// can refresh it in place. A hovered tooltip is then a live readout rather
    /// than a number frozen at the moment the pointer stopped.
    std::optional<bool> m_hovered;
    QPoint m_hoverGlobalPos;

    const Kind m_kind;

    QString m_unavailableReason;
    bool m_available = false;
};

} // namespace hidcenter
