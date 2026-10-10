// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTransform>

class QIODevice;

namespace hidcenter::svg {

/// One element of an SVG document, reduced to its geometry and to what names it.
///
/// This is for code that keeps a drawing in an SVG file so it can be edited in
/// an ordinary editor, but draws it itself: it needs to know where things are
/// and which is which, not how the file would render. So styling is not
/// resolved. CSS is ignored, and of the presentation properties only an
/// element's own `fill` is decoded, as a colour; anything else is left in
/// `attributes` for the caller to read with property().
struct SvgElement
{
    /// The local name: `svg`, `g`, `path`, `rect`, `circle`, `ellipse`, `line`,
    /// `polyline`, `polygon`, `text`, and so on. Never `use`: a `<use>` is
    /// replaced by a copy of what it references (see readSvg()).
    QString tag;

    QString id;
    QStringList classes;

    /// The text of the element's own `<title>` child, which is what editors
    /// such as Inkscape show and edit as its title.
    QString title;

    /// Every attribute as written, by qualified name (`clip-path`,
    /// `data-wheel-id`, `inkscape:label`).
    QHash<QString, QString> attributes;

    /// The fill set on this element itself, from its `style` or its `fill`
    /// attribute. Invalid when it sets none, sets `none`, or sets something
    /// other than a plain colour. Not inherited.
    QColor fill;

    /// From this element's own coordinates to the document's, including the
    /// element's own `transform`.
    QTransform transform;

    /// The element's own outline, in its own coordinates. Empty for anything
    /// that is not a shape.
    QPainterPath path;

    /// For a `<rect>`, the rectangle as given, before `transform`.
    QRectF rect;

    /// For a `<text>`, all of its character data, including that of its
    /// `<tspan>`s, with whitespace simplified.
    QString text;

    /// For a `<text>`, its `x` and `y`, or its first `<tspan>`'s, in its own
    /// coordinates.
    QPointF textPosition;

    QList<SvgElement> children;

    bool hasClass(QStringView name) const;

    /// A presentation property as the element sets it: from its `style` if
    /// there, else from the attribute of that name. Empty if it sets neither.
    QString property(const QString &name) const;

    /// path, in document coordinates.
    QPainterPath outline() const;
};

struct SvgDocument
{
    /// The `<svg>` element. Empty if the file could not be read.
    SvgElement root;

    /// Why the file could not be read at all, with the line and column.
    /// Empty on success.
    QString error;

    /// Anything read but not understood, and so left out: unparseable path
    /// data, a `<use>` of an id that does not exist. Each names its line.
    QStringList warnings;
};

/// Read an SVG document's geometry.
///
/// **Coordinates are user units.** The root's `width`, `height` and `viewBox`
/// frame the page and are not applied, so a number in the file is the same
/// number in the tree.
///
/// **`<use>` is expanded.** It becomes a copy of the element it references,
/// placed as SVG places it: the use's `transform`, then its `x` and `y`, then
/// the referenced element's own `transform`, but not the transforms of that
/// element's ancestors. The copy's `id` and `title` are the use's own, since
/// they identify it, and none of the copied elements inside it keep an id. Its
/// `class`, `fill` and each `data-*` attribute are the use's where it sets them,
/// and the original's where it does not. References can point forwards and into
/// `<defs>`.
///
/// **Left out:** anything with `display: none` (such as a hidden layer of
/// reference images), what only exists to be referenced or to style (`defs`,
/// `clipPath`, `mask`, `symbol`, gradients, `style`), `metadata`, `image`,
/// and every element outside the SVG namespace (such as Inkscape's
/// `sodipodi:namedview`).
SvgDocument readSvg(QIODevice &device);

} // namespace hidcenter::svg
