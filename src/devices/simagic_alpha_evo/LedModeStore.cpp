// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LedModeStore.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSettings>
#include <QStringList>

#include <optional>

// For kMaxRingColours only — see ringDefaultColours(). The class itself is a
// widget and has no business here.
#include "ui/LedRing.h"

#include "core/Logging.h"
#include "core/usb/UsbDeviceInfo.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

// --- Names on the wire of the settings file ---------------------------------
// Every one of these is load-bearing for a file already on disk. Renaming one
// discards whatever the user had under it.

constexpr QLatin1String kKeyVersion("version");
constexpr QLatin1String kKeyModes("modes");
constexpr QLatin1String kKeyColours("colours");
constexpr QLatin1String kKeyAnimation("animation");
constexpr QLatin1String kKeyPeriod("periodMs");

struct AnimationName
{
    LedAnimation animation;
    const char *name;
};

/// Animations by name rather than by number, for the reason the modes are: the
/// enumerators are free to be reordered, and a file written last month must
/// still mean what it said.
constexpr std::array<AnimationName, 4> kAnimationNames{{
    {LedAnimation::Static, "static"},
    {LedAnimation::Breathing, "breathing"},
    {LedAnimation::Rotating, "rotating"},
    {LedAnimation::Blinking, "blinking"},
}};

QString animationName(LedAnimation animation)
{
    for (const AnimationName &entry : kAnimationNames) {
        if (entry.animation == animation)
            return QString::fromLatin1(entry.name);
    }
    return QString::fromLatin1(kAnimationNames.front().name);
}

/// The animation a name means, or nothing when the name is not one we know —
/// which is what a file from a newer build looks like.
std::optional<LedAnimation> animationFromName(const QString &name)
{
    for (const AnimationName &entry : kAnimationNames) {
        if (name.compare(QLatin1String(entry.name), Qt::CaseInsensitive) == 0)
            return entry.animation;
    }
    return std::nullopt;
}

// --- Colours ----------------------------------------------------------------

QString colourName(const LedRgb &colour)
{
    return QColor(colour.r, colour.g, colour.b).name();  // "#rrggbb"
}

QJsonArray coloursToJson(const LedRingColours &colours)
{
    QJsonArray array;
    const int count = qMin(int(colours.count), kMaxLedStops);
    for (int i = 0; i < count; ++i)
        array.append(colourName(colours.stops[std::size_t(i)]));
    return array;
}

/// Read a colour list back. Anything unreadable is dropped rather than taken as
/// black, because a black stop is a thing a user might have meant and a typo is
/// not — and a list that ends up empty falls back to the default for that mode.
LedRingColours coloursFromJson(const QJsonArray &array, bool *lostSome)
{
    LedRingColours colours;
    for (const QJsonValue &value : array) {
        if (colours.count >= kMaxLedStops) {
            *lostSome = true;
            break;
        }
        const QColor colour(value.toString());
        if (!colour.isValid()) {
            *lostSome = true;
            continue;
        }
        colours.stops[std::size_t(colours.count)] =
            LedRgb{quint8(colour.red()), quint8(colour.green()), quint8(colour.blue())};
        ++colours.count;
    }
    return colours;
}

LedRingColours coloursOf(std::initializer_list<const char *> names)
{
    LedRingColours colours;
    for (const char *name : names) {
        if (colours.count >= kMaxLedStops)
            break;
        const QColor colour{QLatin1String(name)};
        colours.stops[std::size_t(colours.count)] =
            LedRgb{quint8(colour.red()), quint8(colour.green()), quint8(colour.blue())};
        ++colours.count;
    }
    return colours;
}

/// The first three colours a fresh LedRing shows.
///
/// The formula is LedRing's own — an even sweep of hue at 0.75 saturation and
/// 0.95 value, taking the first `kInitialRingColours` of `kMaxRingColours`. It is
/// copied rather than shared because the widget's palette is a view concern and
/// this is a model one; the two drifting apart would change nothing but the look
/// of a device nobody has configured yet.
LedRingColours ringDefaultColours()
{
    LedRingColours colours;
    for (int i = 0; i < 3; ++i) {
        const QColor colour = QColor::fromHsvF(qreal(i) / qreal(kMaxRingColours), 0.75, 0.95);
        colours.stops[std::size_t(colours.count)] =
            LedRgb{quint8(colour.red()), quint8(colour.green()), quint8(colour.blue())};
        ++colours.count;
    }
    return colours;
}

/// Where a settings file keeps one base's modes.
QString settingsPath(const QString &key)
{
    return QStringLiteral("devices/%1/wheelLed").arg(key);
}

} // namespace

LedModeSet defaultLedModes()
{
    LedModeSet set;

    set.at(LedMode::Idle).colours = ringDefaultColours();
    set.at(LedMode::Idle).animation = LedAnimation::Static;

    // Amber, because that is what a traction-control telltale is on nearly every
    // dashboard, breathing rather than blinking: traction control works in long
    // stretches out of a corner, and something that blinks for four seconds is
    // an alarm rather than an indication.
    set.at(LedMode::TractionControl).colours = coloursOf({"#ffb000"});
    set.at(LedMode::TractionControl).animation = LedAnimation::Breathing;
    set.at(LedMode::TractionControl).periodMs = 900;

    // Red and blinking fast. ABS engages in bursts of well under a second, so
    // this has to be legible in a glance that may be all the driver gets.
    set.at(LedMode::Abs).colours = coloursOf({"#ff2000"});
    set.at(LedMode::Abs).animation = LedAnimation::Blinking;
    set.at(LedMode::Abs).periodMs = 300;

    return set;
}

