// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelLayouts.h"

#include <QCoreApplication>
#include <QFile>
#include <QLineF>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <span>

#include "AlphaEvoWheelConfig.h"
#include "core/DeveloperMode.h"
#include "core/Logging.h"
#include "svg/SvgGeometry.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

using svg::SvgElement;

// Every wheel is a file under wheels/, built into this module as a resource.
// What the file may contain, and what each part of it becomes, is in README.md,
// "Layouts are SVG files".
struct WheelFile
{
    WheelModel model;
    const char *resource;
};

constexpr WheelFile kWheelFiles[] = {
    {WheelModel::Gtc, ":/simagic_alpha_evo/ui/wheels/gtc.svg"},
    {WheelModel::GtNeo, ":/simagic_alpha_evo/ui/wheels/gt_neo.svg"},
};

/// One thing a control can be made to do that presses a joystick button of its
/// own: see wheelControlActions().
struct ControlAction
{
    /// As `data-input` and the saved settings name it.
    const char *key;

    /// As the user reads it, in English.
    const char *name;
};

constexpr ControlAction kButtonActions[] = {
    {"", ""},
};

constexpr ControlAction kKnobActions[] = {
    {"clockwise", QT_TRANSLATE_NOOP("WheelLayouts", "Clockwise")},
    {"anticlockwise", QT_TRANSLATE_NOOP("WheelLayouts", "Anticlockwise")},
};

constexpr ControlAction kRollerActions[] = {
    {"up", QT_TRANSLATE_NOOP("WheelLayouts", "Up")},
    {"down", QT_TRANSLATE_NOOP("WheelLayouts", "Down")},
};

// The directions first, as a hat lists them, then the push and the turns.
constexpr ControlAction kSevenWayActions[] = {
    {"up", QT_TRANSLATE_NOOP("WheelLayouts", "Up")},
    {"down", QT_TRANSLATE_NOOP("WheelLayouts", "Down")},
    {"left", QT_TRANSLATE_NOOP("WheelLayouts", "Left")},
    {"right", QT_TRANSLATE_NOOP("WheelLayouts", "Right")},
    {"push", QT_TRANSLATE_NOOP("WheelLayouts", "Push")},
    {"clockwise", QT_TRANSLATE_NOOP("WheelLayouts", "Clockwise")},
    {"anticlockwise", QT_TRANSLATE_NOOP("WheelLayouts", "Anticlockwise")},
};

std::span<const ControlAction> actionsOf(WheelControlKind kind)
{
    switch (kind) {
    case WheelControlKind::Button:
        return kButtonActions;
    case WheelControlKind::RotaryEncoder:
        return kKnobActions;
    case WheelControlKind::ThumbEncoder:
        return kRollerActions;
    case WheelControlKind::SevenWaySwitch:
        return kSevenWayActions;
    case WheelControlKind::LitLabel:
    case WheelControlKind::ShiftPaddle:
    case WheelControlKind::ClutchPaddle:
    case WheelControlKind::RpmLights:
        break;
    }
    return {};
}

std::optional<WheelControlKind> kindOf(const SvgElement &element)
{
    static const QHash<QString, WheelControlKind> kinds{
        {QStringLiteral("button"), WheelControlKind::Button},
        {QStringLiteral("lit-label"), WheelControlKind::LitLabel},
        {QStringLiteral("rotary-encoder"), WheelControlKind::RotaryEncoder},
        {QStringLiteral("thumb-encoder"), WheelControlKind::ThumbEncoder},
        {QStringLiteral("seven-way-switch"), WheelControlKind::SevenWaySwitch},
        {QStringLiteral("shift-paddle"), WheelControlKind::ShiftPaddle},
        {QStringLiteral("clutch-paddle"), WheelControlKind::ClutchPaddle},
        {QStringLiteral("rpm-lights"), WheelControlKind::RpmLights},
    };
    for (const QString &name : element.classes) {
        const auto it = kinds.constFind(name);
        if (it != kinds.cend())
            return *it;
    }
    return std::nullopt;
}

