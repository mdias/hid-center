// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PressedButtonsView.h"

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>

#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// How many columns the size hint asks for: enough for forty buttons to be
/// five rows, which is what a GTC's forty mapping bytes can reach.
constexpr int kHintColumns = 8;

/// Space between neighbouring circles, in pixels.
constexpr int kGapPx = 4;

} // namespace

PressedButtonsView::PressedButtonsView(QWidget *parent)
    : QWidget(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void PressedButtonsView::setButtonCount(int count)
{
    count = qMax(0, count);
    if (count == m_count)
        return;

    m_count = count;
    updateGeometry();
    update();
}

void PressedButtonsView::setPressed(const QList<int> &pressed)
{
    if (pressed == m_pressed)
        return;

    m_pressed = pressed;
    update();
}

int PressedButtonsView::cellPx() const
{
    const QFontMetrics metrics(font());
    const int digits = metrics.horizontalAdvance(QStringLiteral("888"));
    return qMax(digits, metrics.height()) + metrics.height() / 2 + kGapPx;
}

int PressedButtonsView::columnsFor(int width) const
{
    return qMax(1, width / cellPx());
}

QSize PressedButtonsView::sizeHint() const
{
    const int cell = cellPx();
    const int columns = qMin(kHintColumns, qMax(1, m_count));
    return {columns * cell, heightForWidth(columns * cell)};
}

QSize PressedButtonsView::minimumSizeHint() const
{
    const int cell = cellPx();
    return {cell, cell};
}

int PressedButtonsView::heightForWidth(int width) const
{
    const int columns = columnsFor(width);
    const int rows = (m_count + columns - 1) / columns;
    return qMax(1, rows) * cellPx();
}

void PressedButtonsView::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange)
        updateGeometry();
    if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange)
        update();
    QWidget::changeEvent(event);
}

void PressedButtonsView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int cell = cellPx();
    const int columns = columnsFor(width());
    // Centred, so the grid sits under the middle of its box rather than hard
    // against one side.
    const qreal left = (width() - qMin(columns, qMax(1, m_count)) * cell) / 2.0;

    // A pressed button in the active highlight whether or not the window has
    // focus: whoever presses one has their hands on the wheel, not on the app.
    const QPalette &colours = palette();
    const QColor lit = colours.color(QPalette::Active, QPalette::Highlight);
    const QColor litText = colours.color(QPalette::Active, QPalette::HighlightedText);

    for (int index = 0; index < m_count; ++index) {
        const int button = index + 1;
        const bool pressed = m_pressed.contains(button);

        const QRectF square(left + (index % columns) * cell, (index / columns) * cell, cell, cell);
        const QRectF circle = square.adjusted(kGapPx / 2.0, kGapPx / 2.0, -kGapPx / 2.0,
                                              -kGapPx / 2.0);

        painter.setPen(QPen(pressed ? lit : colours.color(QPalette::Mid), 1.0));
        painter.setBrush(pressed ? lit : colours.color(QPalette::Base));
        painter.drawEllipse(circle);

        painter.setPen(pressed ? litText : colours.color(QPalette::Disabled, QPalette::Text));
        painter.drawText(circle, Qt::AlignCenter, QString::number(button));
    }
}

} // namespace hidcenter::simagic_alpha_evo
