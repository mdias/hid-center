// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QWidget>

namespace hidcenter::simagic_alpha_evo {

/// The two clutch paddles as a pair of bars, one above the other, with the
/// synced paddles' ratio as a line across both and a handle between them to
/// drag it by.
///
/// The bars are drawn as the style draws a progress bar, so they look like the
/// rest of the application wherever it runs. The line and the handle are in
/// the palette's highlight, and dimmed while the ratio is not editable, since a
/// ratio only does anything to synced paddles.
///
/// Only a display and an editor: what the bars show and what the ratio is set
/// to are the page's to say. The ratio can also be set from the keyboard, by
/// a step with the arrow keys and by ten with Page Up and Page Down.
class ClutchPaddlesView : public QWidget
{
    Q_OBJECT

public:
    explicit ClutchPaddlesView(QWidget *parent = nullptr);

    /// How far each bar is filled, 0 to 1, and what it is called.
    void setValues(qreal top, qreal bottom);
    void setLabels(const QString &top, const QString &bottom);

    /// The ratio, 0 to 100.
    int ratio() const { return m_ratio; }
    void setRatio(int percent);

    /// Whether the ratio can be dragged. It is drawn either way.
    void setRatioEditable(bool editable);
    bool isRatioEditable() const { return m_ratioEditable; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The user moved the ratio, not setRatio().
    void ratioChanged(int percent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    /// Where the bars run, left to right: between the labels and the values.
    int trackLeft() const;
    int trackRight() const;
    int barHeight() const;
    int handleRowHeight() const;

    QRect topBar() const;
    QRect bottomBar() const;

    /// The ratio's x, and the handle drawn on it.
    int ratioX() const;
    QRect handleRect() const;

    /// The ratio at a widget x, held to 0–100.
    int ratioAt(int x) const;

    /// Set the ratio as the user did, and say so.
    void editRatio(int percent);

    qreal m_top = 0.0;
    qreal m_bottom = 0.0;
    QString m_topLabel;
    QString m_bottomLabel;

    int m_ratio = 0;
    bool m_ratioEditable = true;
    bool m_dragging = false;
    bool m_hovering = false;
    int m_grabOffset = 0;  ///< From where the handle was grabbed to the ratio line, in pixels.
};

} // namespace hidcenter::simagic_alpha_evo
