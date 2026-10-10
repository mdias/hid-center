// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelSchematic.h"

#include "WheelLayouts.h"

#include <QCursor>
#include <QFont>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QIcon>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyle>
#include <QToolTip>
#include <QtMath>

#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Room round the wheel, so its outline never touches the widget's edge.
constexpr qreal kMarginPx = 12.0;

/// Line widths, in millimetres on the wheel, and never thinner than a pixel.
constexpr qreal kOutlineMm = 1.0;
constexpr qreal kEdgeMm = 0.45;
constexpr qreal kFineMm = 0.3;

/// Text smaller than this is not drawn at all, rather than drawn as a smudge.
constexpr int kMinTextPx = 6;

/// How large a control's warning icon is drawn, in pixels: in step with the
/// control, but never too small to make out or large enough to hide the wheel.
constexpr qreal kMinWarningPx = 14.0;
constexpr qreal kMaxWarningPx = 24.0;

/// How much of a see-through body is painted: enough for the rim to keep its
/// shape, little enough that a paddle behind it reads as a paddle. The outline
/// keeps more, because the outline is what the shape is.
constexpr qreal kSeeThroughFill = 0.3;
constexpr qreal kSeeThroughOutline = 0.6;

// The shades, fixed rather than taken from the palette: see the class comment.
// The outline is the one light thing about the wheel. It is what keeps a black
// grip from vanishing into a dark theme's background.
const QColor kOutline(0x80, 0x81, 0x8a);
const QColor kPlateEdge(0x5e, 0x5f, 0x66);
const QColor kGrip(0x2a, 0x2a, 0x2e);
const QColor kCarbon(0x37, 0x38, 0x3d);
const QColor kPlate(0x22, 0x23, 0x27);
const QColor kHousing(0x24, 0x24, 0x28);
const QColor kBand(0x43, 0x44, 0x4a);
const QColor kSlot(0x0d, 0x0d, 0x0f);
const QColor kSlotEdge(0x45, 0x46, 0x4c);
/// A light that is off: an RPM LED, the ring round a button, the ring round an
/// encoder. A shade off the dark around it, so where the light is still shows.
const QColor kLedOff(0x26, 0x27, 0x2b);
const QColor kSeam(0x24, 0x25, 0x29);
const QColor kStitch(0xcf, 0x2d, 0x2d);

const QColor kBezel(0x15, 0x15, 0x17);
const QColor kBezelEdge(0x45, 0x46, 0x4c);
const QColor kCap(0x2f, 0x30, 0x35);
const QColor kCapEdge(0x7a, 0x7b, 0x82);
const QColor kCapHighlight(0x55, 0x56, 0x5c);
const QColor kInkOnLit(0x14, 0x14, 0x16);
const QColor kInkOnDark(0xc8, 0xc9, 0xce);

const QColor kAnodised(0xcc, 0x22, 0x2c);
const QColor kAnodisedEdge(0x6e, 0x0e, 0x14);
const QColor kAnodisedRib(0x85, 0x12, 0x1a);

const QColor kPaddle(0x4b, 0x4c, 0x52);
const QColor kPaddleEdge(0x7c, 0x7d, 0x84);

const QColor kLogo(0xb8, 0xb9, 0xbf);

QColor fillFor(WheelMaterial material)
{
    switch (material) {
    case WheelMaterial::Grip:    return kGrip;
    case WheelMaterial::Carbon:  return kCarbon;
    case WheelMaterial::Plate:   return kPlate;
    case WheelMaterial::Housing: return kHousing;
    case WheelMaterial::Band:    return kBand;
    case WheelMaterial::Seam:    return kSeam;
    case WheelMaterial::Stitch:  return kStitch;
    }
    return kCarbon;
}

bool isStroked(WheelMaterial material)
{
    return material == WheelMaterial::Seam || material == WheelMaterial::Stitch;
}

/// A pen `widthMm` wide on the wheel, for a painter already scaled to
/// millimetres, and at least a pixel wide on screen however small the widget.
QPen penMm(const QColor &colour, qreal widthMm, qreal pxPerMm)
{
    QPen pen(colour, qMax(widthMm, 1.0 / pxPerMm));
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    return pen;
}

