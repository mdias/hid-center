// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WheelConfigController.h"

#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "AlphaEvoBlocks.h"
#include "AlphaEvoSettings.h"
#include "LedModeStore.h"
#include "WheelButtonColours.h"
#include "WheelLedTriggers.h"
#include "ui/WheelLayouts.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

/// The least time between two updates of the wheel's configuration. Not about
/// bandwidth: a GTC loses an update that comes too soon after the one before.
/// Switching a button between two colours with 150 ms here showed only the
/// first. So after an update everything waits this long, and whatever changed
/// meanwhile goes as one update with the latest values.
constexpr int kSendIntervalMs = 1000;

/// As in WheelLedController: how often the file is touched, not whether a change
/// survives, since the destructor saves too.
constexpr int kSaveDebounceMs = 400;

/// What a base the app has never set shows. The wheel's real brightness cannot
/// be read, so any value here is a choice rather than a fact, and full is the
/// one that cannot be mistaken for the LEDs being off.
constexpr int kDefaultLedBrightness = kWheelLedBrightnessMax;

/// Beside the ring's modes, under the same per-base key.
QString brightnessSettingsPath(const UsbDeviceInfo &info)
{
    return QStringLiteral("devices/%1/wheelLedBrightness").arg(ledSettingsKey(info));
}

/// Beside the brightness: like it, they are the same bytes on every wheel.
QString clutchSyncedSettingsPath(const UsbDeviceInfo &info)
{
    return QStringLiteral("devices/%1/wheelClutchSynced").arg(ledSettingsKey(info));
}

QString clutchRatioSettingsPath(const UsbDeviceInfo &info)
{
    return QStringLiteral("devices/%1/wheelClutchRatio").arg(ledSettingsKey(info));
}

/// One group per wheel, by its wheel id, holding a colour name per control key.
QString colourSettingsGroup(const UsbDeviceInfo &info, int wheelId)
{
    return QStringLiteral("devices/%1/wheelLedColours/%2").arg(ledSettingsKey(info)).arg(wheelId);
}

/// The same for triggers, holding a trigger's settings name per control key.
QString triggerSettingsGroup(const UsbDeviceInfo &info, int wheelId)
{
    return QStringLiteral("devices/%1/wheelLedTriggers/%2").arg(ledSettingsKey(info)).arg(wheelId);
}

/// The same for mappings, holding a joystick button number per control action:
/// see mappingSettingsKey().
QString mappingSettingsGroup(const UsbDeviceInfo &info, int wheelId)
{
    return QStringLiteral("devices/%1/wheelButtonMappings/%2")
        .arg(ledSettingsKey(info))
        .arg(wheelId);
}

/// What one action of a control's joystick button is kept under, in
/// mappingSettingsGroup(): the control's key for a button's one action, and the
/// key then the action's name for any other, "switch_7way_left/up".
QString mappingSettingsKey(const WheelControl &control, int action)
{
    const QString name = wheelControlActions(control.kind).value(action);
    return name.isEmpty() ? control.key : control.key + u'/' + name;
}

/// The same for the RPM lights, holding a pattern per row, "1" the lowest.
QString rpmLightsSettingsGroup(const UsbDeviceInfo &info, int wheelId)
{
    return QStringLiteral("devices/%1/wheelRpmLights/%2").arg(ledSettingsKey(info)).arg(wheelId);
}

const WheelControl *findControl(const WheelLayout &layout, const QString &key)
{
    for (const WheelControl &control : layout.controls) {
        if (control.key == key)
            return &control;
    }
    return nullptr;
}

/// How many RPM LEDs a layout draws, as far as the block has room for them.
int rpmLedCount(const WheelLayout &layout)
{
    for (const WheelControl &control : layout.controls) {
        if (control.kind == WheelControlKind::RpmLights)
            return int(qMin<qsizetype>(control.lights.size(), kWheelConfigRpmLedCount));
    }
    return 0;
}

/// The block's pattern for a row of RpmLights, which lists the lowest first.
int patternOfRow(qsizetype row)
{
    return kWheelConfigRpmPatternCount - 1 - int(row);
}

RpmLights readRpmLights(const WheelConfig &config, int ledCount)
{
    RpmLights lights;
    for (int row = 0; row < kWheelConfigRpmPatternCount; ++row) {
        const int pattern = patternOfRow(row);
        RpmPattern entry;
        entry.percent = config.rpmThreshold(pattern);
        for (int led = 0; led < ledCount; ++led)
            entry.leds << RpmLed{config.rpmLedColour(led, pattern), config.rpmLedFlashing(led, pattern)};
        lights << entry;
    }
    return lights;
}