std::optional<WheelMaterial> materialOf(const SvgElement &element)
{
    static const QHash<QString, WheelMaterial> materials{
        {QStringLiteral("grip"), WheelMaterial::Grip},
        {QStringLiteral("carbon"), WheelMaterial::Carbon},
        {QStringLiteral("plate"), WheelMaterial::Plate},
        {QStringLiteral("housing"), WheelMaterial::Housing},
        {QStringLiteral("band"), WheelMaterial::Band},
        {QStringLiteral("seam"), WheelMaterial::Seam},
        {QStringLiteral("stitch"), WheelMaterial::Stitch},
    };
    for (const QString &name : element.classes) {
        const auto it = materials.constFind(name);
        if (it != materials.cend())
            return *it;
    }
    return std::nullopt;
}

CapGlyph glyphOf(const SvgElement &element)
{
    if (element.hasClass(u"glyph-arrow-left"))
        return CapGlyph::ArrowLeft;
    if (element.hasClass(u"glyph-arrow-right"))
        return CapGlyph::ArrowRight;
    if (element.hasClass(u"glyph-power"))
        return CapGlyph::Power;
    return CapGlyph::None;
}

bool isText(const SvgElement &element)
{
    return element.tag == u"text";
}

/// Every outline in an element and the elements inside it, in document
/// coordinates, text left out.
QPainterPath outlineOf(const SvgElement &element)
{
    QPainterPath all;
    if (isText(element))
        return all;
    if (!element.path.isEmpty())
        all.addPath(element.outline());
    for (const SvgElement &child : element.children)
        all.addPath(outlineOf(child));
    return all;
}

/// The first element, in document order, that `matches`, this one included.
const SvgElement *findFirst(const SvgElement &element,
                            const std::function<bool(const SvgElement &)> &matches)
{
    if (matches(element))
        return &element;
    for (const SvgElement &child : element.children) {
        if (const SvgElement *found = findFirst(child, matches))
            return found;
    }
    return nullptr;
}

/// The first `<rect>` in an element or inside it, which is what a label and a
/// roller are placed by. Null when it has none.
const SvgElement *findRect(const SvgElement &element)
{
    return findFirst(element, [](const SvgElement &e) { return e.tag == u"rect"; });
}

void forEach(const SvgElement &element, const std::function<void(const SvgElement &)> &visit)
{
    visit(element);
    for (const SvgElement &child : element.children)
        forEach(child, visit);
}

/// An angle turned to point the same way as far as a rectangle is concerned,
/// and into (-90, 90], so text set at it never reads upside down.
qreal uprightDeg(qreal degrees)
{
    degrees = std::fmod(degrees, 180.0);
    if (degrees > 90.0)
        degrees -= 180.0;
    else if (degrees <= -90.0)
        degrees += 180.0;
    return degrees;
}

/// Where a `<rect>` is on the page, however it got there: its middle, its sides
/// as they measure there, and how far clockwise its width is turned. A mirrored
/// rectangle is the same rectangle turned, which is all this reports.
struct Placement
{
    QPointF centre;
    QSizeF size;
    qreal rotationDeg = 0.0;
};

Placement placementOf(const SvgElement &rect)
{
    const QPointF middle = rect.rect.center();
    const QLineF across = rect.transform.map(QLineF(middle, middle + QPointF(1.0, 0.0)));
    const QLineF down = rect.transform.map(QLineF(middle, middle + QPointF(0.0, 1.0)));

    Placement placement;
    placement.centre = rect.transform.map(middle);
    placement.size = QSizeF(rect.rect.width() * across.length(),
                            rect.rect.height() * down.length());
    placement.rotationDeg = qRadiansToDegrees(std::atan2(across.dy(), across.dx()));
    return placement;
}

/// Turns one wheel's SVG into its layout.
class LayoutReader
{
public:
    LayoutReader(QString file, WheelLayout &wheel)
        : m_file(std::move(file))
        , m_wheel(wheel)
    {
    }

