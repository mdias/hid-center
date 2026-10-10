// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMetaType>
#include <QtGlobal>

#include <array>
#include <cstddef>

/// The steering wheel's own configuration, as it is sent through
/// the base: SET feature report 0x91, sub-commands 0 to 3.
///
/// The same report id as telemetry, told apart by byte 1. Telemetry is
/// sub-commands 4 and 5; these four carry a 240-byte block, sixty bytes at a time.
///
/// **What is confirmed on hardware is the LED brightness**, from captures of
/// it changing: one byte, a plain percentage, and the wheel dims every LED
/// with it. The LED colours were checked against the same captures; the rest of
/// the layout is inferred.
///
/// **Nothing here can be read back.** The base never answers with this block, so
/// the bytes this module does not own have to come from somewhere. They come
/// from a capture: see defaultWheelConfig() in AlphaEvoBlocks.cpp, where the
/// composer below is also asserted against those captures at compile time.
///
/// The block, counted from zero (byte 231 counting from one is offset 230 here):
///
/// | Offset | Report | What is there |
/// | --- | --- | --- |
/// | 0–39 | 0 | button mappings: per input of the wheel, the joystick button it presses, less one |
/// | 40–49 | 0 | RPM LED thresholds, one per pattern, pattern 9's first |
/// | 50–79 | 0, 1 | per LED slot: `colour << 4 \| trigger`, colour 1–8 or 9 for off, trigger 0 always lit or 1–5 on a game event |
/// | 80–229 | 1, 2, 3 | RPM LED patterns: per LED, per pattern, `colour << 4 \| flash << 3 \| trigger` |
/// | 230 | 3 | **LED brightness, percent** (confirmed) |
/// | 231 | 3 | **clutch mode**: 0 synced, 1 independent (tried on a wheel) |
/// | 232 | 3 | **clutch ratio, percent** (tried on a wheel) |
/// | 233 | 3 | RPM LED brightness |
/// | 234–239 | 3 | unused |
namespace hidcenter::simagic_alpha_evo {

inline constexpr quint8 kWheelConfigReportId = 0x91;

/// The whole block, and how it is split: sub-command N carries bytes
/// `N × 60` to `N × 60 + 59`.
inline constexpr int kWheelConfigSize = 240;
inline constexpr int kWheelConfigChunkSize = 60;
inline constexpr int kWheelConfigChunkCount = 4;

static_assert(kWheelConfigChunkSize * kWheelConfigChunkCount == kWheelConfigSize,
              "the chunks cover the block exactly");

/// Report id, sub-command, one chunk. Sixty-two bytes, as captured, rather than
/// the sixty-four every other report this module sends is padded to.
inline constexpr int kWheelConfigReportSize = 2 + kWheelConfigChunkSize;

inline constexpr int kWheelConfigLedBrightnessOffset = 230;
inline constexpr int kWheelLedBrightnessMax = 100;

/// How the two clutch paddles behave, as tried on a GTC with its dual-clutch
/// module by writing the byte and watching the joystick report.
///
/// **Independent** (1), which the captured block has: each paddle is an axis of
/// its own, the left on Slider and the right on Rz, each over its full range.
///
/// **Synced** (0): both paddles drive one axis, Slider, and Rz stays at 0.
/// Either paddle alone goes only as far as the ratio; both together go all the
/// way. That is a launch clutch: hold both, let one go, and the clutch sits at
/// the ratio, the bite point.
///
/// What other values do was not tried. The byte is written whole, as 0 or 1.
inline constexpr int kWheelConfigClutchModeOffset = 231;
inline constexpr quint8 kWheelClutchModeSynced = 0;
inline constexpr quint8 kWheelClutchModeIndependent = 1;

/// The ratio of synced paddles, a plain percentage: how far one paddle alone
/// takes the clutch. Set to 50 and to 25, either paddle alone stopped at that
/// share of the axis's range. It does nothing while the paddles are independent.
///
/// The wheel says it back: byte 38 of the joystick report, as received,
/// follows this byte as soon as it is sent. See kClutchRatioEchoByte.
inline constexpr int kWheelConfigClutchRatioOffset = 232;
inline constexpr int kWheelClutchRatioMax = 100;

/// The button mappings: one byte for each of the wheel's inputs, the first
/// forty bytes of the block. Byte N says which joystick button input N presses,
/// as that button's number less one. Which input is which control depends on the
/// wheel; here it is each control's `data-input` in the wheel's SVG.
inline constexpr int kWheelConfigMappingOffset = 0;
inline constexpr int kWheelConfigMappingCount = 40;

/// The highest joystick button a control is offered. One per mapping byte, so
/// a wheel that gave every input a button of its own would have room.
inline constexpr int kWheelButtonMax = kWheelConfigMappingCount;

/// The RPM lights: ten patterns of up to fifteen LEDs, and the RPM percentage
/// each is shown from.
///
/// The thresholds go out
/// reversed: offset 40 is entry 9. The step in the
/// telemetry is found by finding the highest entry the revs have reached, and entry N
/// gives step N, so entry N is pattern N's threshold as long as step N shows
/// pattern N, which the captured bar bears out. Pattern 0 is therefore the top
/// of the range, where a shift light is all lit, and the thresholds fall as the
/// pattern number rises.
///
/// The patterns are laid out LED first, ten bytes per LED.
inline constexpr int kWheelConfigRpmThresholdOffset = 40;
inline constexpr int kWheelConfigRpmPatternCount = 10;
inline constexpr int kWheelConfigRpmPatternOffset = 80;
inline constexpr int kWheelConfigRpmLedCount = 15;
inline constexpr int kWheelRpmThresholdMax = 100;

static_assert(kWheelConfigRpmPatternOffset
                      + kWheelConfigRpmLedCount * kWheelConfigRpmPatternCount
                  == kWheelConfigLedBrightnessOffset,
              "the RPM patterns end where the brightness is");

/// The LED slots: one byte each for the lit parts of the wheel, buttons first.
/// Which slot is which part depends on the wheel, and here it is each
/// control's `data-led` in the wheel's SVG.
inline constexpr int kWheelConfigLedOffset = 50;
inline constexpr int kWheelConfigLedCount = 30;

static_assert(kWheelConfigRpmThresholdOffset + kWheelConfigRpmPatternCount == kWheelConfigLedOffset
                  && kWheelConfigLedOffset + kWheelConfigLedCount == kWheelConfigRpmPatternOffset,
              "the thresholds, the LED slots and the RPM patterns follow one another");

/// The wheel's own colour numbers, as the presets match against
/// them: 1 red, 2 orange, 3 yellow, 4 green, 5 blue, 6 cyan, 7 purple, 8 white.
/// 9 is black, which is how an LED is turned off and how the RPM patterns
/// leave one dark.
inline constexpr int kWheelLedColourOff = 9;

/// The low nibble: what makes the LED light. 0 is the "static effect",
/// lit all the time; 1 and 2 light it while ABS and TC are active, as the
/// captures show. 3 to 5 are the pit limiter, DRS and a flag by their order,
/// and have not been seen working.
inline constexpr int kWheelLedTriggerStatic = 0;
inline constexpr int kWheelLedTriggerAbs = 1;
inline constexpr int kWheelLedTriggerTc = 2;
inline constexpr int kWheelLedTriggerPitLimiter = 3;
inline constexpr int kWheelLedTriggerDrs = 4;
inline constexpr int kWheelLedTriggerFlag = 5;

/// The 240 bytes, in the order they go out.
///
/// A plain array rather than a packed struct of named fields: most of it is
/// carried rather than understood, and a struct would suggest otherwise. The
/// fields this module does set get accessors.
struct WheelConfig
{
    std::array<quint8, kWheelConfigSize> bytes{};

