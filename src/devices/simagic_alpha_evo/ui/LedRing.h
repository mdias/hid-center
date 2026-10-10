// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QGradient>
#include <QList>
#include <QWidget>

class QLabel;
class QSpinBox;

namespace hidcenter::simagic_alpha_evo {

/// The most colour stops the ring will hold. The wheel's strip fades between at
/// most this many, so it is the ceiling on the array as well as on the spin box
/// that sizes it.
inline constexpr int kMaxRingColours = 16;

/// Where colours[0] sits, in degrees clockwise from the top of the ring. Zero
/// puts it at twelve o'clock; change this one number to rotate the whole thing
/// round to wherever the strip's first LED physically is.
inline constexpr qreal kRingStartAngleDeg = 90.0;

/// How far a lit LED bleeds into its neighbours, as the standard deviation of a
/// Gaussian in degrees round the ring.
///
/// The wheel's strip sits behind a diffuser, so a stop set to black does not read
/// black: light arrives from both sides and fills it in. Twenty degrees is about
/// 2.8 LEDs of spread on a fifty-LED strip, where one LED is 7.2 degrees.
///
/// This exists to be tuned against the hardware. What it costs, as the 8-bit
/// sRGB level a black stop reads at when every other stop is white:
///
///     sigma | n=2  n=3  n=4  n=6  n=10  n=16
///        15 |  73   89  102  123   155   186
///        20 |  84  102  117  141   175   202
///        30 | 102  123  141  168   199   220
///        40 | 117  141  160  186   213   229
///
/// Two stops is the diametrically-opposite case, and it stays the darkest the
/// ring can go at every width. That is the behaviour being modelled: darkness is
/// only reachable well away from anything lit.
inline constexpr qreal kDiffuserSigmaDeg1 = 15.0;
inline constexpr qreal kDiffuserSigmaDeg2 = 60.0;
inline constexpr qreal kDiffuserSigmaDeg2Weight = 0.15;

/// gamma correction
inline constexpr qreal kGamma = 2.2;
inline constexpr qreal kGammaDiffuserWeights = 1.0;

/// How finely the ring is sampled before blurring, which sets the smoothness and
/// the cost together.
///
/// At 180 a sample is two degrees, comfortably finer than the bleed it resolves,
/// and the pass is 180 × 180 multiply-adds — well under a millisecond on a widget
/// that repaints on a resize or on crossing a zone. The cost grows with the
/// square, so doubling this is four times the work; far below the bleed width the
/// blur starts to show as facets rather than a fade. It is also the number of
/// gradient stops handed to Qt, which builds a 1024-entry table from them, so
/// there is nothing to gain past that.
inline constexpr int kDiffuserSamples = 64;

/// The stops for the conical gradient that fades a colour array round a ring,
/// in Qt's own terms: position 0 at the ring's start angle, rising
/// counter-clockwise, which is the direction QConicalGradient counts in.
///
/// Free and pure so the mapping can be checked on its own — it is the one piece
/// of this widget that is easy to get subtly wrong and impossible to see wrong.
/// The list always closes: its last stop repeats the first colour at 1.0, so the
/// final colour fades back into the first instead of snapping.
QGradientStops ringGradientStops(const QList<QColor> &colours);

/// Blur a set of conical-gradient stops the way the wheel's diffuser blurs the
/// strip: a wrapped Gaussian, in linear light because that is how light adds.
///
/// Deliberately knows nothing about the ring. A rotationally symmetric blur is
/// orientation-blind — it cannot tell which way the positions run — so the
/// clockwise-to-counter-clockwise mapping stays where it belongs, in
/// ringGradientStops(), and this is free to be a plain signal operation over
/// positions that happen to wrap at 1.0.
///
/// The kernel is normalised, so a set of stops that are all one colour comes back
/// unchanged. That is the property to check first if this ever looks wrong: the
/// blur must spread a colour, never shift it.
QGradientStops diffuseGradientStops(const QGradientStops &stops, qreal sigmaDeg1, qreal sigmaDeg2);

/// A ring of LEDs fading between a handful of colours, each of which can be set
/// by clicking the part of the ring it governs, with the control for how many of
/// them there are sitting in the hole in the middle.
///
/// **The colour array is the whole model.** Its length is the number of
/// transitions and each entry is the colour at one of them, which is why
/// colours()/setColours() is the entire interface: nothing else has to be kept
/// in step, and whoever eventually decodes the wheel's LED protocol can read the
/// array straight out and send it.
///
/// **It is self-contained on purpose.** The count control, the colours currently
/// hidden behind a lower count, and the tooltip that explains the thing are all
/// inside, so reusing it is `new LedRing` and a connection to coloursChanged().
///
/// **The band is a preview, the dots are the picks.** The strip on the wheel sits
/// behind a diffuser, so the band is blurred to match it and a chosen colour is
/// not what appears at its own anchor. The exact colour is drawn as a small
/// swatch there instead, because a preview that hides what was picked is no use
/// for picking.
///
/// Colour *i* is anchored at kRingStartAngleDeg + i × 360/N clockwise from the
/// top, and the arc it can be clicked on is centred on that anchor, ±180/N wide.
/// So one colour is the whole ring, two are the top and bottom halves, three are
/// −60..60, 60..180 and 180..300 — the zone you click is always the one whose
/// colour is pure under the cursor, rather than a wedge whose colour is somewhere
/// off to the side.
///
/// The widget knows nothing about wheelbases and holds no device state; it is an
/// input, and the page above it decides what the colours mean.
class LedRing : public QWidget
{
    Q_OBJECT