    void read(const SvgElement &root)
    {
        m_wheel.title = root.title;
        if (m_wheel.title.isEmpty())
            warn(QStringLiteral("the <svg> has no <title> to name the wheel by"));

        bool ok = false;
        m_wheel.wheelId = root.attributes.value(QStringLiteral("data-wheel-id")).toInt(&ok);
        if (!ok)
            warn(QStringLiteral("the <svg> has no data-wheel-id"));

        m_wheel.seeThroughBody = root.hasClass(u"see-through");

        for (const SvgElement &child : root.children)
            visit(child);

        m_wheel.body.setFillRule(Qt::OddEvenFill);
        if (m_wheel.body.isEmpty())
            warn(QStringLiteral("nothing has class \"body\", so the wheel has no outline"));
    }

private:
    void visit(const SvgElement &element)
    {
        if (const std::optional<WheelControlKind> kind = kindOf(element)) {
            readControl(element, *kind);
        } else if (element.hasClass(u"label")) {
            readLabel(element);
        } else if (element.hasClass(u"body")) {
            readBody(element);
        } else if (const std::optional<WheelMaterial> material = materialOf(element)) {
            readSurface(element, *material);
        } else {
            if (!element.path.isEmpty() || isText(element))
                warn(QStringLiteral("%1 is not drawn: it has no class the Wheel tab knows")
                         .arg(describe(element)));
            for (const SvgElement &child : element.children)
                visit(child);
        }
    }

    void readBody(const SvgElement &element)
    {
        m_wheel.body.addPath(outlineOf(element));
        if (const std::optional<WheelMaterial> material = materialOf(element))
            m_wheel.bodyMaterial = *material;
        else
            warn(QStringLiteral("%1 is the body but has no material class").arg(describe(element)));
    }

    void readSurface(const SvgElement &element, WheelMaterial material)
    {
        WheelSurface surface;
        surface.path = outlineOf(element);
        surface.material = material;
        QString clip = element.property(QStringLiteral("clip-path"));
        clip.remove(u' ');
        surface.clipToBody = clip == QLatin1String("url(#body-clip)");
        surface.onBody = element.hasClass(u"on-body");
        m_wheel.surfaces.append(surface);
    }

    void readLabel(const SvgElement &element)
    {
        const SvgElement *box = findRect(element);
        const SvgElement *text = findFirst(element, isText);
        if (!box || !text) {
            warn(QStringLiteral("%1 is a label, which needs a <rect> and a <text>")
                     .arg(describe(element)));
            return;
        }

        const Placement placement = placementOf(*box);
        WheelLabel label;
        label.centre = placement.centre;
        label.size = placement.size;
        label.rotationDeg = uprightDeg(placement.rotationDeg);
        label.text = text->text;
        m_wheel.labels.append(label);
    }

    void readControl(const SvgElement &element, WheelControlKind kind)
    {
        WheelControl control;
        control.kind = kind;
        control.key = element.id;
        control.name = element.title;
        control.optional = element.hasClass(u"optional");
        control.glyph = glyphOf(element);

        if (control.key.isEmpty())
            warn(QStringLiteral("%1 has no id, so nothing can be saved for it")
                     .arg(describe(element)));
        else if (!claimKey(control.key))
            warn(QStringLiteral("more than one control has the id \"%1\"").arg(control.key));
        if (control.name.isEmpty()) {
            warn(QStringLiteral("%1 has no <title> to name it by").arg(describe(element)));
            control.name = control.key;
        }

        // The glyph is drawn instead of any text, which is only there so the
        // editor shows something like it.
        const SvgElement *text = findFirst(element, isText);
        if (text && control.glyph == CapGlyph::None)
            control.legend = text->text;

        switch (kind) {
        case WheelControlKind::RpmLights:
            readRpmLights(element, control);
            break;

        case WheelControlKind::ThumbEncoder:
            readRoller(element, control);
            break;

        case WheelControlKind::LitLabel:
            readWindow(element, control);
            break;

        case WheelControlKind::ShiftPaddle:
        case WheelControlKind::ClutchPaddle:
            control.shape = outlineOf(element);
            control.centre = text ? text->transform.map(text->textPosition)
                                  : control.shape.boundingRect().center();
            break;

        case WheelControlKind::Button:
        case WheelControlKind::RotaryEncoder:
        case WheelControlKind::SevenWaySwitch:
            control.shape = outlineOf(element);
            control.centre = control.shape.boundingRect().center();
            control.size = control.shape.boundingRect().size();
            break;
        }

        if (kind != WheelControlKind::RpmLights)
            control.colour = stockColour(element);
        if (control.shape.isEmpty())
            warn(QStringLiteral("%1 has no shape").arg(describe(element)));

        control.ledSlot = readIndex(element, QStringLiteral("data-led"), kWheelConfigLedCount,
                                    m_ledSlots);
        control.inputs = readInputs(element, kind);

        m_wheel.controls.append(control);
    }