    /// How bright the wheel's LEDs are, 0 to 100.
    constexpr int ledBrightness() const
    {
        return bytes[std::size_t(kWheelConfigLedBrightnessOffset)];
    }

    /// Clamped to 0–100. The captures go as high as 100 and never past it.
    constexpr void setLedBrightness(int percent)
    {
        const int clamped = percent < 0                          ? 0
                            : percent > kWheelLedBrightnessMax ? kWheelLedBrightnessMax
                                                               : percent;
        bytes[std::size_t(kWheelConfigLedBrightnessOffset)] = quint8(clamped);
    }

    /// Whether the clutch paddles are synced. See kWheelConfigClutchModeOffset.
    constexpr bool clutchSynced() const
    {
        return bytes[std::size_t(kWheelConfigClutchModeOffset)] == kWheelClutchModeSynced;
    }

    constexpr void setClutchSynced(bool synced)
    {
        bytes[std::size_t(kWheelConfigClutchModeOffset)] =
            synced ? kWheelClutchModeSynced : kWheelClutchModeIndependent;
    }

    /// The synced paddles' ratio, percent. See kWheelConfigClutchRatioOffset.
    constexpr int clutchRatio() const
    {
        return bytes[std::size_t(kWheelConfigClutchRatioOffset)];
    }