    /// The state as a property as well as a pair of accessors, which is what a
    /// reusable Qt widget is expected to offer: it is what lets anything holding
    /// only a QWidget* — Designer, a stylesheet, a test — get at the colours
    /// without linking against this class.
    Q_PROPERTY(QList<QColor> colours READ colours WRITE setColours NOTIFY coloursChanged)
    Q_PROPERTY(int colourCount READ colourCount WRITE setColourCount NOTIFY coloursChanged)
    Q_PROPERTY(bool countVisible READ isCountVisible WRITE setCountVisible)

public:
    explicit LedRing(QWidget *parent = nullptr);

    /// Only the colours currently in play. The ones beyond the count are still
    /// held, so lowering it and raising it again gives them back, but they are
    /// not part of what the ring is showing and so not part of this.
    QList<QColor> colours() const { return m_all.mid(0, m_count); }

    /// The array carries the transition count with it: one entry per stop.
    ///
    /// Clamped to kMaxRingColours and never left empty — a ring with no colours
    /// has nothing to draw and no zone to click, so an empty list is taken as
    /// one black stop rather than as a widget that has stopped working. Emits
    /// coloursChanged() only on an actual change, so feeding this back its own
    /// signal cannot loop.
    void setColours(const QList<QColor> &colours);

    int colourCount() const { return m_count; }

    /// How many colours the ring fades between, clamped to 1..kMaxRingColours.
    ///
    /// Lowering it hides the colours past the new end rather than dropping them,
    /// which is what lets a user try a smaller ring and go back without losing
    /// what they had picked.
    void setColourCount(int count);

    /// Whether the count control in the middle is shown. Hiding it leaves the
    /// ring driven purely by setColours(), for a caller with a fixed number of
    /// colours or a control of its own for them.
    bool isCountVisible() const;
    void setCountVisible(bool visible);

    /// Paint the band as these colours instead of the chosen ones, leaving the
    /// chosen ones alone.
    ///
    /// This is what an animation is, on screen: the band shows the frame and the
    /// swatches keep showing the picks, so a ring that is breathing still says
    /// exactly what colour each stop is set to. It falls straight out of what
    /// this widget already claims — the band is a preview, the dots are the
    /// picks — and it is the reason the two were drawn from different sources in
    /// the first place.
    ///
    /// Emits no coloursChanged(), which matters: that signal is what a page uses
    /// to decide something has been edited, and an animation has edited nothing.
    /// A list that is empty, or that is not as long as colours(), goes back to
    /// painting colours() — the zones and the swatches are laid out from the
    /// real count, and a band of a different length would not line up with them.
    void setPreviewColours(const QList<QColor> &colours);

    /// Which stop a point addresses, or -1 for anywhere off the band — the hole
    /// in the middle and the corners around the outside included.
    int zoneAt(const QPointF &point) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void coloursChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    /// The largest centred square the widget can hold, less a margin. Everything
    /// drawn and everything hit-tested is derived from this one rectangle, so
    /// the arcs the mouse is measured against are by construction the ones on
    /// screen.
    QRectF ringRect() const;

    /// The band's inner and outer radii, from the same rectangle.
    qreal outerRadius() const;
    qreal innerRadius() const;

    /// Move the spin box to match m_count without it answering back.
    void syncCountBox();

    /// Always kMaxRingColours long. Only the first m_count are shown; the rest
    /// are what a lowered count gives back when it is raised again.
    QList<QColor> m_all;

    int m_count = 1;

    /// What the band paints, when something is animating it. Empty means the
    /// band paints the picks, which is every case but that one.
    QList<QColor> m_preview;

    QLabel *m_countLabel = nullptr;
    QSpinBox *m_countBox = nullptr;

    /// The zone under the cursor, or -1. Drives the highlight and the cursor; a
    /// ring that is not being pointed at looks the same whether or not the
    /// window has focus.
    int m_hovered = -1;
};

} // namespace hidcenter::simagic_alpha_evo
