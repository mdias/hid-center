// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SvgGeometry.h"

#include <QIODevice>
#include <QLineF>
#include <QSet>
#include <QXmlStreamReader>
#include <QtMath>

#include <cmath>
#include <optional>

namespace hidcenter::svg {
namespace {

const QString kSvgNamespace = QStringLiteral("http://www.w3.org/2000/svg");
const QString kXlinkNamespace = QStringLiteral("http://www.w3.org/1999/xlink");

/// Beyond this many `<use>`s inside one another, a reference is taken to be
/// going round in a circle the stack check has not caught.
constexpr int kMaxUseDepth = 32;

/// An element as it is in the file, before `<use>` is expanded.
struct RawElement
{
    QString tag;
    bool inSvgNamespace = true;
    QHash<QString, QString> attributes;
    QString href;

    /// All the character data inside it, its children's included, in document
    /// order: the text of a `<title>`, or of a `<text>` and its `<tspan>`s.
    QString text;

    qint64 line = 0;
    QList<RawElement> children;
};

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

/// Reads SVG's number lists: `1.5,-2`, `1.5-2`, `.5.5` and `1e-3` alike.
class NumberScanner
{
public:
    explicit NumberScanner(QStringView text)
        : m_text(text)
    {
    }

    bool atEnd()
    {
        skipSeparators();
        return m_pos >= m_text.size();
    }

    QChar peek()
    {
        skipSeparators();
        return m_pos < m_text.size() ? m_text.at(m_pos) : QChar();
    }

    void advance() { ++m_pos; }

    bool atNumber()
    {
        const QChar c = peek();
        return c.isDigit() || c == u'-' || c == u'+' || c == u'.';
    }

    std::optional<qreal> number()
    {
        skipSeparators();
        const qsizetype start = m_pos;
        qsizetype pos = m_pos;
        const auto digits = [&] {
            const qsizetype from = pos;
            while (pos < m_text.size() && m_text.at(pos).isDigit())
                ++pos;
            return pos > from;
        };

        if (pos < m_text.size() && (m_text.at(pos) == u'-' || m_text.at(pos) == u'+'))
            ++pos;
        bool mantissa = digits();
        if (pos < m_text.size() && m_text.at(pos) == u'.') {
            ++pos;
            mantissa = digits() || mantissa;
        }
        if (!mantissa)
            return std::nullopt;

        if (pos < m_text.size() && (m_text.at(pos) == u'e' || m_text.at(pos) == u'E')) {
            qsizetype exponent = pos + 1;
            if (exponent < m_text.size()
                && (m_text.at(exponent) == u'-' || m_text.at(exponent) == u'+'))
                ++exponent;
            if (exponent < m_text.size() && m_text.at(exponent).isDigit()) {
                pos = exponent;
                digits();
            }
        }

        bool ok = false;
        const qreal value = m_text.sliced(start, pos - start).toDouble(&ok);
        if (!ok)
            return std::nullopt;
        m_pos = pos;
        return value;
    }

    /// An arc's flag, which may be packed against what follows: `a1 1 0 01 5 5`.
    std::optional<bool> flag()
    {
        skipSeparators();
        if (m_pos >= m_text.size())
            return std::nullopt;
        const QChar c = m_text.at(m_pos);
        if (c != u'0' && c != u'1')
            return std::nullopt;
        ++m_pos;
        return c == u'1';
    }

private:
    void skipSeparators()
    {
        while (m_pos < m_text.size() && (m_text.at(m_pos).isSpace() || m_text.at(m_pos) == u','))
            ++m_pos;
    }

