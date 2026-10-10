// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QWidget>

namespace hidcenter::simagic_alpha_evo {

/// Joystick buttons as a grid of numbered circles, lit while pressed.
///
/// As many columns as the width holds, filled a row at a time from button 1,
/// so the height follows the width (heightForWidth()). In the palette's
/// colours rather than the wheel drawing's fixed dark ones: this is a readout
/// beside the settings, not a picture of the wheel. A pressed button is filled
/// with the highlight colour.
class PressedButtonsView : public QWidget
{
    Q_OBJECT

public:
    explicit PressedButtonsView(QWidget *parent = nullptr);

    /// How many buttons to show, numbered from 1.
    void setButtonCount(int count);
    int buttonCount() const { return m_count; }

    /// The buttons pressed, by number. One past buttonCount() is not shown.
    void setPressed(const QList<int> &pressed);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    /// The side of one button's square, in pixels, from the font: room for a
    /// three-digit number inside the circle.
    int cellPx() const;
    int columnsFor(int width) const;

    int m_count = 0;
    QList<int> m_pressed;
};

} // namespace hidcenter::simagic_alpha_evo