QColor blend(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

/// A light at `glow`, from 0 for showing only `unlit` to 1 for fully `lit`.
QColor dimmed(const QColor &lit, const QColor &unlit, qreal glow)
{
    return blend(unlit, lit, glow);
}

/// The ink that stands out more on a cap of this shade: dark on a bright cap,
/// light on one dimmed towards the dark.
QColor inkFor(const QColor &cap)
{
    const qreal lightness = cap.lightnessF();
    return qAbs(lightness - kInkOnLit.lightnessF()) > qAbs(lightness - kInkOnDark.lightnessF())
               ? kInkOnLit
               : kInkOnDark;
}

/// Text centred on a point on the wheel, `heightMm` tall and no wider than
/// `maxWidthMm`.
///
/// Laid out in device pixels rather than through the millimetre transform, so
/// the font is hinted at the size it is actually shown.
void drawText(QPainter &painter, const QTransform &toWidget, const QPointF &centreMm,
              qreal rotationDeg, qreal heightMm, qreal maxWidthMm, const QString &text,
              const QColor &colour, bool italic = false)
{
    const qreal scale = toWidget.m11();

    QFont font = painter.font();
    font.setBold(true);
    font.setItalic(italic);
    font.setPixelSize(qRound(heightMm * scale));

    const qreal width = QFontMetricsF(font).horizontalAdvance(text);
    const qreal maxWidth = maxWidthMm * scale;
    if (width > maxWidth && width > 0.0)
        font.setPixelSize(int(font.pixelSize() * maxWidth / width));
    if (font.pixelSize() < kMinTextPx)
        return;

    painter.save();
    painter.setTransform(QTransform());
    painter.translate(toWidget.map(centreMm));
    painter.rotate(rotationDeg);
    painter.setFont(font);
    painter.setPen(colour);
    painter.drawText(QRectF(-maxWidth, -maxWidth, 2.0 * maxWidth, 2.0 * maxWidth),
                     Qt::AlignCenter, text);
    painter.restore();
}

/// A ring with teeth round its edge, which is what makes a knob read as
/// something to turn.
QPainterPath knurled(const QPointF &centre, qreal outer, qreal inner, int teeth)
{
    QPolygonF points;
    points.reserve(teeth * 2);
    for (int i = 0; i < teeth * 2; ++i) {
        const qreal radians = M_PI * i / teeth;
        const qreal radius = i % 2 == 0 ? outer : inner;
        points.append(centre + QPointF(radius * std::cos(radians), radius * std::sin(radians)));
    }

    QPainterPath path;
    path.addPolygon(points);
    path.closeSubpath();
    return path;
}

/// What is printed on a cap: a glyph if it has one, else its legend.
void drawCapMarking(QPainter &painter, const QTransform &toWidget, const WheelControl &control,
                    qreal capRadius, const QColor &ink)
{
    const QPointF c = control.centre;
    const qreal s = capRadius;

    switch (control.glyph) {
    case CapGlyph::ArrowLeft:
    case CapGlyph::ArrowRight: {
        const qreal towards = control.glyph == CapGlyph::ArrowLeft ? -1.0 : 1.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawPolygon(QPolygonF{c + QPointF(towards * s * 0.5, 0.0),
                                      c + QPointF(-towards * s * 0.3, -s * 0.45),
                                      c + QPointF(-towards * s * 0.3, s * 0.45)});
        return;
    }
    case CapGlyph::Power: {
        QPen pen(ink, s * 0.14);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const qreal ring = s * 0.45;
        // From just right of twelve o'clock, clockwise almost all the way
        // round: the gap at the top is where the stroke comes down through it.
        painter.drawArc(QRectF(c.x() - ring, c.y() - ring, 2.0 * ring, 2.0 * ring), 60 * 16,
                        -300 * 16);
        painter.drawLine(c + QPointF(0.0, -s * 0.62), c + QPointF(0.0, -s * 0.05));
        return;
    }
    case CapGlyph::None:
        break;
    }

    if (!control.legend.isEmpty())
        drawText(painter, toWidget, c, 0.0, s * 0.62, s * 1.6, control.legend, ink);
}

/// `colour` is what the cap is lit in, and invalid for a cap that is off.
/// `glow` is how brightly, from 0, drawn as off, to 1.
void drawButton(QPainter &painter, const QTransform &toWidget, const WheelControl &control,
                const QColor &colour, qreal glow)
{
    const qreal scale = toWidget.m11();
    const QPointF c = control.centre;
    const qreal r = control.size.width() / 2.0;

    painter.setPen(penMm(kBezelEdge, kFineMm, scale));
    painter.setBrush(kBezel);
    painter.drawEllipse(c, r, r);

    // How a lit button looks on the wheel: a ring of the colour, a dark gap,
    // and the cap glowing a paler shade of it with the marking dark on top.
    // Turned down, both sink towards the plain cap and dark ring of a button
    // that is off, and the marking goes light once the cap is too dark to carry
    // dark ink. Off is only the far end of that, so a button keeps its shape
    // whatever it is set to.
    if (!colour.isValid())
        glow = 0.0;
    const QColor lit = colour.isValid() ? colour : kLedOff;

    painter.setPen(QPen(dimmed(lit, kLedOff, glow), r * 0.16));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(c, r * 0.80, r * 0.80);

    // The edge is what makes out an unlit cap against the bezel. A lit one is
    // edged in its own colour, which is the same as not being edged.
    const QColor cap = dimmed(blend(lit, Qt::white, 0.45), kCap, glow);
    painter.setPen(penMm(dimmed(cap, kCapEdge, glow), kFineMm, scale));
    painter.setBrush(cap);
    painter.drawEllipse(c, r * 0.62, r * 0.62);

    drawCapMarking(painter, toWidget, control, r * 0.62, inkFor(cap));
}

/// `colour` is what the ring round the knob is lit in, and invalid for a knob
/// without one. `glow` is how brightly, from 0, drawn dark, to 1.
void drawEncoder(QPainter &painter, const WheelControl &control, const QColor &colour, qreal glow,
                 qreal scale)
{
    const QPointF c = control.centre;
    const qreal r = control.size.width() / 2.0;

    painter.setPen(penMm(kBezelEdge, kFineMm, scale));
    painter.setBrush(kBezel);
    painter.drawEllipse(c, r, r);

    // Whether the knob has a ring decides its size, not how bright the ring is,
    // so turning the lights down leaves the dashes dark rather than the knob
    // growing into where they were.
    qreal knob = r * 0.78;
    if (colour.isValid()) {
        // The LEDs round the knob, which show on the wheel as a dashed circle.
        QPen ring(dimmed(colour, kLedOff, glow), r * 0.1);
        ring.setCapStyle(Qt::FlatCap);
        ring.setDashPattern({2.0, 1.3});
        painter.setPen(ring);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(c, r * 0.84, r * 0.84);
        knob = r * 0.64;
    }

    painter.setPen(penMm(kCapEdge, kFineMm, scale));
    painter.setBrush(kCap);
    painter.drawPath(knurled(c, knob, knob * 0.9, 18));

    painter.setPen(penMm(kBezelEdge, kFineMm, scale));
    painter.setBrush(kBezel);
    painter.drawEllipse(c, knob * 0.55, knob * 0.55);
}

void drawSevenWaySwitch(QPainter &painter, const WheelControl &control, qreal scale)
{
    const QPointF c = control.centre;
    const qreal r = control.size.width() / 2.0;

    painter.setPen(penMm(kBezelEdge, kFineMm, scale));
    painter.setBrush(kBezel);
    painter.drawEllipse(c, r, r);

    // The four ways it rocks, as arrowheads round a plain knob. They are what
    // tell it apart from a button of the same size.
    painter.setPen(Qt::NoPen);
    painter.setBrush(kCapEdge);
    const QPolygonF arrowhead{QPointF(0.0, -r * 0.93), QPointF(-r * 0.14, -r * 0.76),
                              QPointF(r * 0.14, -r * 0.76)};
    for (int i = 0; i < 4; ++i) {
        QTransform turn;
        turn.translate(c.x(), c.y());
        turn.rotate(90.0 * i);
        painter.drawPolygon(turn.map(arrowhead));
    }

    painter.setPen(penMm(kCapEdge, kFineMm, scale));
    painter.setBrush(kCap);
    painter.drawEllipse(c, r * 0.62, r * 0.62);

    painter.setPen(penMm(kCapHighlight, kFineMm, scale));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(c, r * 0.36, r * 0.36);
}

void drawRoller(QPainter &painter, const WheelControl &control, qreal scale)
{
    const qreal w = control.size.width();
    const qreal h = control.size.height();
    const qreal corner = qMin(w, h) * 0.22;

    painter.save();
    painter.translate(control.centre);
    painter.rotate(control.rotationDeg);

    painter.setPen(penMm(kAnodisedEdge, kFineMm, scale));
    painter.setBrush(kAnodised);
    painter.drawRoundedRect(QRectF(-w / 2.0, -h / 2.0, w, h), corner, corner);

    painter.setPen(penMm(kAnodisedRib, kFineMm, scale));
    const int ribs = qMax(3, qRound(w / 1.6));
    const qreal inset = h * 0.12;
    for (int i = 1; i < ribs; ++i) {
        const qreal x = -w / 2.0 + w * i / ribs;
        painter.drawLine(QPointF(x, -h / 2.0 + inset), QPointF(x, h / 2.0 - inset));
    }

    painter.restore();
}

void drawPaddle(QPainter &painter, const QTransform &toWidget, const WheelControl &control)
{
    const qreal scale = toWidget.m11();

    // A paddle not every wheel of the model has is drawn as a ghost of one:
    // there to be recognised and pointed at, but not claiming to be fitted.
    QPen pen = penMm(kPaddleEdge, kEdgeMm, scale);
    QColor fill = kPaddle;
    if (control.optional) {
        pen.setStyle(Qt::DashLine);
        fill.setAlphaF(0.4);
    }
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawPath(control.shape);

    if (!control.legend.isEmpty())
        drawText(painter, toWidget, control.centre, 0.0, 7.0, 10.0, control.legend, kInkOnDark);
}

/// `colours` is what each LED is lit in, in order, invalid for one that is off.
/// `glow` is how brightly, from 0, drawn as unlit, to 1.
void drawRpmLights(QPainter &painter, const WheelControl &control, const QList<QColor> &colours,
                   qreal glow, qreal scale)
{
    painter.setPen(penMm(kSlotEdge, kFineMm, scale));
    painter.setBrush(kSlot);
    painter.drawPath(control.shape);

    painter.setPen(Qt::NoPen);
    for (qsizetype i = 0; i < control.lights.size(); ++i) {
        const WheelLed &led = control.lights.at(i);
        const QColor &colour = colours.at(i);
        const qreal radius = qMin(led.rect.width(), led.rect.height()) / 2.0;
        painter.setBrush(colour.isValid() ? dimmed(colour, kLedOff, glow) : kLedOff);
        painter.drawRoundedRect(led.rect, radius, radius);
    }
}

void drawLabel(QPainter &painter, const QTransform &toWidget, const WheelLabel &label)
{
    drawText(painter, toWidget, label.centre, label.rotationDeg, label.size.height() * 0.8,
             label.size.width(), label.text, kLogo, true);
}

/// A word on a lit window, like a GTC's ABS. `colour` is what the LED behind it
/// is lit in, and invalid for one that is off; `glow` is how brightly, from 0,
/// drawn as off, to 1.
void drawLitLabel(QPainter &painter, const QTransform &toWidget, const WheelControl &control,
                  const QColor &colour, qreal glow)
{
    const qreal w = control.size.width();
    const qreal h = control.size.height();

    // The window diffuses the LED, so it shows a paler shade of the colour than
    // the LED itself, which is also what keeps the word on it legible. Turned
    // down it sinks to the dark of a light that is off, as a cap does, and the
    // word turns light once the window is too dark to carry dark ink.
    if (!colour.isValid())
        glow = 0.0;
    const QColor lit = colour.isValid() ? colour : kLedOff;
    const QColor window = dimmed(blend(lit, Qt::white, 0.45), kLedOff, glow);

    painter.save();
    painter.translate(control.centre);
    painter.rotate(control.rotationDeg);
    painter.setPen(penMm(kCapEdge, kFineMm, toWidget.m11()));
    painter.setBrush(window);
    painter.drawRoundedRect(QRectF(-w / 2.0, -h / 2.0, w, h), h * 0.35, h * 0.35);
    painter.restore();

    drawText(painter, toWidget, control.centre, control.rotationDeg, h * 0.7, w * 0.85,
             control.legend, inkFor(window));
}

} // namespace