    QStringView m_text;
    qsizetype m_pos = 0;
};

/// A length attribute's number. Units are ignored: the document is taken to be
/// in user units throughout.
qreal length(const QHash<QString, QString> &attributes, const QString &name, qreal fallback = 0.0)
{
    const auto it = attributes.constFind(name);
    if (it == attributes.cend())
        return fallback;
    NumberScanner scanner(*it);
    return scanner.number().value_or(fallback);
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

/// A `transform` attribute. In the list `A B`, B applies first; in Qt's row
/// vector convention that is `B * A`.
QTransform parseTransform(QStringView text, bool *ok)
{
    *ok = true;
    QTransform result;
    qsizetype pos = 0;

    while (true) {
        while (pos < text.size() && (text.at(pos).isSpace() || text.at(pos) == u','))
            ++pos;
        if (pos >= text.size())
            break;

        const qsizetype open = text.indexOf(u'(', pos);
        const qsizetype close = open < 0 ? -1 : text.indexOf(u')', open);
        if (open < 0 || close < 0) {
            *ok = false;
            break;
        }

        const QStringView name = text.sliced(pos, open - pos).trimmed();
        QList<qreal> args;
        NumberScanner scanner(text.sliced(open + 1, close - open - 1));
        while (!scanner.atEnd()) {
            const std::optional<qreal> value = scanner.number();
            if (!value) {
                *ok = false;
                return result;
            }
            args.append(*value);
        }
        pos = close + 1;

        const auto arg = [&](qsizetype i, qreal fallback) {
            return i < args.size() ? args.at(i) : fallback;
        };

        QTransform step;
        if (name == u"matrix" && args.size() == 6) {
            step = QTransform(args[0], args[1], args[2], args[3], args[4], args[5]);
        } else if (name == u"translate" && !args.isEmpty()) {
            step = QTransform::fromTranslate(args[0], arg(1, 0.0));
        } else if (name == u"scale" && !args.isEmpty()) {
            step = QTransform::fromScale(args[0], arg(1, args[0]));
        } else if (name == u"rotate" && (args.size() == 1 || args.size() == 3)) {
            const qreal radians = qDegreesToRadians(args[0]);
            const qreal cx = arg(1, 0.0);
            const qreal cy = arg(2, 0.0);
            const QTransform turn(std::cos(radians), std::sin(radians), -std::sin(radians),
                                  std::cos(radians), 0.0, 0.0);
            step = QTransform::fromTranslate(-cx, -cy) * turn * QTransform::fromTranslate(cx, cy);
        } else if (name == u"skewX" && args.size() == 1) {
            step = QTransform(1.0, 0.0, std::tan(qDegreesToRadians(args[0])), 1.0, 0.0, 0.0);
        } else if (name == u"skewY" && args.size() == 1) {
            step = QTransform(1.0, std::tan(qDegreesToRadians(args[0])), 0.0, 1.0, 0.0, 0.0);
        } else {
            *ok = false;
            continue;
        }
        result = step * result;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Path data
// ---------------------------------------------------------------------------

qreal angleBetween(const QPointF &u, const QPointF &v)
{
    return std::atan2(u.x() * v.y() - u.y() * v.x(), u.x() * v.x() + u.y() * v.y());
}

/// An elliptical arc from `from` to `to`, as SVG gives one, drawn as cubics of
/// at most a quarter turn each. The centre is found as in the SVG spec's
/// implementation notes (F.6.5), including the scaling up of radii too small to
/// reach.
void arcTo(QPainterPath &path, const QPointF &from, qreal rx, qreal ry, qreal xAxisRotationDeg,
           bool largeArc, bool sweep, const QPointF &to)
{
    if (from == to)
        return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx == 0.0 || ry == 0.0) {
        path.lineTo(to);
        return;
    }

    const qreal phi = qDegreesToRadians(xAxisRotationDeg);
    const qreal cosPhi = std::cos(phi);
    const qreal sinPhi = std::sin(phi);

    const qreal halfDx = (from.x() - to.x()) / 2.0;
    const qreal halfDy = (from.y() - to.y()) / 2.0;
    const qreal x1 = cosPhi * halfDx + sinPhi * halfDy;
    const qreal y1 = -sinPhi * halfDx + cosPhi * halfDy;

    const qreal lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1.0) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }

    const qreal rx2 = rx * rx;
    const qreal ry2 = ry * ry;
    const qreal denominator = rx2 * y1 * y1 + ry2 * x1 * x1;
    qreal coefficient = denominator > 0.0
                            ? std::sqrt(qMax(0.0, (rx2 * ry2 - denominator) / denominator))
                            : 0.0;
    if (largeArc == sweep)
        coefficient = -coefficient;

    const qreal cx1 = coefficient * rx * y1 / ry;
    const qreal cy1 = -coefficient * ry * x1 / rx;
    const qreal cx = cosPhi * cx1 - sinPhi * cy1 + (from.x() + to.x()) / 2.0;
    const qreal cy = sinPhi * cx1 + cosPhi * cy1 + (from.y() + to.y()) / 2.0;

    const QPointF startVector((x1 - cx1) / rx, (y1 - cy1) / ry);
    const QPointF endVector((-x1 - cx1) / rx, (-y1 - cy1) / ry);
    const qreal startAngle = angleBetween(QPointF(1.0, 0.0), startVector);
    qreal sweepAngle = angleBetween(startVector, endVector);
    if (!sweep && sweepAngle > 0.0)
        sweepAngle -= 2.0 * M_PI;
    else if (sweep && sweepAngle < 0.0)
        sweepAngle += 2.0 * M_PI;

    const auto onEllipse = [&](qreal ux, qreal uy) {
        return QPointF(cx + rx * ux * cosPhi - ry * uy * sinPhi,
                       cy + rx * ux * sinPhi + ry * uy * cosPhi);
    };

    const int segments = qMax(1, int(std::ceil(std::abs(sweepAngle) / (M_PI / 2.0) - 1e-9)));
    const qreal step = sweepAngle / segments;
    const qreal handle = 4.0 / 3.0 * std::tan(step / 4.0);
    for (int i = 0; i < segments; ++i) {
        const qreal a1 = startAngle + step * i;
        const qreal a2 = a1 + step;
        const QPointF c1 = onEllipse(std::cos(a1) - handle * std::sin(a1),
                                     std::sin(a1) + handle * std::cos(a1));
        const QPointF c2 = onEllipse(std::cos(a2) + handle * std::sin(a2),
                                     std::sin(a2) - handle * std::cos(a2));
        // The last end exactly where the arc was asked to end, not where
        // rounding has carried it.
        const QPointF end = i + 1 == segments ? to : onEllipse(std::cos(a2), std::sin(a2));
        path.cubicTo(c1, c2, end);
    }
}

/// Path data, as far as it is valid: per the spec, a path is drawn up to the
/// first error in it, and `error` says what that was.
QPainterPath parsePathData(QStringView data, QString *error)
{
    QPainterPath path;
    NumberScanner scanner(data);

    QChar command;
    QPointF current;
    QPointF subpathStart;
    QPointF lastCubicControl;
    QPointF lastQuadControl;
    QChar previous;
    bool closed = false;

    const auto fail = [&](const QString &why) {
        *error = why;
        return path;
    };

    while (!scanner.atEnd()) {
        if (scanner.peek().isLetter()) {
            command = scanner.peek();
            scanner.advance();
        } else if (command.isNull()) {
            return fail(QStringLiteral("path data does not start with a command"));
        } else if (command == u'Z' || command == u'z') {
            return fail(QStringLiteral("numbers after a close-path"));
        }

        const bool relative = command.isLower();
        const QChar upper = command.toUpper();
        const QPointF origin = relative ? current : QPointF();

        const auto numbers = [&](int count, qreal *out) {
            for (int i = 0; i < count; ++i) {
                const std::optional<qreal> value = scanner.number();
                if (!value)
                    return false;
                out[i] = *value;
            }
            return true;
        };

        // A drawing command straight after a close-path starts a new subpath
        // where the closed one began. QPainterPath would start it at (0, 0).
        if (closed && upper != u'M' && upper != u'Z') {
            path.moveTo(current);
            closed = false;
        }

        qreal n[7];
        if (upper == u'Z') {
            path.closeSubpath();
            current = subpathStart;
            closed = true;
        } else if (upper == u'M') {
            if (!numbers(2, n))
                return fail(QStringLiteral("bad move-to"));
            current = origin + QPointF(n[0], n[1]);
            path.moveTo(current);
            subpathStart = current;
            closed = false;
            // Further pairs after a move-to are line-tos.
            command = relative ? u'l' : u'L';
        } else if (upper == u'L') {
            if (!numbers(2, n))
                return fail(QStringLiteral("bad line-to"));
            current = origin + QPointF(n[0], n[1]);
            path.lineTo(current);
        } else if (upper == u'H') {
            if (!numbers(1, n))
                return fail(QStringLiteral("bad horizontal line-to"));
            current.setX((relative ? current.x() : 0.0) + n[0]);
            path.lineTo(current);
        } else if (upper == u'V') {
            if (!numbers(1, n))
                return fail(QStringLiteral("bad vertical line-to"));
            current.setY((relative ? current.y() : 0.0) + n[0]);
            path.lineTo(current);
        } else if (upper == u'C' || upper == u'S') {
            QPointF control1;
            if (upper == u'C') {
                if (!numbers(6, n))
                    return fail(QStringLiteral("bad curve-to"));
                control1 = origin + QPointF(n[0], n[1]);
                lastCubicControl = origin + QPointF(n[2], n[3]);
                current = origin + QPointF(n[4], n[5]);
            } else {
                if (!numbers(4, n))
                    return fail(QStringLiteral("bad smooth curve-to"));
                const bool follows = previous == u'C' || previous == u'S';
                control1 = follows ? 2.0 * current - lastCubicControl : current;
                lastCubicControl = origin + QPointF(n[0], n[1]);
                current = origin + QPointF(n[2], n[3]);
            }
            path.cubicTo(control1, lastCubicControl, current);
        } else if (upper == u'Q' || upper == u'T') {
            if (upper == u'Q') {
                if (!numbers(4, n))
                    return fail(QStringLiteral("bad quadratic curve-to"));
                lastQuadControl = origin + QPointF(n[0], n[1]);
                current = origin + QPointF(n[2], n[3]);
            } else {
                if (!numbers(2, n))
                    return fail(QStringLiteral("bad smooth quadratic curve-to"));
                const bool follows = previous == u'Q' || previous == u'T';
                lastQuadControl = follows ? 2.0 * current - lastQuadControl : current;
                current = origin + QPointF(n[0], n[1]);
            }
            path.quadTo(lastQuadControl, current);
        } else if (upper == u'A') {
            if (!numbers(3, n))
                return fail(QStringLiteral("bad arc"));
            const std::optional<bool> largeArc = scanner.flag();
            const std::optional<bool> sweep = largeArc ? scanner.flag() : std::nullopt;
            if (!sweep || !numbers(2, n + 3))
                return fail(QStringLiteral("bad arc"));
            const QPointF to = origin + QPointF(n[3], n[4]);
            arcTo(path, current, n[0], n[1], n[2], *largeArc, *sweep, to);
            current = to;
        } else {
            return fail(QStringLiteral("unknown path command '%1'").arg(command));
        }
        previous = upper;
    }
    return path;
}

QList<qreal> numberList(QStringView text)
{
    QList<qreal> list;
    NumberScanner scanner(text);
    while (!scanner.atEnd()) {
        const std::optional<qreal> value = scanner.number();
        if (!value)
            break;
        list.append(*value);
    }
    return list;
}

// ---------------------------------------------------------------------------
// Styling
// ---------------------------------------------------------------------------

QString styleProperty(const QHash<QString, QString> &attributes, const QString &name)
{
    const QString style = attributes.value(QStringLiteral("style"));
    for (QStringView declaration : QStringView(style).split(u';')) {
        const qsizetype colon = declaration.indexOf(u':');
        if (colon < 0)
            continue;
        if (declaration.first(colon).trimmed() == name)
            return declaration.sliced(colon + 1).trimmed().toString();
    }
    return attributes.value(name).trimmed();
}

QColor parseColour(const QString &value)
{
    if (value.isEmpty() || value == QLatin1String("none"))
        return {};
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    return QColor::fromString(value);
#else
    return QColor(value);
#endif
}

bool isHidden(const QHash<QString, QString> &attributes)
{
    return styleProperty(attributes, QStringLiteral("display")) == QLatin1String("none");
}

// ---------------------------------------------------------------------------
// Reading the file
// ---------------------------------------------------------------------------

RawElement readElement(QXmlStreamReader &xml)
{
    RawElement element;
    element.tag = xml.name().toString();
    element.inSvgNamespace = xml.namespaceUri().isEmpty() || xml.namespaceUri() == kSvgNamespace;
    element.line = xml.lineNumber();

    for (const QXmlStreamAttribute &attribute : xml.attributes()) {
        element.attributes.insert(attribute.qualifiedName().toString(), attribute.value().toString());
        if (attribute.name() == u"href"
            && (attribute.namespaceUri().isEmpty() || attribute.namespaceUri() == kXlinkNamespace))
            element.href = attribute.value().toString();
    }

    while (!xml.atEnd() && xml.readNext() != QXmlStreamReader::Invalid) {
        if (xml.isEndElement())
            break;
        if (xml.isStartElement()) {
            element.children.append(readElement(xml));
            element.text += element.children.last().text;
        } else if (xml.isCharacters()) {
            element.text += xml.text();
        }
    }
    return element;
}

void indexIds(const RawElement &element, QHash<QString, const RawElement *> &byId)
{
    const QString id = element.attributes.value(QStringLiteral("id"));
    if (!id.isEmpty() && !byId.contains(id))
        byId.insert(id, &element);
    for (const RawElement &child : element.children)
        indexIds(child, byId);
}

class Builder
{
public:
    Builder(const RawElement &root, QStringList &warnings)
        : m_warnings(warnings)
    {
        indexIds(root, m_byId);
    }