/// Each LED's trigger stays as the block has it: what it means in a pattern is
/// not known, and the default block's are all the same.
void writeRpmLights(WheelConfig &config, const RpmLights &lights, int ledCount)
{
    int floor = 0;
    const qsizetype rows = qMin<qsizetype>(lights.size(), kWheelConfigRpmPatternCount);
    for (qsizetype row = 0; row < rows; ++row) {
        const int pattern = patternOfRow(row);
        const RpmPattern &entry = lights.at(row);
        floor = qBound(floor, entry.percent, kWheelRpmThresholdMax);
        config.setRpmThreshold(pattern, floor);

        const qsizetype leds = qMin<qsizetype>(entry.leds.size(), ledCount);
        for (qsizetype led = 0; led < leds; ++led) {
            const RpmLed &light = entry.leds.at(led);
            config.setRpmLed(int(led), pattern, light.colour, light.flashing,
                             config.rpmLedTrigger(int(led), pattern));
        }
    }
}

} // namespace

WheelConfigController::WheelConfigController(AlphaEvoSettings *settings, UsbDeviceInfo info,
                                             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_info(std::move(info))
    , m_config(defaultWheelConfig())
{
    QSettings store;
    bool valid = false;
    const int saved = store.value(brightnessSettingsPath(m_info)).toInt(&valid);
    m_config.setLedBrightness(valid ? saved : kDefaultLedBrightness);

    // Never set, they are what the block has: independent, at 34 %.
    const QVariant synced = store.value(clutchSyncedSettingsPath(m_info));
    if (synced.isValid())
        m_config.setClutchSynced(synced.toBool());
    const int ratio = store.value(clutchRatioSettingsPath(m_info)).toInt(&valid);
    if (valid)
        m_config.setClutchRatio(ratio);

    for (const WheelLayout &layout : wheelLayouts()) {
        store.beginGroup(colourSettingsGroup(m_info, layout.wheelId));
        QHash<QString, QColor> &colours = m_colours[layout.wheelId];
        for (const QString &key : store.childKeys()) {
            if (const std::optional<QColor> colour =
                    colourFromSettingsName(store.value(key).toString())) {
                colours.insert(key, *colour);
            }
        }
        store.endGroup();
    }

    // Every lit part of the wheel the block belongs to shows what the user
    // picked, or else what the block already had, lit by what the user picked.
    // Failing that a button is lit all the time rather than by whatever the block
    // gave it, since one it left to a game event would be dark in a picture that
    // draws every button lit. An ABS or TC window keeps the block's trigger: the
    // game event is what the window is there to show.
    if (const WheelLayout *gtc = wheelLayoutForId(kDefaultWheelConfigWheelId)) {
        QHash<QString, int> triggers;
        store.beginGroup(triggerSettingsGroup(m_info, gtc->wheelId));
        for (const QString &key : store.childKeys()) {
            if (const std::optional<int> trigger =
                    triggerFromSettingsName(store.value(key).toString())) {
                triggers.insert(key, *trigger);
            }
        }
        store.endGroup();

        const QHash<QString, QColor> &picked = m_colours.value(gtc->wheelId);
        for (const WheelControl &control : gtc->controls) {
            if (control.ledSlot < 0)
                continue;
            const auto it = picked.constFind(control.key);
            const int wheelColour = it != picked.cend() ? wheelColourOf(*it) : -1;
            const int stockTrigger = control.kind == WheelControlKind::Button
                                         ? kWheelLedTriggerStatic
                                         : m_config.ledTrigger(control.ledSlot);
            m_config.setLed(control.ledSlot,
                            wheelColour > 0 ? wheelColour : m_config.ledColour(control.ledSlot),
                            triggers.value(control.key, stockTrigger));
        }

        // A mapping never set is what the block has, the default.
        store.beginGroup(mappingSettingsGroup(m_info, gtc->wheelId));
        for (const WheelControl &control : gtc->controls) {
            for (qsizetype action = 0; action < control.inputs.size(); ++action) {
                bool valid = false;
                const int button =
                    store.value(mappingSettingsKey(control, int(action))).toInt(&valid);
                const int input = control.inputs.at(action);
                if (input >= 0 && valid && button >= 1 && button <= kWheelButtonMax)
                    m_config.setButtonMapping(input, button);
            }
        }
        store.endGroup();

        // So are RPM lights never set. A row that cannot be read is left as
        // the block has it rather than dropping the rest.
        const int leds = rpmLedCount(*gtc);
        RpmLights lights = readRpmLights(m_config, leds);
        store.beginGroup(rpmLightsSettingsGroup(m_info, gtc->wheelId));
        for (qsizetype row = 0; row < lights.size(); ++row) {
            const std::optional<RpmPattern> saved = rpmPatternFromSettingsValue(
                store.value(QString::number(row + 1)).toString());
            if (saved)
                lights[row] = *saved;
        }
        store.endGroup();
        writeRpmLights(m_config, lights, leds);
    }

    m_throttle = new QTimer(this);
    m_throttle->setSingleShot(true);
    m_throttle->setInterval(kSendIntervalMs);
    // The interval is a minimum the wheel needs, and a coarse timer may fire up
    // to 5 % early: 475 ms, as measured on the wire.
    m_throttle->setTimerType(Qt::PreciseTimer);
    connect(m_throttle, &QTimer::timeout, this, &WheelConfigController::flushPending);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, &WheelConfigController::save);

    if (m_settings) {
        connect(m_settings, &AlphaEvoSettings::baseStatusRefreshed, this,
                &WheelConfigController::refreshAttachedWheel);
    }

    // The brightness once at start-up, so the wheel matches what the app will
    // show. Held rather than sent: the colours may join it once
    // refreshAttachedWheel() knows what is on the base, and the two have to go
    // as one update, since a second so soon after the first would be lost.
    m_pendingChunks = quint8(1u << wheelConfigChunkOf(kWheelConfigLedBrightnessOffset));
}