WheelSchematic::WheelSchematic(QWidget *parent)
    : QWidget(parent)
{
    // Without this the cursor would only follow the pointer while a button was
    // held, which is exactly when it matters least.
    setMouseTracking(true);
}

void WheelSchematic::setWheel(const WheelLayout *wheel)
{
    if (wheel == m_wheel)
        return;

    m_wheel = wheel;
    // A millimetre spare all round, for the half of the outline's stroke that
    // falls outside the shape it is drawn along.
    m_bounds = wheel ? wheel->bounds().adjusted(-1.0, -1.0, 1.0, 1.0) : QRectF();

    // The indices belonged to the old wheel, and the pointer may be over
    // something else now without having moved: the wheel can be changed from
    // the keyboard. Which of them could be clicked belonged to it too.
    m_editable.clear();
    m_hovered = -1;
    m_pressed = -1;
    unsetCursor();
    if (underMouse())
        updateHover(QPointF(mapFromGlobal(QCursor::pos())));

    update();
}

void WheelSchematic::setControlColours(const QHash<QString, QColor> &colours)
{
    m_colours = colours;
    update();
}

QColor WheelSchematic::controlColour(const WheelControl &control) const
{
    return lightColour(control.key, control.colour);
}

QColor WheelSchematic::lightColour(const QString &key, const QColor &stock) const
{
    const auto it = m_colours.constFind(key);
    return it != m_colours.cend() ? *it : stock;
}

