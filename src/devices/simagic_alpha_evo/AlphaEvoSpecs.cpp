// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoSpecs.h"

#include <QCoreApplication>

namespace hidcenter::simagic_alpha_evo {

const QVector<SettingSpec> &settingSpecs()
{
    static const char *const kAngleLockStrengths[] = {QT_TR_NOOP("Soft"), QT_TR_NOOP("Normal"),
                                                      QT_TR_NOOP("Firm")};

    // Ranges are the driver's, from the clamps in simagic_attribute_settings*_store()
    // and the sm_sanitize_settings*_report() functions.
    static const QVector<SettingSpec> specs = {
        {Setting::FfStrength, SettingBlock::Settings1, QT_TR_NOOP("Force feedback strength"), -100, 100},
        {Setting::MechanicalCentering, SettingBlock::Settings1, QT_TR_NOOP("Mechanical centering"), 0, 100},
        {Setting::MechanicalDamper, SettingBlock::Settings1, QT_TR_NOOP("Mechanical damper"), 0, 100},
        {Setting::MechanicalFriction, SettingBlock::Settings1, QT_TR_NOOP("Mechanical friction"), 0, 100},
        {Setting::MechanicalInertia, SettingBlock::Settings2, QT_TR_NOOP("Mechanical inertia"), 0, 100},
        {Setting::CenterDamper, SettingBlock::Settings1, QT_TR_NOOP("Center damper"), 0, 100},

        {Setting::GameCentering, SettingBlock::Settings1, QT_TR_NOOP("Game centering"), 0, 200},
        {Setting::GameInertia, SettingBlock::Settings1, QT_TR_NOOP("Game inertia"), 0, 200},
        {Setting::GameDamper, SettingBlock::Settings1, QT_TR_NOOP("Game damper"), 0, 200},
        {Setting::GameFriction, SettingBlock::Settings1, QT_TR_NOOP("Game friction"), 0, 200},

        {Setting::MaxAngle, SettingBlock::Settings1, QT_TR_NOOP("Maximum rotation (°)"), 90, 2520},
        // The base clamps this to the maximum rotation above, not to 2520;
        // AlphaEvoSettings::effectiveMaximum() is what the UI actually uses, and
        // composeSettings2() clamps the value that is written.
        {Setting::AngleLock, SettingBlock::Settings2, QT_TR_NOOP("Angle lock (°)"), 90, 2520},
        {Setting::AngleLockStrength, SettingBlock::Settings2, QT_TR_NOOP("Angle lock strength"), 0,
         2, ControlKind::Choice, kAngleLockStrengths},
        {Setting::WheelRotationSpeed, SettingBlock::Settings1, QT_TR_NOOP("Wheel rotation speed"), 0, 100},

        {Setting::SteeringTorqueAssist, SettingBlock::Settings4, QT_TR_NOOP("Steering torque assist"), 0, 100},

        {Setting::DynamicPredictionLevel, SettingBlock::Settings4, QT_TR_NOOP("Dynamic prediction level"), 0, 10},
        {Setting::FeedbackDetail, SettingBlock::Settings2, QT_TR_NOOP("Feedback detail"), 0, 100},
        {Setting::FilterLevel, SettingBlock::Settings4, QT_TR_NOOP("Filter level"), 0, 20},
        {Setting::SlewRateControl, SettingBlock::Settings4, QT_TR_NOOP("Slew rate control"), 0, 100},

        {Setting::RingLight, SettingBlock::Settings3, QT_TR_NOOP("Ring light"), 0, 1,
         ControlKind::Toggle},
        {Setting::RingLightBrightness, SettingBlock::Settings3,
         QT_TR_NOOP("Ring light brightness"), 0, 100},
    };
    return specs;
}

const SettingSpec *specFor(Setting setting)
{
    for (const SettingSpec &spec : settingSpecs()) {
        if (spec.id == setting)
            return &spec;
    }
    return nullptr;
}

int clampToSpec(Setting setting, int value)
{
    const SettingSpec *spec = specFor(setting);
    return spec ? qBound(spec->minimum, value, spec->maximum) : value;
}

bool touchesBlock(const SettingValues &changes, SettingBlock block)
{
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const SettingSpec *spec = specFor(it.key());
        if (spec && spec->block == block)
            return true;
    }
    return false;
}

} // namespace hidcenter::simagic_alpha_evo