WheelConfigController::~WheelConfigController()
{
    if (m_saveTimer && m_saveTimer->isActive())
        save();
}

void WheelConfigController::setLedBrightness(int percent)
{
    const WheelConfig before = m_config;
    m_config.setLedBrightness(percent);
    if (m_config == before)
        return;

    scheduleChanges(before);
    m_saveTimer->start();
}

void WheelConfigController::setClutchSynced(bool synced)
{
    const WheelConfig before = m_config;
    m_config.setClutchSynced(synced);
    if (m_config == before)
        return;

    scheduleChanges(before);
    m_saveTimer->start();
}

void WheelConfigController::setClutchRatio(int percent)
{
    const WheelConfig before = m_config;
    m_config.setClutchRatio(percent);
    if (m_config == before)
        return;

    scheduleChanges(before);
    m_saveTimer->start();
}

QHash<QString, QColor> WheelConfigController::controlColours(WheelModel model) const
{
    const WheelLayout &layout = wheelLayout(model);
    QHash<QString, QColor> colours = m_colours.value(layout.wheelId);
    if (layout.wheelId != kDefaultWheelConfigWheelId)
        return colours;

    for (const WheelControl &control : layout.controls) {
        if (control.ledSlot >= 0)
            colours.insert(control.key, colourOfWheel(m_config.ledColour(control.ledSlot)));

        // The RPM LEDs as pattern 0 has them, the top of the range, which is
        // the one pattern that shows the whole bar.
        if (control.kind == WheelControlKind::RpmLights) {
            const qsizetype leds = qMin<qsizetype>(control.lights.size(), kWheelConfigRpmLedCount);
            for (qsizetype led = 0; led < leds; ++led) {
                colours.insert(control.lights.at(led).key,
                               colourOfWheel(m_config.rpmLedColour(int(led), 0)));
            }
        }
    }
    return colours;
}

void WheelConfigController::setControlColour(WheelModel model, const QString &key,
                                             const QColor &colour)
{
    const WheelLayout &layout = wheelLayout(model);
    QHash<QString, QColor> &picked = m_colours[layout.wheelId];
    const auto it = picked.constFind(key);
    if (it == picked.cend() || *it != colour) {
        picked.insert(key, colour);
        m_coloursChanged = true;
        m_saveTimer->start();
    }

    const WheelControl *control = blockControl(model, key);
    const int wheelColour = wheelColourOf(colour);
    if (!control || control->ledSlot < 0 || wheelColour < 0)
        return;

    const WheelConfig before = m_config;
    m_config.setLed(control->ledSlot, wheelColour, m_config.ledTrigger(control->ledSlot));
    if (sendsWheelSettings())
        scheduleChanges(before);
}