    /// The element, or nothing if it is not something with geometry to report.
    std::optional<SvgElement> build(const RawElement &raw, const QTransform &parent)
    {
        if (!raw.inSvgNamespace || isHidden(raw.attributes) || isSkipped(raw.tag))
            return std::nullopt;

        if (raw.tag == u"use")
            return expandUse(raw, parent);

        SvgElement element;
        element.tag = raw.tag;
        element.id = raw.attributes.value(QStringLiteral("id"));
        element.classes = raw.attributes.value(QStringLiteral("class"))
                              .split(u' ', Qt::SkipEmptyParts);
        element.attributes = raw.attributes;
        element.fill = parseColour(styleProperty(raw.attributes, QStringLiteral("fill")));

        QTransform own;
        const auto transform = raw.attributes.constFind(QStringLiteral("transform"));
        if (transform != raw.attributes.cend()) {
            bool ok = false;
            own = parseTransform(*transform, &ok);
            if (!ok)
                warn(raw, QStringLiteral("a transform that could not be read in full: \"%1\"")
                              .arg(*transform));
        }
        element.transform = own * parent;

        buildGeometry(raw, element);

        for (const RawElement &child : raw.children) {
            if (child.tag == u"title" && child.inSvgNamespace) {
                if (element.title.isEmpty())
                    element.title = child.text.simplified();
                continue;
            }
            // A text's tspans are its text, already gathered; they are not
            // shapes of their own.
            if (element.tag == u"text")
                continue;
            if (std::optional<SvgElement> built = build(child, element.transform))
                element.children.append(std::move(*built));
        }
        return element;
    }

private:
    static bool isSkipped(const QString &tag)
    {
        static const QSet<QString> skipped{
            QStringLiteral("defs"),           QStringLiteral("clipPath"),
            QStringLiteral("mask"),           QStringLiteral("symbol"),
            QStringLiteral("marker"),         QStringLiteral("pattern"),
            QStringLiteral("linearGradient"), QStringLiteral("radialGradient"),
            QStringLiteral("filter"),         QStringLiteral("style"),
            QStringLiteral("script"),         QStringLiteral("metadata"),
            QStringLiteral("title"),          QStringLiteral("desc"),
            QStringLiteral("image"),          QStringLiteral("foreignObject"),
        };
        return skipped.contains(tag);
    }

