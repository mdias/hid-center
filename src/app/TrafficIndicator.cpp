// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/TrafficIndicator.h"

#include <QFontMetrics>
#include <QHelpEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>

#include <cmath>

namespace hidcenter {
namespace {

/// How long an arrow stays lit after a packet. Long enough to be seen at a
/// glance, short enough that a burst still reads as a flicker rather than a
/// solid light.
constexpr int kGlowMs = 150;

/// How often the lit state is re-evaluated while something is lit and not
/// blinking. Finer than this buys nothing the eye can use.
constexpr int kDecayTickMs = 60;

/// After this long continuously lit, packets are arriving faster than one per
/// kGlowMs and the glow will never expire on its own. The arrow would sit
/// solidly on, saying only "at least one packet recently" — which is the least
/// interesting thing it could say — so it starts blinking instead.
constexpr int kSaturationMs = 450;

/// Blinks a second while saturated. Fast enough to read as a flood rather than
/// as packets you could count.
constexpr int kSaturatedBlinkHz = 15;
constexpr int kBlinkHalfPeriodMs = 1000 / (2 * kSaturatedBlinkHz);

/// Vivid enough to read as "now" against both palettes, and conventional: green
/// for what arrives, amber for what leaves.
const QColor kActiveIn(0x37, 0xd2, 0x4d);
const QColor kActiveOut(0xf0, 0x8c, 0x00);

/// Opacity of an idle arrow. Enough to be plainly there, little enough that a
/// lit one is unmistakable beside it.
constexpr qreal kIdleOpacity = 0.2;

/// One second, so the number in the tooltip is literally reports in the last
/// second and needs no explaining.
constexpr int kRateWindowMs = 1000;

/// A triangle pointing down (in) or up (out), inscribed in `box`.
QPainterPath arrowPath(const QRectF &box, bool pointingDown)
{
    QPainterPath path;
    if (pointingDown) {
        path.moveTo(box.left(), box.top());
        path.lineTo(box.right(), box.top());
        path.lineTo(box.center().x(), box.bottom());
    } else {
        path.moveTo(box.left(), box.bottom());
        path.lineTo(box.right(), box.bottom());
        path.lineTo(box.center().x(), box.top());
    }
    path.closeSubpath();
    return path;
}

QColor dimmed(QColor colour)
{
    colour.setAlphaF(kIdleOpacity);
    return colour;
}

} // namespace

TrafficIndicator::TrafficIndicator(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
{
    // Fixed both ways: the arrows are sized from the font, and a widget stretched
    // taller than its hint would scale them past the width reserved for them.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_unavailableReason = showsOutgoing() ? tr("No traffic source for this device.")
                                          : tr("Nothing is listening for telemetry.");

    m_decay = new QTimer(this);
    m_decay->setInterval(kDecayTickMs);
    connect(m_decay, &QTimer::timeout, this, &TrafficIndicator::tick);

    m_rate = new QTimer(this);
    m_rate->setInterval(kRateWindowMs);
    connect(m_rate, &QTimer::timeout, this, &TrafficIndicator::closeRateWindow);
}

QSize TrafficIndicator::sizeHint() const
{
    // Sized from the font so the arrows sit with the device name beside them
    // whatever the user's text size.
    const int height = QFontMetrics(font()).ascent();
    const int width = showsOutgoing() ? height * 2 + height / 2 : height;
    return {width, height};
}

QRectF TrafficIndicator::arrowRect(bool incoming) const
{
    const qreal size = height();
    const qreal gap = size / 2;
    return incoming ? QRectF(0, 0, size, size) : QRectF(size + gap, 0, size, size);
}

QRect TrafficIndicator::hoverRect(bool incoming) const
{
    // With one arrow the whole widget is its hover area. Halving it would leave
    // a dead strip beside the only thing here that has anything to say.
    if (!showsOutgoing())
        return incoming ? rect() : QRect();

    const int half = width() / 2;
    return incoming ? QRect(0, 0, half, height())
                    : QRect(half, 0, width() - half, height());
}

void TrafficIndicator::noteTraffic(TrafficDirection direction)
{
    // A feature read is a request we send, but what it produces is a reply, and
    // the reply is the interesting half — so it counts as incoming.
    const bool incoming = !showsOutgoing() || direction == TrafficDirection::In
        || direction == TrafficDirection::FeatureGet;

    // Everything lands on the incoming arrow when it is the only one there is:
    // a mis-signalled direction should not vanish into an arrow nothing paints.
    Arrow &arrow = incoming ? m_in : m_out;

    arrow.since.restart();
    ++arrow.count;

    if (refreshState())
        update();

    repaceTicks();

    if (!m_decay->isActive())
        m_decay->start();
}

void TrafficIndicator::setAvailable(bool available, const QString &reason)
{
    m_unavailableReason = reason;

    if (m_available == available)
        return;

    m_available = available;
    if (m_available) {
        m_rateWindow.start();
        m_rate->start();
    } else {
        m_in = {};
        m_out = {};
        m_decay->stop();
        m_rate->stop();
    }
    update();
}

void TrafficIndicator::tick()
{
    if (refreshState())
        update();

    const bool outLit = showsOutgoing() && (m_out.lit || m_out.saturated);
    if (!m_in.lit && !m_in.saturated && !outLit) {
        m_decay->stop();
        return;
    }

    repaceTicks();
}

void TrafficIndicator::repaceTicks()
{
    const bool saturated = m_in.saturated || (showsOutgoing() && m_out.saturated);
    const int wanted = saturated ? kBlinkHalfPeriodMs : kDecayTickMs;

    // Only when it actually differs: setInterval() restarts a running timer, so
    // reassigning the same value every tick would keep pushing the next one away.
    if (m_decay->interval() != wanted)
        m_decay->setInterval(wanted);
}

bool TrafficIndicator::refreshState()
{
    bool changed = false;

    for (Arrow *arrow : {&m_in, &m_out}) {
        const bool glowing = arrow->since.isValid() && arrow->since.elapsed() < kGlowMs;

        bool lit = false;
        bool saturated = false;

        if (!glowing) {
            arrow->litSince.invalidate();
        } else {
            // Traffic has just resumed: this is where a run of continuous
            // activity starts being measured from.
            if (!arrow->litSince.isValid())
                arrow->litSince.start();

            const qint64 continuous = arrow->litSince.elapsed();
            saturated = continuous >= kSaturationMs;
            // Phased from litSince rather than a free-running clock, so the
            // blink starts *on* the moment saturation begins.
            lit = !saturated || (continuous / kBlinkHalfPeriodMs) % 2 == 0;
        }

        if (lit != arrow->lit || saturated != arrow->saturated) {
            arrow->lit = lit;
            arrow->saturated = saturated;
            changed = true;
        }
    }

    return changed;
}

void TrafficIndicator::closeRateWindow()
{
    // Scaled by the window's measured length rather than assuming the timer fired
    // on time, so a late tick does not inflate the figure.
    const qint64 elapsed = m_rateWindow.isValid() ? m_rateWindow.restart() : kRateWindowMs;
    if (elapsed <= 0)
        return;

    for (Arrow *arrow : {&m_in, &m_out}) {
        arrow->rate = int(std::llround(double(arrow->count) * 1000.0 / double(elapsed)));
        arrow->count = 0;
    }

    // Refresh a tooltip that is already up, so hovering an arrow gives a meter
    // rather than a number frozen when the pointer stopped moving. underMouse()
    // is the guard that keeps this from hijacking some other widget's tooltip —
    // QToolTip::isVisible() is true for any tooltip anywhere in the application.
    if (m_hovered && underMouse() && QToolTip::isVisible()) {
        QToolTip::showText(m_hoverGlobalPos, tooltipFor(*m_hovered), this,
                           hoverRect(*m_hovered));
    }
}

QString TrafficIndicator::tooltipFor(bool incoming) const
{
    if (!m_available)
        return m_unavailableReason;

    const Arrow &arrow = incoming ? m_in : m_out;

    if (!showsOutgoing())
        return tr("Incoming: %1 packets/s\nrFactor 2 telemetry datagrams.").arg(arrow.rate);

    // Naming what each arrow counts is worth the second line: nothing else says
    // that a feature *read* is incoming traffic.
    return incoming
        ? tr("Incoming: %1 reports/s\nInput reports and feature reads.").arg(arrow.rate)
        : tr("Outgoing: %1 reports/s\nOutput reports and feature writes.").arg(arrow.rate);
}

bool TrafficIndicator::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const bool incoming = hoverRect(true).contains(help->pos());

        // Passing the arrow's own half as the rect keeps the tooltip up while the
        // pointer wanders within one arrow, and re-asks when it crosses to the
        // other.
        m_hovered = incoming;
        m_hoverGlobalPos = help->globalPos();
        QToolTip::showText(m_hoverGlobalPos, tooltipFor(incoming), this, hoverRect(incoming));
        return true;
    }

    if (event->type() == QEvent::Leave)
        m_hovered.reset();

    return QWidget::event(event);
}

void TrafficIndicator::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor muted = palette().color(QPalette::Mid);

    const struct
    {
        bool incoming;
        const Arrow &arrow;
        QColor active;
    } arrows[] = {
        {true, m_in, kActiveIn},
        {false, m_out, kActiveOut},
    };

    for (const auto &entry : arrows) {
        if (!entry.incoming && !showsOutgoing())
            continue;

        // Inset by half the pen width, so an outlined arrow occupies exactly the
        // same space as a filled one and the pair does not shift as it lights.
        const QPainterPath path =
            arrowPath(arrowRect(entry.incoming).adjusted(1, 1, -1, -1), entry.incoming);

        if (!m_available) {
            painter.setPen(QPen(muted, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            continue;
        }

        // Idle and the blink's off phase paint the same dimmed colour, so a
        // saturated arrow pulses in place instead of flickering in and out of
        // existence.
        painter.setPen(Qt::NoPen);
        painter.setBrush(entry.arrow.lit ? entry.active : dimmed(entry.active));
        painter.drawPath(path);
    }
}

} // namespace hidcenter
