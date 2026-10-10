// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

namespace hidcenter::simagic_alpha_evo {

/// The steering wheels this module knows the face of.
enum class WheelModel
{
    Gtc,
    GtNeo,
};

/// What a control on a wheel is. It decides how the control is drawn, and will
/// decide which settings clicking it opens.
///
/// The mapping is inferred from the preset messages rather than seen on the
/// wire.
enum class WheelControlKind
{
    Button,          ///< a cap, lit or not
    LitLabel,        ///< a lit window with a word on it, like a GTC's ABS
    RotaryEncoder,   ///< a knob on the face
    ThumbEncoder,    ///< a knurled roller turned with the thumb
    SevenWaySwitch,  ///< four directions, a push, and a turn each way
    ShiftPaddle,     ///< on/off
    ClutchPaddle,    ///< an axis
    RpmLights,       ///< the row of rev LEDs, as one control
};

/// A symbol printed on a cap rather than a word. Only the ones simple enough to
/// draw at the size of a button are here; any other icon cap is drawn plain.
enum class CapGlyph
{
    None,
    ArrowLeft,
    ArrowRight,
    Power,
};

/// What a stretch of the wheel is made of, which is all the drawing needs to
/// know to colour it.
enum class WheelMaterial
{
    Grip,     ///< rubber or leather
    Carbon,
    Plate,    ///< the dark brushed alloy behind a GTC's controls
    Housing,  ///< black plastic mouldings
    Band,     ///< a raised strip, a shade lighter than the carbon around it
    Seam,     ///< stroked, not filled: an edge between two panels
    Stitch,   ///< stroked, not filled: the thread along a leather edge
};

/// One region of a wheel's face, drawn over the body in list order.
struct WheelSurface
{
    QPainterPath path;
    WheelMaterial material = WheelMaterial::Carbon;

    /// Cut to the body's outline when drawn. It lets a grip be given as a rough
    /// region, so the edge the user sees is the body's and exists once, rather
    /// than traced a second time and never quite lining up.
    bool clipToBody = false;

    /// Part of the body, like a grip or its stitching, rather than something
    /// mounted on it. It goes see-through along with the body.
    bool onBody = false;
};

/// One LED in a row of rev lights. Drawn with fully rounded ends, so a square
/// one is round.
struct WheelLed
{
    /// Stable identifier, unique within a wheel, like a control's key: what
    /// this LED's colour will be set and saved by.
    QString key;

    QRectF rect;
    QColor colour;
};

/// Text printed on the wheel itself, as against on a cap: a logo, drawn light
/// straight onto the surface. A word on a lit window, like a GTC's ABS, is a
/// control of its own instead, since it has a colour to set.
struct WheelLabel
{
    QPointF centre;
    QSizeF size;
    qreal rotationDeg = 0.0;
    QString text;
};

/// One thing on a wheel that the user can press, turn or pull.
///
/// Every length is in **millimetres on the real wheel**, origin at the wheel's
/// centre, +x to the driver's right and +y down, so it can be checked against a
/// photograph with a ruler.
struct WheelControl
{
    /// Stable identifier, unique within a wheel: the element's id in the
    /// wheel's SVG. **Never change one**: it is what settings for this control,
    /// such as the colour of the LEDs under a button, will be saved under.
    QString key;

    WheelControlKind kind = WheelControlKind::Button;

    /// The element's title in the SVG, in English. Use wheelControlName()
    /// rather than this directly.
    QString name;

    /// The middle of a round control, a roller or a lit label. For a paddle,
    /// where its legend goes: somewhere in the part that shows, which the
    /// middle may not be.
    QPointF centre;

    /// A round control's diameter is the width. A roller's width is its long
    /// side, which its ribs cross, before rotation. A lit label's is its
    /// window, as the rectangle measures.
    QSizeF size;

    /// Clockwise, as QPainter rotates.
    qreal rotationDeg = 0.0;

    /// The colour the cap or ring is lit out of the box, or invalid for a
    /// control that is not lit. Only the default: what the user sets will be
    /// looked up by key.
    QColor colour;

    /// Short text printed on the control, as it comes: "PIT" on a cap, "−" on a
    /// paddle, "ABS" on a lit label.
    QString legend;
    CapGlyph glyph = CapGlyph::None;