QJsonObject ledModesToJson(const LedModeSet &modes)
{
    QJsonObject byMode;
    for (const LedModeSpec &spec : ledModeSpecs()) {
        const LedModeSettings &mode = modes.at(spec.mode);
        QJsonObject entry;
        entry.insert(kKeyColours, coloursToJson(mode.colours));
        entry.insert(kKeyAnimation, animationName(mode.animation));
        entry.insert(kKeyPeriod, int(mode.periodMs));
        byMode.insert(QLatin1String(spec.key), entry);
    }

    QJsonObject json;
    json.insert(kKeyVersion, kLedModesFormatVersion);
    json.insert(kKeyModes, byMode);
    return json;
}

LedModeSet ledModesFromJson(const QJsonObject &json, QString *warning)
{
    if (warning)
        warning->clear();

    LedModeSet set = defaultLedModes();
    if (json.isEmpty())
        return set;

    const int version = json.value(kKeyVersion).toInt(0);
    if (version > kLedModesFormatVersion) {
        // Refused whole rather than read as far as it goes. A later format may
        // have moved what a field means, and defaults are an honest answer where
        // a half-read document is not.
        if (warning) {
            *warning = QStringLiteral("the saved LED modes are version %1, and this build reads "
                                      "up to version %2")
                           .arg(version)
                           .arg(kLedModesFormatVersion);
        }
        return set;
    }

    const QJsonObject byMode = json.value(kKeyModes).toObject();
    QStringList dropped;

    for (const LedModeSpec &spec : ledModeSpecs()) {
        const QString key = QLatin1String(spec.key);
        if (!byMode.contains(key))
            continue;

        const QJsonObject entry = byMode.value(key).toObject();
        LedModeSettings &mode = set.at(spec.mode);

        bool lostColours = false;
        const LedRingColours colours = coloursFromJson(entry.value(kKeyColours).toArray(),
                                                       &lostColours);
        // An empty list is not a ring, it is a mode with nothing to show, so the
        // default stands. LedRing takes the same view of an empty list.
        if (colours.count > 0)
            mode.colours = colours;
        if (lostColours || (colours.count == 0 && entry.contains(kKeyColours)))
            dropped.append(QStringLiteral("%1 colours").arg(key));

        if (entry.contains(kKeyAnimation)) {
            const auto animation = animationFromName(entry.value(kKeyAnimation).toString());
            if (animation)
                mode.animation = *animation;
            else
                dropped.append(QStringLiteral("%1 animation").arg(key));
        }

        if (entry.contains(kKeyPeriod)) {
            const int period = entry.value(kKeyPeriod).toInt(kLedDefaultPeriodMs);
            const int clamped = qBound(kLedMinPeriodMs, period, kLedMaxPeriodMs);
            mode.periodMs = quint16(clamped);
            if (clamped != period)
                dropped.append(QStringLiteral("%1 speed").arg(key));
        }
    }

    // Modes this build has never heard of are left alone rather than reported:
    // a file written by a newer build within the same format version is expected
    // to carry rows we do not know, and saying so every launch would be noise.

    if (warning && !dropped.isEmpty())
        *warning = QStringLiteral("ignored in the saved LED modes: %1").arg(dropped.join(
            QStringLiteral(", ")));

    return set;
}

QString ledSettingsKey(const UsbDeviceInfo &info)
{
    if (!info.serial.isEmpty()) {
        // A serial can hold anything the device felt like reporting, and this
        // becomes part of a QSettings path where a slash would make a group.
        QString serial = info.serial;
        serial.replace(QLatin1Char('/'), QLatin1Char('_'));
        serial.replace(QLatin1Char('\\'), QLatin1Char('_'));
        return serial;
    }
    return info.id.toString();
}

LedModeSet loadLedModes(const UsbDeviceInfo &info)
{
    QSettings settings;
    const QString text = settings.value(settingsPath(ledSettingsKey(info))).toString();
    if (text.isEmpty())
        return defaultLedModes();

    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(hidcenterDevice) << "Saved wheel LED modes could not be read:"
                                << parse.errorString();
        return defaultLedModes();
    }

    QString warning;
    const LedModeSet modes = ledModesFromJson(document.object(), &warning);
    if (!warning.isEmpty())
        qCWarning(hidcenterDevice).noquote() << warning;
    return modes;
}

void saveLedModes(const UsbDeviceInfo &info, const LedModeSet &modes)
{
    // One string rather than a tree of QSettings keys. It is what a preset will
    // carry, it survives a mode being added without leaving orphaned keys behind,
    // and it is readable in the ini file when something looks wrong.
    const QJsonDocument document(ledModesToJson(modes));
    QSettings settings;
    settings.setValue(settingsPath(ledSettingsKey(info)),
                      QString::fromUtf8(document.toJson(QJsonDocument::Compact)));
}

} // namespace hidcenter::simagic_alpha_evo