std::optional<int> WheelConfigController::controlTrigger(WheelModel model,
                                                         const QString &key) const
{
    const WheelControl *control = blockControl(model, key);
    if (!control || control->ledSlot < 0)
        return std::nullopt;
    return m_config.ledTrigger(control->ledSlot);
}

void WheelConfigController::setControlTrigger(WheelModel model, const QString &key, int trigger)
{
    const WheelControl *control = blockControl(model, key);
    if (!control || control->ledSlot < 0)
        return;

    const WheelConfig before = m_config;
    m_config.setLed(control->ledSlot, m_config.ledColour(control->ledSlot), trigger);
    if (m_config == before)
        return;

    m_triggersChanged = true;
    m_saveTimer->start();
    if (sendsWheelSettings())
        scheduleChanges(before);
}

std::optional<int> WheelConfigController::controlButton(WheelModel model, const QString &key,
                                                        int action) const
{
    const int input = controlInput(model, key, action);
    if (input < 0)
        return std::nullopt;
    return m_config.buttonMapping(input);
}

void WheelConfigController::setControlButton(WheelModel model, const QString &key, int action,
                                             int button)
{
    const int input = controlInput(model, key, action);
    if (input < 0 || button < 1 || button > kWheelButtonMax)
        return;

    const WheelConfig before = m_config;
    m_config.setButtonMapping(input, button);
    if (m_config == before)
        return;

    m_mappingsChanged = true;
    m_saveTimer->start();
    if (sendsWheelSettings())
        scheduleChanges(before);
}

WheelConfigController::ButtonSharing WheelConfigController::buttonSharing(WheelModel model,
                                                                          const QString &key,
                                                                          int action,
                                                                          int button) const
{
    ButtonSharing sharing;
    const int asked = controlInput(model, key, action);
    if (asked < 0)
        return sharing;

    const WheelLayout &layout = wheelLayout(model);
    for (int input = 0; input < kWheelConfigMappingCount; ++input) {
        if (input == asked || !isGtcButtonInput(input) || m_config.buttonMapping(input) != button)
            continue;

        bool placed = false;
        for (const WheelControl &control : layout.controls) {
            const qsizetype owner = control.inputs.indexOf(input);
            if (owner >= 0) {
                sharing.controls << SharedAction{control.key, int(owner)};
                placed = true;
                break;
            }
        }
        if (!placed)
            ++sharing.otherInputs;
    }
    return sharing;
}

std::optional<RpmLights> WheelConfigController::rpmLights(WheelModel model) const
{
    const WheelLayout &layout = wheelLayout(model);
    const int leds = rpmLedCount(layout);
    if (layout.wheelId != kDefaultWheelConfigWheelId || leds == 0)
        return std::nullopt;
    return readRpmLights(m_config, leds);
}

void WheelConfigController::setRpmLights(WheelModel model, const RpmLights &lights)
{
    const WheelLayout &layout = wheelLayout(model);
    const int leds = rpmLedCount(layout);
    if (layout.wheelId != kDefaultWheelConfigWheelId || leds == 0)
        return;

    const WheelConfig before = m_config;
    writeRpmLights(m_config, lights, leds);
    if (m_config == before)
        return;

    m_rpmLightsChanged = true;
    m_saveTimer->start();
    if (sendsWheelSettings())
        scheduleChanges(before);
}

bool WheelConfigController::sendsWheelSettings() const
{
    if (!m_settings)
        return false;
    return m_settings->isOffline() || m_attachedWheelId == kDefaultWheelConfigWheelId;
}

const WheelControl *WheelConfigController::blockControl(WheelModel model, const QString &key) const
{
    const WheelLayout &layout = wheelLayout(model);
    return layout.wheelId == kDefaultWheelConfigWheelId ? findControl(layout, key) : nullptr;
}

int WheelConfigController::controlInput(WheelModel model, const QString &key, int action) const
{
    const WheelControl *control = blockControl(model, key);
    return control ? control->inputs.value(action, -1) : -1;
}