    void buildGeometry(const RawElement &raw, SvgElement &element)
    {
        const QHash<QString, QString> &a = raw.attributes;
        const QString &tag = raw.tag;

        if (tag == u"path") {
            QString error;
            element.path = parsePathData(a.value(QStringLiteral("d")), &error);
            if (!error.isEmpty())
                warn(raw, QStringLiteral("path data drawn only up to an error: %1").arg(error));
        } else if (tag == u"rect") {
            const QRectF rect(length(a, QStringLiteral("x")), length(a, QStringLiteral("y")),
                              length(a, QStringLiteral("width")),
                              length(a, QStringLiteral("height")));
            if (rect.width() <= 0.0 || rect.height() <= 0.0)
                return;
            // Either radius alone sets both, as the spec says.
            const bool hasRx = a.contains(QStringLiteral("rx"));
            const bool hasRy = a.contains(QStringLiteral("ry"));
            qreal rx = length(a, QStringLiteral("rx"));
            qreal ry = length(a, QStringLiteral("ry"));
            if (hasRx && !hasRy)
                ry = rx;
            else if (hasRy && !hasRx)
                rx = ry;
            rx = qBound(0.0, rx, rect.width() / 2.0);
            ry = qBound(0.0, ry, rect.height() / 2.0);
            element.rect = rect;
            if (rx > 0.0 && ry > 0.0)
                element.path.addRoundedRect(rect, rx, ry);
            else
                element.path.addRect(rect);
        } else if (tag == u"circle") {
            const qreal r = length(a, QStringLiteral("r"));
            if (r > 0.0)
                element.path.addEllipse(
                    QPointF(length(a, QStringLiteral("cx")), length(a, QStringLiteral("cy"))), r, r);
        } else if (tag == u"ellipse") {
            const qreal rx = length(a, QStringLiteral("rx"));
            const qreal ry = length(a, QStringLiteral("ry"));
            if (rx > 0.0 && ry > 0.0)
                element.path.addEllipse(
                    QPointF(length(a, QStringLiteral("cx")), length(a, QStringLiteral("cy"))), rx,
                    ry);
        } else if (tag == u"line") {
            element.path.moveTo(length(a, QStringLiteral("x1")), length(a, QStringLiteral("y1")));
            element.path.lineTo(length(a, QStringLiteral("x2")), length(a, QStringLiteral("y2")));
        } else if (tag == u"polyline" || tag == u"polygon") {
            const QList<qreal> points = numberList(a.value(QStringLiteral("points")));
            for (qsizetype i = 0; i + 1 < points.size(); i += 2) {
                if (i == 0)
                    element.path.moveTo(points.at(i), points.at(i + 1));
                else
                    element.path.lineTo(points.at(i), points.at(i + 1));
            }
            if (tag == u"polygon" && points.size() >= 4)
                element.path.closeSubpath();
        } else if (tag == u"text") {
            element.text = raw.text.simplified();
            const RawElement *positioned = &raw;
            if (!a.contains(QStringLiteral("x")) && !a.contains(QStringLiteral("y"))) {
                for (const RawElement &child : raw.children) {
                    if (child.tag == u"tspan") {
                        positioned = &child;
                        break;
                    }
                }
            }
            const QList<qreal> x = numberList(positioned->attributes.value(QStringLiteral("x")));
            const QList<qreal> y = numberList(positioned->attributes.value(QStringLiteral("y")));
            element.textPosition = QPointF(x.value(0), y.value(0));
        }
    }