    /// A lit label: the window it shows through, whose text readControl() has
    /// already made its legend.
    void readWindow(const SvgElement &element, WheelControl &control)
    {
        control.shape = outlineOf(element);

        const SvgElement *rect = findRect(element);
        if (!rect) {
            warn(QStringLiteral("%1 is a lit label, which needs a <rect>").arg(describe(element)));
            control.centre = control.shape.boundingRect().center();
            control.size = control.shape.boundingRect().size();
            return;
        }

        const Placement placement = placementOf(*rect);
        control.centre = placement.centre;
        control.size = placement.size;
        // Upright, so the word on it never reads upside down.
        control.rotationDeg = uprightDeg(placement.rotationDeg);
    }

    void readRoller(const SvgElement &element, WheelControl &control)
    {
        control.shape = outlineOf(element);

        const SvgElement *rect = findRect(element);
        if (!rect) {
            warn(QStringLiteral("%1 is a thumb encoder, which needs a <rect>").arg(describe(element)));
            control.centre = control.shape.boundingRect().center();
            control.size = control.shape.boundingRect().size();
        } else {
            const Placement placement = placementOf(*rect);
            control.centre = placement.centre;
            control.size = placement.size;
            control.rotationDeg = placement.rotationDeg;
        }

        // The ribs are drawn across the width, and on a roller they cross its
        // long side, so the width is made the long side whichever way the
        // rectangle was drawn.
        if (control.size.height() > control.size.width()) {
            control.size.transpose();
            control.rotationDeg += 90.0;
        }
        control.rotationDeg = uprightDeg(control.rotationDeg);
    }

    void readRpmLights(const SvgElement &element, WheelControl &control)
    {
        forEach(element, [&](const SvgElement &part) {
            if (part.hasClass(u"recess")) {
                control.shape.addPath(outlineOf(part));
            } else if (part.hasClass(u"led")) {
                WheelLed led;
                led.key = part.id;
                led.rect = outlineOf(part).boundingRect();
                led.colour = part.fill;
                if (led.key.isEmpty())
                    warn(QStringLiteral("an LED in \"%1\" has no id").arg(control.key));
                else if (!claimKey(led.key))
                    warn(QStringLiteral("more than one control or LED has the id \"%1\"")
                             .arg(led.key));
                if (!led.colour.isValid())
                    warn(QStringLiteral("LED \"%1\" has no fill colour").arg(led.key));
                control.lights.append(led);
            }
        });
        control.centre = control.shape.boundingRect().center();
        control.size = control.shape.boundingRect().size();
    }

    /// A numbered attribute such as `data-led`, saying which of `count` things
    /// of the wheel the control is: from 0 to `count` - 1, and no other
    /// control's, which `taken` keeps. -1 when it is missing or fails either.
    /// Only the control's own element is read, and a `<use>` has its own.
    int readIndex(const SvgElement &element, const QString &attribute, int count, QSet<int> &taken)
    {
        const auto it = element.attributes.constFind(attribute);
        if (it == element.attributes.cend())
            return -1;
        return claimIndex(element, attribute, it.value(), count, taken);
    }