void WheelSchematic::setEditableControls(const QSet<QString> &keys)
{
    m_editable = keys;
    // The pointer may already be over one that has just become clickable.
    m_hovered = -1;
    if (underMouse())
        updateHover(QPointF(mapFromGlobal(QCursor::pos())));
}

void WheelSchematic::setLedBrightness(int percent)
{
    percent = qBound(0, percent, 100);
    if (percent == m_ledBrightness)
        return;

    m_ledBrightness = percent;
    update();
}

void WheelSchematic::setControlWarnings(const QHash<QString, QString> &warnings)
{
    if (warnings == m_warnings)
        return;

    m_warnings = warnings;
    update();
}

bool WheelSchematic::isClickable(int index) const
{
    return m_wheel && index >= 0 && m_editable.contains(m_wheel->controls.at(index).key);
}

QSize WheelSchematic::sizeHint() const
{
    return {640, 400};
}

QSize WheelSchematic::minimumSizeHint() const
{
    // Below about this the caps are too small to tell apart.
    return {320, 200};
}

QTransform WheelSchematic::toWidget() const
{
    if (!m_wheel || m_bounds.isEmpty())
        return {};

    const QRectF area = QRectF(rect()).adjusted(kMarginPx, kMarginPx, -kMarginPx, -kMarginPx);
    if (area.width() <= 0.0 || area.height() <= 0.0)
        return {};

    const qreal scale = qMin(area.width() / m_bounds.width(), area.height() / m_bounds.height());

    // Read bottom to top: centre the wheel on the origin, scale it, move it to
    // the middle of the widget.
    QTransform transform;
    transform.translate(area.center().x(), area.center().y());
    transform.scale(scale, scale);
    transform.translate(-m_bounds.center().x(), -m_bounds.center().y());
    return transform;
}