    std::optional<SvgElement> expandUse(const RawElement &use, const QTransform &parent)
    {
        if (!use.href.startsWith(u'#')) {
            warn(use, QStringLiteral("a <use> that does not reference an element in this file"));
            return std::nullopt;
        }
        const QString targetId = use.href.sliced(1);
        const RawElement *target = m_byId.value(targetId);
        if (!target) {
            warn(use, QStringLiteral("a <use> of \"%1\", which does not exist").arg(targetId));
            return std::nullopt;
        }
        if (m_useStack.contains(target) || m_useStack.size() >= kMaxUseDepth) {
            warn(use, QStringLiteral("a <use> of \"%1\" that refers back to itself").arg(targetId));
            return std::nullopt;
        }

        QTransform own;
        const auto transform = use.attributes.constFind(QStringLiteral("transform"));
        if (transform != use.attributes.cend()) {
            bool ok = false;
            own = parseTransform(*transform, &ok);
            if (!ok)
                warn(use, QStringLiteral("a transform that could not be read in full: \"%1\"")
                              .arg(*transform));
        }
        const QTransform placed = QTransform::fromTranslate(length(use.attributes, QStringLiteral("x")),
                                                            length(use.attributes, QStringLiteral("y")))
                                  * own * parent;

        m_useStack.append(target);
        std::optional<SvgElement> copy = build(*target, placed);
        m_useStack.removeLast();
        if (!copy)
            return std::nullopt;

        clearIds(*copy);
        copy->id = use.attributes.value(QStringLiteral("id"));
        copy->title.clear();
        for (const RawElement &child : use.children) {
            if (child.tag == u"title" && child.inSvgNamespace) {
                copy->title = child.text.simplified();
                break;
            }
        }

        const QString useClass = use.attributes.value(QStringLiteral("class"));
        if (!useClass.trimmed().isEmpty())
            copy->classes = useClass.split(u' ', Qt::SkipEmptyParts);
        const QColor useFill = parseColour(styleProperty(use.attributes, QStringLiteral("fill")));
        if (useFill.isValid())
            copy->fill = useFill;

        // Data the file attaches to the element, like its fill, belongs to the
        // clone that sets it. A mirrored control is a different control, and
        // would otherwise say whatever the original says about itself.
        for (auto it = use.attributes.cbegin(); it != use.attributes.cend(); ++it) {
            if (it.key().startsWith(u"data-"))
                copy->attributes.insert(it.key(), it.value());
        }
        return copy;
    }

