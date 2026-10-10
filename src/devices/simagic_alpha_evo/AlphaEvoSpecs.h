// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QMetaType>
#include <QVector>

namespace hidcenter::simagic_alpha_evo {

/// The settings this module exposes. Each belongs to exactly one write block.
enum class Setting {
    FfStrength,
    MechanicalCentering,
    MechanicalDamper,
    MechanicalFriction,
    MechanicalInertia,
    CenterDamper,
    GameCentering,
    GameInertia,
    GameDamper,
    GameFriction,
    MaxAngle,
    AngleLock,
    AngleLockStrength,
    WheelRotationSpeed,
    SteeringTorqueAssist,
    DynamicPredictionLevel,
    FeedbackDetail,
    FilterLevel,
    SlewRateControl,
    RingLight,
    RingLightBrightness,
};

/// A batch of edits, keyed by setting. Named rather than spelled out because it
/// crosses to the I/O thread through a queued signal and so must be a metatype.
using SettingValues = QHash<Setting, int>;

/// Which SET-feature block carries a setting. Changing a setting rewrites its
/// whole block, so this is also the unit in which writes are batched.
enum class SettingBlock {
    Settings1,
    Settings2,
    Settings3,
    Settings4,
};

/// How a setting should be presented. Most are a plain range; a couple are not,
/// and a 0-2 slider with no labels does not say which end is which.
enum class ControlKind {
    Slider,
    Choice,  ///< One of `choices`, indexed from `minimum`.
    Toggle,  ///< 0 or 1.
};

/// Everything the UI needs to build a control. Ranges come from the clamps in
/// the kernel driver's sysfs store and sanitize functions.
struct SettingSpec
{
    Setting id;
    SettingBlock block;
    const char *label;
    int minimum;
    int maximum;
    ControlKind kind = ControlKind::Slider;

    /// Choice only: maximum - minimum + 1 entries, in value order.
    const char *const *choices = nullptr;
};

/// The table that drives both the UI and the protocol. Adding a setting is one
/// row here plus one case in the block composers.
const QVector<SettingSpec> &settingSpecs();
const SettingSpec *specFor(Setting setting);

/// Hold a value inside its spec's range. A setting with no spec is passed
/// through untouched — there is nothing to clamp it against.
int clampToSpec(Setting setting, int value);

/// Whether any of these changes lands in the given block, and so whether that
/// block has to be composed and written at all.
bool touchesBlock(const SettingValues &changes, SettingBlock block);

} // namespace hidcenter::simagic_alpha_evo

Q_DECLARE_METATYPE(hidcenter::simagic_alpha_evo::SettingValues)