    /// One number of a numbered attribute, as readIndex() takes it.
    int claimIndex(const SvgElement &element, const QString &attribute, const QString &text,
                   int count, QSet<int> &taken)
    {
        bool ok = false;
        const int index = text.trimmed().toInt(&ok);
        if (!ok || index < 0 || index >= count) {
            warn(QStringLiteral("%1 has %2 \"%3\", which is not a number from 0 to %4")
                     .arg(describe(element), attribute, text)
                     .arg(count - 1));
            return -1;
        }
        if (taken.contains(index)) {
            warn(QStringLiteral("%1 has %2 %3, which another control already has")
                     .arg(describe(element), attribute)
                     .arg(index));
            return -1;
        }
        taken.insert(index);
        return index;
    }

    /// `data-input`: the wheel input of each of the control's actions, as
    /// WheelControl::inputs holds them. A button's one action has no name, so it
    /// is a bare number, "6". Any other kind names each of its actions,
    /// "clockwise:37 anticlockwise:36", in any order. Every number is checked as
    /// readIndex() checks one.
    QList<int> readInputs(const SvgElement &element, WheelControlKind kind)
    {
        const std::span<const ControlAction> actions = actionsOf(kind);
        QList<int> inputs(qsizetype(actions.size()), -1);

        const QString attribute = QStringLiteral("data-input");
        const auto it = element.attributes.constFind(attribute);
        if (it == element.attributes.cend())
            return inputs;

        if (actions.empty()) {
            warn(QStringLiteral("%1 has %2, but nothing on its kind of control presses a button")
                     .arg(describe(element), attribute));
            return inputs;
        }
        if (actions.size() == 1 && *actions.front().key == '\0') {
            inputs[0] = claimIndex(element, attribute, it.value(), kWheelConfigMappingCount,
                                   m_inputs);
            return inputs;
        }

        QSet<qsizetype> named;
        const QStringList pairs = it.value().simplified().split(u' ', Qt::SkipEmptyParts);
        for (const QString &pair : pairs) {
            const qsizetype colon = pair.indexOf(u':');
            const QString name = pair.left(colon);
            const auto action = std::find_if(actions.begin(), actions.end(),
                                             [&name](const ControlAction &a) {
                                                 return name == QLatin1String(a.key);
                                             });
            if (colon < 0 || action == actions.end()) {
                QStringList names;
                for (const ControlAction &a : actions)
                    names << QString::fromLatin1(a.key);
                warn(QStringLiteral("%1 has \"%2\" in %3, which is not one of %4 then a colon "
                                    "and a number")
                         .arg(describe(element), pair, attribute,
                              names.join(QStringLiteral(", "))));
                continue;
            }

            const qsizetype index = action - actions.begin();
            if (named.contains(index)) {
                warn(QStringLiteral("%1 names %2 more than once in %3")
                         .arg(describe(element), name, attribute));
                continue;
            }
            named.insert(index);
            inputs[index] = claimIndex(element, attribute, pair.mid(colon + 1),
                                       kWheelConfigMappingCount, m_inputs);
        }

        for (qsizetype index = 0; index < qsizetype(actions.size()); ++index) {
            if (!named.contains(index)) {
                warn(QStringLiteral("%1 has no %2 in %3")
                         .arg(describe(element), QLatin1String(actions[std::size_t(index)].key),
                              attribute));
            }
        }
        return inputs;
    }

    /// The fill set on the control, or failing that on the first shape in it.
    static QColor stockColour(const SvgElement &element)
    {
        if (element.fill.isValid())
            return element.fill;
        const SvgElement *shape = findFirst(element, [](const SvgElement &e) {
            return !e.path.isEmpty() && e.fill.isValid();
        });
        return shape ? shape->fill : QColor();
    }