    static void clearIds(SvgElement &element)
    {
        element.id.clear();
        for (SvgElement &child : element.children)
            clearIds(child);
    }

    void warn(const RawElement &raw, const QString &message)
    {
        m_warnings.append(QStringLiteral("line %1: %2").arg(raw.line).arg(message));
    }

    QStringList &m_warnings;
    QHash<QString, const RawElement *> m_byId;
    QList<const RawElement *> m_useStack;
};

} // namespace

bool SvgElement::hasClass(QStringView name) const
{
    for (const QString &c : classes) {
        if (c == name)
            return true;
    }
    return false;
}

QString SvgElement::property(const QString &name) const
{
    return styleProperty(attributes, name);
}

QPainterPath SvgElement::outline() const
{
    return transform.map(path);
}

SvgDocument readSvg(QIODevice &device)
{
    SvgDocument document;
    QXmlStreamReader xml(&device);

    std::optional<RawElement> root;
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            root = readElement(xml);
            break;
        }
    }
    // Read to the end, so an error after the root element is reported too.
    while (!xml.atEnd() && !xml.hasError())
        xml.readNext();

    if (xml.hasError()) {
        document.error = QStringLiteral("line %1, column %2: %3")
                             .arg(xml.lineNumber())
                             .arg(xml.columnNumber())
                             .arg(xml.errorString());
        return document;
    }
    if (!root || root->tag != u"svg" || !root->inSvgNamespace) {
        document.error = QStringLiteral("not an SVG document");
        return document;
    }

    Builder builder(*root, document.warnings);
    if (std::optional<SvgElement> built = builder.build(*root, QTransform()))
        document.root = std::move(*built);
    return document;
}

} // namespace hidcenter::svg