bool WheelSchematic::isCoveredAt(const QPointF &mm) const
{
    // A see-through body covers nothing, and neither does anything that is part
    // of it: it is drawn faint precisely so that what is behind can be reached.
    const bool bodyCovers = !m_wheel->seeThroughBody;
    if (bodyCovers && m_wheel->body.contains(mm))
        return true;

    for (const WheelSurface &surface : m_wheel->surfaces) {
        // A surface cut to the body covers nothing the body does not, and a
        // stroked one covers nothing at all.
        if (surface.clipToBody || isStroked(surface.material))
            continue;
        if (surface.onBody && !bodyCovers)
            continue;
        if (surface.path.contains(mm))
            return true;
    }
    return false;
}

int WheelSchematic::controlAt(const QPointF &point) const
{
    if (!m_wheel)
        return -1;

    bool invertible = false;
    const QPointF mm = toWidget().inverted(&invertible).map(point);
    if (!invertible)
        return -1;

    const QList<WheelControl> &controls = m_wheel->controls;

    // What is on the face first, and the last drawn first, since that is the one
    // on top.
    for (qsizetype i = controls.size() - 1; i >= 0; --i) {
        const WheelControl &control = controls.at(i);
        if (!isPaddle(control.kind) && control.shape.contains(mm))
            return int(i);
    }

    if (isCoveredAt(mm))
        return -1;

    for (qsizetype i = controls.size() - 1; i >= 0; --i) {
        const WheelControl &control = controls.at(i);
        if (isPaddle(control.kind) && control.shape.contains(mm))
            return int(i);
    }
    return -1;
}