void WheelConfigController::refreshAttachedWheel()
{
    const int wheelId =
        m_settings && m_settings->hasBaseStatus() ? int(m_settings->baseStatus().wheelId) : 0;
    if (m_attachedWheelKnown && wheelId == m_attachedWheelId)
        return;

    m_attachedWheelKnown = true;
    m_attachedWheelId = wheelId;
    Q_EMIT attachedWheelChanged();

    // Everything a GTC is set to at start-up, with the brightness held since
    // construction, and again whenever a GTC comes back onto the base, which may
    // have been set by something else in the meantime: the buttons and the RPM
    // thresholds in report 0, the RPM patterns in 1 and 2, so the whole block.
    // The first call sends the brightness even when there is nothing else to
    // send.
    if (sendsWheelSettings()) {
        for (int chunk = 0; chunk < kWheelConfigApplyChunk; ++chunk)
            m_pendingChunks |= quint8(1u << chunk);
    }
    if (!m_throttle->isActive())
        flushPending();
}

void WheelConfigController::scheduleChanges(const WheelConfig &before)
{
    for (int offset = 0; offset < kWheelConfigSize; ++offset) {
        if (before.bytes[std::size_t(offset)] != m_config.bytes[std::size_t(offset)])
            m_pendingChunks |= quint8(1u << wheelConfigChunkOf(offset));
    }
    if (!m_throttle->isActive())
        flushPending();
}

void WheelConfigController::flushPending()
{
    const quint8 pending = std::exchange(m_pendingChunks, 0);
    if (!pending)
        return;

    push(pending);
    // From every update, so the next never follows sooner, however it comes.
    m_throttle->start();
}

void WheelConfigController::push(quint8 chunks)
{
    if (!m_settings)
        return;

    // In order, and always ending on the report that applies: the wheel takes
    // the others in without acting on them. Sent once, however many of the
    // others went before it.
    for (int chunk = 0; chunk < kWheelConfigApplyChunk; ++chunk) {
        if (chunks & (1u << chunk))
            m_settings->writeWheelConfigAsync(m_config, chunk);
    }
    m_settings->writeWheelConfigAsync(m_config, kWheelConfigApplyChunk);
}

void WheelConfigController::save()
{
    m_saveTimer->stop();

    QSettings store;
    store.setValue(brightnessSettingsPath(m_info), m_config.ledBrightness());
    store.setValue(clutchSyncedSettingsPath(m_info), m_config.clutchSynced());
    store.setValue(clutchRatioSettingsPath(m_info), m_config.clutchRatio());

    if (std::exchange(m_coloursChanged, false)) {
        for (auto wheel = m_colours.cbegin(); wheel != m_colours.cend(); ++wheel) {
            store.beginGroup(colourSettingsGroup(m_info, wheel.key()));
            for (auto colour = wheel->cbegin(); colour != wheel->cend(); ++colour)
                store.setValue(colour.key(), colourSettingsName(colour.value()));
            store.endGroup();
        }
    }

    // Every button's, not only the ones changed: the block is where they are
    // kept, and one never changed saves as the always-lit it was given anyway.
    const WheelLayout *gtc = wheelLayoutForId(kDefaultWheelConfigWheelId);
    if (std::exchange(m_triggersChanged, false) && gtc) {
        store.beginGroup(triggerSettingsGroup(m_info, gtc->wheelId));
        for (const WheelControl &control : gtc->controls) {
            if (control.ledSlot < 0)
                continue;
            const QString name = triggerSettingsName(m_config.ledTrigger(control.ledSlot));
            if (!name.isEmpty())
                store.setValue(control.key, name);
        }
        store.endGroup();
    }

    if (std::exchange(m_mappingsChanged, false) && gtc) {
        store.beginGroup(mappingSettingsGroup(m_info, gtc->wheelId));
        for (const WheelControl &control : gtc->controls) {
            for (qsizetype action = 0; action < control.inputs.size(); ++action) {
                const int input = control.inputs.at(action);
                if (input >= 0) {
                    store.setValue(mappingSettingsKey(control, int(action)),
                                   m_config.buttonMapping(input));
                }
            }
        }
        store.endGroup();
    }

    if (std::exchange(m_rpmLightsChanged, false) && gtc) {
        const RpmLights lights = readRpmLights(m_config, rpmLedCount(*gtc));
        store.beginGroup(rpmLightsSettingsGroup(m_info, gtc->wheelId));
        for (qsizetype row = 0; row < lights.size(); ++row)
            store.setValue(QString::number(row + 1), rpmPatternSettingsValue(lights.at(row)));
        store.endGroup();
    }
}

} // namespace hidcenter::simagic_alpha_evo