    /// Clamped to 0–100.
    constexpr void setClutchRatio(int percent)
    {
        const int clamped = percent < 0                       ? 0
                            : percent > kWheelClutchRatioMax ? kWheelClutchRatioMax
                                                             : percent;
        bytes[std::size_t(kWheelConfigClutchRatioOffset)] = quint8(clamped);
    }

    /// An LED slot's colour number, 0 to 15. See kWheelLedColourOff.
    constexpr int ledColour(int led) const
    {
        return isLedSlot(led) ? bytes[std::size_t(kWheelConfigLedOffset + led)] >> 4 : 0;
    }

    /// An LED slot's trigger, 0 to 15. See kWheelLedTriggerStatic.
    constexpr int ledTrigger(int led) const
    {
        return isLedSlot(led) ? bytes[std::size_t(kWheelConfigLedOffset + led)] & 0x0F : 0;
    }

    /// Both halves of a slot, each kept to its nibble. A slot that does not
    /// exist is left alone.
    constexpr void setLed(int led, int colour, int trigger)
    {
        if (isLedSlot(led)) {
            bytes[std::size_t(kWheelConfigLedOffset + led)] =
                quint8((colour & 0x0F) << 4 | (trigger & 0x0F));
        }
    }

    static constexpr bool isLedSlot(int led) { return led >= 0 && led < kWheelConfigLedCount; }

    /// The joystick button an input presses, 1 to 256, or 0 for an input that
    /// does not exist.
    constexpr int buttonMapping(int input) const
    {
        return isMappingInput(input) ? bytes[std::size_t(kWheelConfigMappingOffset + input)] + 1
                                     : 0;
    }

    /// Make an input press joystick button `button`, 1 to 256. An input that
    /// does not exist, or a button out of that range, is left alone.
    constexpr void setButtonMapping(int input, int button)
    {
        if (isMappingInput(input) && button >= 1 && button <= 256)
            bytes[std::size_t(kWheelConfigMappingOffset + input)] = quint8(button - 1);
    }

    static constexpr bool isMappingInput(int input)
    {
        return input >= 0 && input < kWheelConfigMappingCount;
    }

    /// The RPM percentage pattern `pattern` is shown from, 0 for a pattern
    /// that does not exist.
    constexpr int rpmThreshold(int pattern) const
    {
        return isRpmPattern(pattern) ? bytes[std::size_t(rpmThresholdOffset(pattern))] : 0;
    }

    /// Clamped to 0–100. A pattern that does not exist is left alone.
    constexpr void setRpmThreshold(int pattern, int percent)
    {
        const int clamped = percent < 0                        ? 0
                            : percent > kWheelRpmThresholdMax ? kWheelRpmThresholdMax
                                                              : percent;
        if (isRpmPattern(pattern))
            bytes[std::size_t(rpmThresholdOffset(pattern))] = quint8(clamped);
    }

    /// An LED of a pattern: its colour number, 0 to 15 (see
    /// kWheelLedColourOff), whether it flashes, and its trigger, 0 to 7, whose
    /// meaning here is not known. All 0 for an LED or pattern that does not
    /// exist.
    constexpr int rpmLedColour(int led, int pattern) const { return rpmLedByte(led, pattern) >> 4; }
    constexpr bool rpmLedFlashing(int led, int pattern) const
    {
        return (rpmLedByte(led, pattern) & 0x08) != 0;
    }
    constexpr int rpmLedTrigger(int led, int pattern) const { return rpmLedByte(led, pattern) & 0x07; }