bool WheelSchematic::event(QEvent *event)
{
    if (event->type() != QEvent::ToolTip)
        return QWidget::event(event);

    const auto *help = static_cast<QHelpEvent *>(event);
    const int index = controlAt(QPointF(help->pos()));
    if (index < 0) {
        QToolTip::hideText();
        event->ignore();
        return true;
    }

    const WheelControl &control = m_wheel->controls.at(index);
    QString tip = wheelControlName(control);
    if (const auto warning = m_warnings.constFind(control.key); warning != m_warnings.cend())
        tip += u'\n' + *warning;
    QToolTip::showText(help->globalPos(), tip, this,
                       toWidget().mapRect(control.shape.boundingRect()).toAlignedRect());
    return true;
}

void WheelSchematic::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = controlAt(event->position());
        m_pressed = isClickable(index) ? index : -1;
    }
    QWidget::mousePressEvent(event);
}

void WheelSchematic::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int pressed = m_pressed;
        m_pressed = -1;
        // Released somewhere else is changing one's mind, the way it is on any
        // push button.
        if (pressed >= 0 && controlAt(event->position()) == pressed)
            Q_EMIT controlClicked(pressed);
    }
    QWidget::mouseReleaseEvent(event);
}

void WheelSchematic::mouseMoveEvent(QMouseEvent *event)
{
    updateHover(event->position());
    QWidget::mouseMoveEvent(event);
}

