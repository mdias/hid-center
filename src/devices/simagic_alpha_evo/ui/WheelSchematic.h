// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QTransform>
#include <QWidget>

namespace hidcenter::simagic_alpha_evo {

struct WheelControl;
struct WheelLayout;

/// A steering wheel seen from the driver's seat, drawn from a WheelLayout, with
/// every control on it pointable.
///
/// **Drawn to be recognised, not admired.** The outline, the proportions, the
/// place of every control and the colours a wheel comes with are all taken
/// from the real thing. What each control is made of is reduced to a flat
/// shade, because the point is that a user looking from this to the wheel in
/// their hands finds the same button in the same place.
///
/// **Always dark.** A wheel is a black object, and one drawn in the palette's
/// colours would turn pale in a light theme and stop looking like itself.
///
/// **One transform.** The layout is in millimetres, and paintEvent() and
/// controlAt() both go through toWidget(), the same way LedRing's drawing and
/// hit-testing both come from ringRect(). A control is therefore clickable
/// exactly where it is drawn, at any size.
///
/// Pointing at a control shows its name after a moment. Pointing at one the
/// page has said can be edited (see setEditableControls()) also shows the hand
/// cursor, and clicking it emits controlClicked(). What that opens is up to the
/// page.
///
/// **Colours are handed in.** The widget keeps no settings and knows nothing
/// about the base, so what the user has set a light to comes from
/// setControlColours(), over the colours the layout comes with. How bright the
/// lights are comes from setLedBrightness() the same way, and which controls
/// have a problem to mark from setControlWarnings().
class WheelSchematic : public QWidget
{
    Q_OBJECT

public:
    explicit WheelSchematic(QWidget *parent = nullptr);

    /// The wheel to draw, or null for none. Not owned: the layouts are read
    /// once and kept for the life of the module, outliving every widget.
    void setWheel(const WheelLayout *wheel);
    const WheelLayout *wheel() const { return m_wheel; }

    /// The colours the user has set, by control key or, for an RPM LED, by the
    /// LED's key, to draw in place of the ones in the layout. An entry holding
    /// an invalid colour is a light turned off. Lights without an entry keep
    /// the layout's colour.
    void setControlColours(const QHash<QString, QColor> &colours);

    /// The colour a control is drawn lit in: its entry from
    /// setControlColours(), or else the layout's. Invalid for unlit.
    QColor controlColour(const WheelControl &control) const;

    /// Which controls open something when clicked, by key. None until set, and
    /// forgotten with the wheel: setWheel() clears it.
    void setEditableControls(const QSet<QString> &keys);

    /// How bright every LED on the wheel is drawn, in percent: 0 draws them all
    /// unlit, 100 in the colours controlColour() and the layout give them.
    /// Clamped to that range.
    void setLedBrightness(int percent);
    int ledBrightness() const { return m_ledBrightness; }

    /// Controls to mark as having something wrong with their settings, by key,
    /// each with a sentence saying what. The mark is the style's warning icon
    /// beside the control, and pointing at the control shows the sentence
    /// under its name.
    void setControlWarnings(const QHash<QString, QString> &warnings);

    /// Which of the wheel's controls a point in widget coordinates is on, as an
    /// index into WheelLayout::controls, or -1 for none.
    ///
    /// A paddle counts only where it shows: through an opening, past the edge
    /// of the wheel, or through a see-through body. Anywhere a solid body or a
    /// plate is drawn over it, the point is on the wheel, not on the paddle
    /// behind it.
    int controlAt(const QPointF &point) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// A control with settings was clicked: pressed and released over it with
    /// the left button. `index` is into WheelLayout::controls.
    void controlClicked(int index);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /// Millimetres on the wheel to pixels in this widget: the wheel as large as
    /// fits, centred, with its aspect kept.
    QTransform toWidget() const;

    /// Whether anything drawn in front of the paddles covers this point.
    bool isCoveredAt(const QPointF &mm) const;

    /// Follow the pointer: the cursor, and which control is under it.
    void updateHover(const QPointF &point);

    /// Whether a control under the pointer can be clicked.
    bool isClickable(int index) const;

    const WheelLayout *m_wheel = nullptr;
    QRectF m_bounds;

    /// The colour a light is drawn in, by its key, as controlColour() is for a
    /// control.
    QColor lightColour(const QString &key, const QColor &stock) const;

    QHash<QString, QColor> m_colours;
    int m_ledBrightness = 100;
    QHash<QString, QString> m_warnings;
    QSet<QString> m_editable;

    /// The clickable control the left button went down on, or -1. A click is a
    /// release over the same one.
    int m_pressed = -1;

    /// The control under the pointer, or -1. It decides the cursor and is kept
    /// only so the cursor is not set again on every pixel of movement.
    int m_hovered = -1;
};

} // namespace hidcenter::simagic_alpha_evo