    /// All three parts of an LED of a pattern, each kept to its bits. An LED
    /// or pattern that does not exist is left alone.
    constexpr void setRpmLed(int led, int pattern, int colour, bool flashing, int trigger)
    {
        if (isRpmLed(led) && isRpmPattern(pattern)) {
            bytes[std::size_t(rpmLedOffset(led, pattern))] =
                quint8((colour & 0x0F) << 4 | (flashing ? 0x08 : 0) | (trigger & 0x07));
        }
    }

    static constexpr bool isRpmPattern(int pattern)
    {
        return pattern >= 0 && pattern < kWheelConfigRpmPatternCount;
    }
    static constexpr bool isRpmLed(int led) { return led >= 0 && led < kWheelConfigRpmLedCount; }

    /// Where a threshold is: reversed, so pattern 9's is first.
    static constexpr int rpmThresholdOffset(int pattern)
    {
        return kWheelConfigRpmThresholdOffset + kWheelConfigRpmPatternCount - 1 - pattern;
    }
    static constexpr int rpmLedOffset(int led, int pattern)
    {
        return kWheelConfigRpmPatternOffset + led * kWheelConfigRpmPatternCount + pattern;
    }

    friend constexpr bool operator==(const WheelConfig &a, const WheelConfig &b)
    {
        return a.bytes == b.bytes;
    }

private:
    constexpr int rpmLedByte(int led, int pattern) const
    {
        return isRpmLed(led) && isRpmPattern(pattern) ? bytes[std::size_t(rpmLedOffset(led, pattern))]
                                                      : 0;
    }

public:
    friend constexpr bool operator!=(const WheelConfig &a, const WheelConfig &b)
    {
        return !(a == b);
    }
};

/// Which report carries a byte of the block.
///
/// What lets a change go out as the report holding it rather than as all four.
/// Sending all four every time is the safe way; sending fewer is an economy,
/// and has a condition: see kWheelConfigApplyChunk.
constexpr int wheelConfigChunkOf(int offset)
{
    return offset / kWheelConfigChunkSize;
}

/// The report the wheel acts on. Sub-command 3 alone sets the brightness, but
/// sub-command 0 alone changes nothing on a GTC: the colours in it only show
/// once a 3 follows. So the others are held until the last report arrives, and
/// a change anywhere else has to be followed by this one.
inline constexpr int kWheelConfigApplyChunk = kWheelConfigChunkCount - 1;

static_assert(wheelConfigChunkOf(kWheelConfigLedBrightnessOffset) == 3,
              "the brightness goes out in the last report");
static_assert(wheelConfigChunkOf(kWheelConfigClutchModeOffset) == kWheelConfigApplyChunk
                  && wheelConfigChunkOf(kWheelConfigClutchRatioOffset) == kWheelConfigApplyChunk,
              "the clutch settings go out in the last report, with the brightness");
static_assert(wheelConfigChunkOf(kWheelConfigLedOffset + 9) == 0,
              "a GTC's ten LED slots all go out in the first report");
static_assert(wheelConfigChunkOf(kWheelConfigMappingOffset + kWheelConfigMappingCount - 1) == 0,
              "every mapping goes out in the first report, with the buttons' colours");

/// One report: `0x91`, the sub-command, and that chunk of the block.
///
/// Constexpr so the captures can check it at compile time. What the I/O thread
/// sends is composeWheelConfigReport() in AlphaEvoBlocks.h, which wraps this.
constexpr std::array<quint8, kWheelConfigReportSize> composeWheelConfigChunk(
    const WheelConfig &config, int chunk)
{
    std::array<quint8, kWheelConfigReportSize> report{};
    report[0] = kWheelConfigReportId;
    report[1] = quint8(chunk);
    for (int i = 0; i < kWheelConfigChunkSize; ++i) {
        report[std::size_t(2 + i)] =
            config.bytes[std::size_t(chunk * kWheelConfigChunkSize + i)];
    }
    return report;
}

} // namespace hidcenter::simagic_alpha_evo

/// Crosses to the I/O thread through a queued invocation, like LedRingColours.
Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::WheelConfig)
