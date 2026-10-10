// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelColourSwatches.h"

#include "WheelButtonColours.h"

#include <QBoxLayout>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QIcon>
#include <QPainter>
#include <QToolButton>

namespace hidcenter::simagic_alpha_evo {
namespace {

constexpr int kSwatchPx = 28;

} // namespace

QPixmap wheelColourCap(const QColor &colour, int px, qreal pixelRatio, bool flashing,
                       bool strikeOff)
{
    QPixmap pixmap(QSize(px, px) * pixelRatio);
    pixmap.setDevicePixelRatio(pixelRatio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool off = !colour.isValid();
    const qreal inset = flashing ? px * 0.22 : 2.0;
    const QRectF cap = QRectF(0.0, 0.0, px, px).adjusted(inset, inset, -inset, -inset);

    // The same edge and cap shades as WheelSchematic's, so off looks here the
    // way it does on the wheel.
    painter.setPen(QPen(off ? QColor(0x7a, 0x7b, 0x82) : colour.darker(160), 1.0));
    painter.setBrush(off ? QColor(0x2f, 0x30, 0x35) : colour);
    painter.drawEllipse(cap);

    if (off && strikeOff) {
        QPen slash(QColor(0xc8, 0xc9, 0xce), 2.0);
        slash.setCapStyle(Qt::RoundCap);
        painter.setPen(slash);
        const qreal across = cap.width() * 0.25;
        painter.drawLine(cap.bottomLeft() + QPointF(across, -across),
                         cap.topRight() + QPointF(-across, across));
    }

    if (flashing) {
        QPen halo(off ? QColor(0x7a, 0x7b, 0x82) : colour, qMax(1.5, px * 0.08));
        halo.setCapStyle(Qt::FlatCap);
        halo.setDashPattern({1.2, 1.0});
        painter.setPen(halo);
        painter.setBrush(Qt::NoBrush);
        const qreal ring = px * 0.08;
        painter.drawEllipse(QRectF(0.0, 0.0, px, px).adjusted(ring, ring, -ring, -ring));
    }

    return pixmap;
}

QButtonGroup *addWheelColourSwatches(QWidget *parent, QBoxLayout *row)
{
    auto *group = new QButtonGroup(parent);

    const auto addSwatch = [&](int id, const QString &name, const QColor &shade) {
        auto *swatch = new QToolButton(parent);
        swatch->setCheckable(true);
        swatch->setAutoRaise(true);
        swatch->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        swatch->setIconSize(QSize(kSwatchPx, kSwatchPx));
        swatch->setIcon(QIcon(wheelColourCap(shade, kSwatchPx, parent->devicePixelRatioF())));
        swatch->setText(name);
        group->addButton(swatch, id);
        row->addWidget(swatch);
    };

    for (const ButtonColour &colour : kButtonColours)
        addSwatch(colour.wheelColour, buttonColourName(colour), QColor(colour.rgb));
    addSwatch(kWheelLedColourOff, QCoreApplication::translate("WheelColourSwatches", "Off"),
              QColor());

    return group;
}

} // namespace hidcenter::simagic_alpha_evo