void WheelSchematic::leaveEvent(QEvent *event)
{
    if (m_hovered != -1) {
        m_hovered = -1;
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

void WheelSchematic::updateHover(const QPointF &point)
{
    const int index = controlAt(point);
    if (index == m_hovered)
        return;

    m_hovered = index;

    // The hand means there is something here to click, so it goes only over a
    // control with settings. Unset rather than forced to an arrow, so the widget
    // falls back to whatever its parent uses.
    if (isClickable(index))
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();

    // A tooltip naming what the pointer has just left would be naming the wrong
    // thing. The next one comes up on its own once the pointer settles.
    QToolTip::hideText();
}

void WheelSchematic::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    if (!m_wheel)
        return;

    const QTransform toWidget = this->toWidget();
    const qreal scale = toWidget.m11();
    if (scale <= 0.0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setTransform(toWidget);

    // --- Paddles -------------------------------------------------------------
    // First, because they are behind the wheel. The body painted over them is
    // what leaves only the part in an opening showing, or, where the body is
    // see-through, what dims the part behind the rim.
    for (const WheelControl &control : m_wheel->controls) {
        if (isPaddle(control.kind))
            drawPaddle(painter, toWidget, control);
    }

    // --- Body ----------------------------------------------------------------
    // The outline is drawn wide because a grip painted over the body afterwards
    // covers the inner half of it.
    const bool seeThrough = m_wheel->seeThroughBody;

    painter.save();
    painter.setOpacity(seeThrough ? kSeeThroughFill : 1.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fillFor(m_wheel->bodyMaterial));
    painter.drawPath(m_wheel->body);
    painter.setOpacity(seeThrough ? kSeeThroughOutline : 1.0);
    painter.setPen(penMm(kOutline, kOutlineMm, scale));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(m_wheel->body);
    painter.restore();

    // --- Surfaces ------------------------------------------------------------
    for (const WheelSurface &surface : m_wheel->surfaces) {
        painter.save();
        if (surface.clipToBody)
            painter.setClipPath(m_wheel->body, Qt::IntersectClip);
        if (surface.onBody && seeThrough)
            painter.setOpacity(kSeeThroughOutline);

        if (surface.material == WheelMaterial::Stitch) {
            QPen thread(kStitch, qMax(0.55, 1.0 / scale));
            thread.setCapStyle(Qt::FlatCap);
            thread.setDashPattern({1.8, 1.4});
            painter.setPen(thread);
            painter.setBrush(Qt::NoBrush);
        } else if (surface.material == WheelMaterial::Seam) {
            painter.setPen(penMm(kSeam, kEdgeMm, scale));
            painter.setBrush(Qt::NoBrush);
        } else {
            // An edge where the surface has one of its own. One cut to the body
            // takes the body's, which is already there.
            if (surface.clipToBody)
                painter.setPen(Qt::NoPen);
            else
                painter.setPen(penMm(kPlateEdge, kFineMm, scale));
            painter.setBrush(fillFor(surface.material));
        }

        painter.drawPath(surface.path);
        painter.restore();
    }

    // --- Lettering -----------------------------------------------------------
    for (const WheelLabel &label : m_wheel->labels)
        drawLabel(painter, toWidget, label);

    // --- Controls on the face --------------------------------------------------
    // The square root is so a low brightness still reads as lit, the way a dim
    // LED does, rather than vanishing into a dark wheel. The picture is not a
    // measurement of the light.
    const qreal glow = std::sqrt(m_ledBrightness / 100.0);

    for (const WheelControl &control : m_wheel->controls) {
        switch (control.kind) {
        case WheelControlKind::Button:
            drawButton(painter, toWidget, control, controlColour(control), glow);
            break;
        case WheelControlKind::LitLabel:
            drawLitLabel(painter, toWidget, control, controlColour(control), glow);
            break;
        case WheelControlKind::RotaryEncoder:
            drawEncoder(painter, control, controlColour(control), glow, scale);
            break;
        case WheelControlKind::ThumbEncoder:
            drawRoller(painter, control, scale);
            break;
        case WheelControlKind::SevenWaySwitch:
            drawSevenWaySwitch(painter, control, scale);
            break;
        case WheelControlKind::RpmLights: {
            QList<QColor> colours;
            for (const WheelLed &led : control.lights)
                colours << lightColour(led.key, led.colour);
            drawRpmLights(painter, control, colours, glow, scale);
            break;
        }
        case WheelControlKind::ShiftPaddle:
        case WheelControlKind::ClutchPaddle:
            break;
        }
    }

    // --- Warnings --------------------------------------------------------------
    // Last, so nothing covers them, and in pixels rather than millimetres: an
    // icon is legible at one size, not at the size of the wheel.
    if (m_warnings.isEmpty())
        return;

    painter.setTransform(QTransform());
    const QIcon warning = style()->standardIcon(QStyle::SP_MessageBoxWarning, nullptr, this);
    for (const WheelControl &control : m_wheel->controls) {
        if (!m_warnings.contains(control.key))
            continue;

        // Over the control's upper right edge, where it sits beside the cap
        // rather than on its colour or legend.
        const QRectF bounds = toWidget.mapRect(control.shape.boundingRect());
        const qreal side = qBound(kMinWarningPx, bounds.width() * 0.7, kMaxWarningPx);
        const QPointF edge = bounds.center()
                             + QPointF(bounds.width(), -bounds.height()) * (0.5 * M_SQRT1_2);
        const QRectF where(edge.x() - side * 0.2, edge.y() - side * 0.8, side, side);
        warning.paint(&painter, where.toAlignedRect());
    }
}

} // namespace hidcenter::simagic_alpha_evo