    bool claimKey(const QString &key)
    {
        if (m_keys.contains(key))
            return false;
        m_keys.insert(key);
        return true;
    }

    static QString describe(const SvgElement &element)
    {
        return element.id.isEmpty() ? QStringLiteral("a <%1>").arg(element.tag)
                                    : QStringLiteral("<%1 id=\"%2\">").arg(element.tag, element.id);
    }

    void warn(const QString &message) const
    {
        qCWarning(hidcenterDevice).noquote() << QStringLiteral("%1: %2").arg(m_file, message);
    }

    QString m_file;
    WheelLayout &m_wheel;
    QSet<QString> m_keys;
    QSet<int> m_ledSlots;
    QSet<int> m_inputs;
};

WheelLayout loadWheel(const WheelFile &file)
{
    WheelLayout wheel;
    wheel.model = file.model;

    const QString path = QString::fromLatin1(file.resource);
    QFile device(path);
    if (!device.open(QIODevice::ReadOnly)) {
        qCWarning(hidcenterDevice).noquote()
            << QStringLiteral("%1: could not be opened: %2").arg(path, device.errorString());
        return wheel;
    }

    const svg::SvgDocument document = svg::readSvg(device);
    if (!document.error.isEmpty()) {
        qCWarning(hidcenterDevice).noquote()
            << QStringLiteral("%1: could not be read: %2").arg(path, document.error);
        return wheel;
    }
    for (const QString &warning : document.warnings)
        qCWarning(hidcenterDevice).noquote() << QStringLiteral("%1: %2").arg(path, warning);

    LayoutReader(path, wheel).read(document.root);
    return wheel;
}

} // namespace

QRectF WheelLayout::bounds() const
{
    QRectF all = body.boundingRect();
    for (const WheelControl &control : controls)
        all |= control.shape.boundingRect();
    return all;
}

const QList<WheelLayout> &wheelLayouts()
{
    static const QList<WheelLayout> layouts = [] {
        QList<WheelLayout> list;
        for (const WheelFile &file : kWheelFiles)
            list.append(loadWheel(file));
        return list;
    }();
    return layouts;
}

const WheelLayout &wheelLayout(WheelModel model)
{
    const QList<WheelLayout> &layouts = wheelLayouts();
    for (const WheelLayout &layout : layouts) {
        if (layout.model == model)
            return layout;
    }
    Q_ASSERT_X(false, "wheelLayout", "a WheelModel with no entry in kWheelFiles");
    return layouts.first();
}

const WheelLayout *wheelLayoutForId(int wheelId)
{
    for (const WheelLayout &layout : wheelLayouts()) {
        if (layout.wheelId == wheelId)
            return &layout;
    }
    return nullptr;
}

bool wheelLayoutReleased(const WheelLayout &layout)
{
    // The GT Neo's page is not finished.
    if (layout.model == WheelModel::GtNeo)
        return developerMode();
    return true;
}

QString wheelControlName(const WheelControl &control)
{
    // The names come from the SVGs, where lupdate cannot find them, but a
    // translation that carries them under this context still applies.
    return QCoreApplication::translate("WheelLayouts", control.name.toUtf8().constData());
}

QStringList wheelControlActions(WheelControlKind kind)
{
    QStringList keys;
    for (const ControlAction &action : actionsOf(kind))
        keys << QString::fromLatin1(action.key);
    return keys;
}

QString wheelControlActionName(WheelControlKind kind, int action)
{
    const std::span<const ControlAction> actions = actionsOf(kind);
    if (action < 0 || std::size_t(action) >= actions.size() || !*actions[std::size_t(action)].name)
        return {};
    return QCoreApplication::translate("WheelLayouts", actions[std::size_t(action)].name);
}

bool isPaddle(WheelControlKind kind)
{
    return kind == WheelControlKind::ShiftPaddle || kind == WheelControlKind::ClutchPaddle;
}

} // namespace hidcenter::simagic_alpha_evo