    /// Fitted to some wheels of this model and not others, like the GTC's
    /// clutch paddles. Drawn dashed, and still clickable.
    bool optional = false;

    /// Which of the wheel's LED slots lights this control, 0 to 29, or -1 when
    /// none is known. It is the index into the per-LED bytes of the wheel's
    /// configuration (AlphaEvoWheelConfig.h), and where the control's colour is
    /// sent. From `data-led` in the SVG.
    int ledSlot = -1;

    /// Which of the wheel's inputs each of the control's actions is, 0 to 39, or
    /// -1 where none is known: one entry per wheelControlActions() of its kind,
    /// in that order, so a button has one and a 7-way switch seven. Each is the
    /// index into the button mappings of the wheel's configuration
    /// (AlphaEvoWheelConfig.h), and so what says which joystick button that
    /// action presses. From `data-input` in the SVG.
    QList<int> inputs;

    /// The control's outline in millimetres. It is what is drawn for a paddle
    /// or the recess of a row of rev lights, and what is hit-tested for every
    /// kind.
    QPainterPath shape;

    /// For rev lights, each LED in the row, in the colour it shows in
    /// Simagic's photographs. Empty for every other kind.
    QList<WheelLed> lights;
};

/// One wheel, as seen from the driver's seat, as read from its SVG file under
/// wheels/. What each part of the file becomes is in README.md, "Layouts are SVG
/// files".
struct WheelLayout
{
    WheelModel model = WheelModel::Gtc;

    /// The model's wheel id,
    /// which is how the base's status report names the wheel on it: see
    /// wheelLayoutForId(). Confirmed for the GTC, which a base reported as 4.
    int wheelId = 0;

    /// The model's name. Not translated, since it is a product name.
    QString title;

    WheelMaterial bodyMaterial = WheelMaterial::Grip;

    /// The whole wheel's outline, with the openings the paddles show through cut
    /// out of it. Filled odd-even.
    QPainterPath body;

    /// Draw the body faint, and let the paddles behind it be seen and pointed
    /// at through it.
    ///
    /// For a wheel whose rim is in front of most of each paddle, like the GTC's.
    /// Drawn solid, it would hide the very controls the tab is for. The plates
    /// and pods mounted on the body stay solid, so what they cover is still
    /// covered.
    bool seeThroughBody = false;

    QList<WheelSurface> surfaces;
    QList<WheelLabel> labels;

    /// Paddles as well as everything on the face. They are drawn in list order,
    /// behind the body for paddles and over it for the rest.
    QList<WheelControl> controls;

    /// Everything drawn, in millimetres.
    QRectF bounds() const;
};

/// Every wheel, in the order the Wheel tab lists them.
///
/// Read from the SVG files built into this module the first time it is called,
/// and never again. A file that cannot be read is logged and gives a wheel with
/// nothing to draw, so there is still one layout for every model.
const QList<WheelLayout> &wheelLayouts();

/// The layout for one model. Never null: every enumerator has one.
const WheelLayout &wheelLayout(WheelModel model);

/// The layout for the wheel a base reports by its wheel id byte, or null when
/// this module has none for that id (including 0, no wheel).
const WheelLayout *wheelLayoutForId(int wheelId);

/// Whether the Wheel tab offers a wheel to the user. A wheel still in
/// development is offered in developer mode only; outside it, the tab treats that
/// wheel like one it has no layout for.
bool wheelLayoutReleased(const WheelLayout &layout);

/// A control's name, in the user's language.
QString wheelControlName(const WheelControl &control);

/// What a kind of control can be made to do that presses a joystick button of
/// its own, by the name `data-input` and the saved settings use: "clockwise"
/// and "anticlockwise" for a knob, "up" and "down" for a thumb roller, and for a
/// 7-way switch its four directions, "push" and both turns. A button has one
/// action, with an empty name, since pressing it is all it does. Paddles and
/// RPM lights have none.
QStringList wheelControlActions(WheelControlKind kind);

/// One of wheelControlActions(), by its index there, as the user reads it:
/// "Up", "Clockwise". Empty for a button's, and for an index the kind has not.
QString wheelControlActionName(WheelControlKind kind, int action);

bool isPaddle(WheelControlKind kind);

} // namespace hidcenter::simagic_alpha_evo
