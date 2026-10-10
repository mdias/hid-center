// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <cstddef>
#include <cstdint>

/// rFactor 2 / Le Mans Ultimate telemetry, as it arrives on the wire.
///
/// TelemInfoV01 and the two structs it contains are ported from the Studio 397
/// plugin SDK header `InternalsPlugin.hpp`, which is what a game-side plugin is
/// handed in its UpdateTelemetry() callback. A sender forwards that struct to us
/// over UDP; this header is the receiving half of that agreement, so it must
/// match the SDK's layout byte for byte.
///
/// **This is a port, not a copy.** The sender is a Windows build and we are not,
/// and four things had to change for the layout to survive the crossing:
///
/// - `#pragma pack(4)` is kept verbatim. Without it every double after the first
///   field shifts.
/// - Every `long` became `qint32`. Windows is LLP64, where `long` is 4 bytes; on
///   Linux it is 8. Left alone, the very first field (`mID`) desynchronises
///   everything after it.
/// - `float_t` became `float`. It comes from <cmath> and is only conditionally
///   the same type.
/// - `bool` is kept, but on the assumption that it is one byte — true for MSVC
///   and GCC, and asserted below rather than hoped for.
///
/// The static_asserts at the end are the real guard. They are what make a
/// mistake in any of that a compile error instead of a wheelbase acting on a
/// number that came out of the wrong offset.
///
/// Only what we need is here: TelemQuat, the graphics and scoring structs and
/// everything else in the SDK header are left out, which is also what keeps
/// `windows.h` out of this file.

// rF2 and its plugins agree on structure packing explicitly, so we must too.
#pragma pack(push, 4)

namespace hidcenter::rf2 {

struct TelemVect3
{
    union
    {
        struct
        {
            double x, y, z;
        };

        double data[3];
    };

    void Set(const double a, const double b, const double c) { x = a; y = b; z = c; }

    // Allowed to reference as [0], [1], or [2], instead of .x, .y, or .z, respectively
    double &operator[](long i) { return (data[i]); }
    const double &operator[](long i) const { return (data[i]); }
};

struct TelemWheelV01
{
    double mSuspensionDeflection;  // meters
    double mRideHeight;            // meters
    double mSuspForce;             // pushrod load in Newtons
    double mBrakeTemp;             // Celsius
    double mBrakePressure;         // currently 0.0-1.0, depending on driver input and brake balance; will convert to true brake pressure (kPa) in future

    double mRotation;              // radians/sec
    double mLateralPatchVel;       // lateral velocity at contact patch
    double mLongitudinalPatchVel;  // longitudinal velocity at contact patch
    double mLateralGroundVel;      // lateral velocity at contact patch
    double mLongitudinalGroundVel; // longitudinal velocity at contact patch
    double mCamber;                // radians (positive is left for left-side wheels, right for right-side wheels)
    double mLateralForce;          // Newtons
    double mLongitudinalForce;     // Newtons
    double mTireLoad;              // Newtons

    double mGripFract;             // an approximation of what fraction of the contact patch is sliding
    double mPressure;              // kPa (tire pressure)
    double mTemperature[3];        // Kelvin (subtract 273.15 to get Celsius), left/center/right (not to be confused with inside/center/outside!)
    double mWear;                  // wear (0.0-1.0, fraction of maximum) ... this is not necessarily proportional with grip loss
    char mTerrainName[16];         // the material prefixes from the TDF file
    unsigned char mSurfaceType;    // 0=dry, 1=wet, 2=grass, 3=dirt, 4=gravel, 5=rumblestrip, 6=special
    bool mFlat;                    // whether tire is flat
    bool mDetached;                // whether wheel is detached
    unsigned char mStaticUndeflectedRadius; // tire radius in centimeters

    double mVerticalTireDeflection;// how much is tire deflected from its (speed-sensitive) radius
    double mWheelYLocation;        // wheel's y location relative to vehicle y location
    double mToe;                   // current toe angle w.r.t. the vehicle

    double mTireCarcassTemperature;       // rough average of temperature samples from carcass (Kelvin)
    double mTireInnerLayerTemperature[3]; // rough average of temperature samples from innermost layer of rubber (before carcass) (Kelvin)

    float mOptimalTemp;            // SDK: float_t
    unsigned char mCompoundIndex;
    unsigned char mCompoundType;
    unsigned char mExpansion[18];
};

/// Vehicle class, as LMU reports it.
enum class IP_VehicleClass : uint8_t {
    Hypercar = 0x00,
    LMP2_ELMS = 0x02,
    LMP2,
    LMP3,
    GTE,
    GT3,
    PaceCar = 0x08,
    Unknown = 0xFF
};

enum class IP_VehicleChampionship : uint8_t {
    WEC_2023 = 0x00, WEC_2024, WEC_2025, WEC_2026,
    ELMS_2025 = 0X10, ELMS_2026,
    Unknown = 0xFF
};

// Our world coordinate system is left-handed, with +y pointing up.
// The local vehicle coordinate system is as follows:
//   +x points out the left side of the car (from the driver's perspective)
//   +y points out the roof
//   +z points out the back of the car
// Rotations are as follows:
//   +x pitches up
//   +y yaws to the right
//   +z rolls to the right
// Note that ISO vehicle coordinates (+x forward, +y right, +z upward) are
// right-handed.  If you are using that system, be sure to negate any rotation
// or torque data because things rotate in the opposite direction.  In other
// words, a -z velocity in rFactor is a +x velocity in ISO, but a -z rotation
// in rFactor is a -x rotation in ISO!!!

struct TelemInfoV01
{
    // Time
    qint32 mID;                    // slot ID (note that it can be re-used in multiplayer after someone leaves)
    double mDeltaTime;             // time since last update (seconds)
    double mElapsedTime;           // game session time
    qint32 mLapNumber;             // current lap number
    double mLapStartET;            // time this lap was started
    char mVehicleName[64];         // current vehicle name
    char mTrackName[64];           // current track name

    // Position and derivatives
    TelemVect3 mPos;               // world position in meters
    TelemVect3 mLocalVel;          // velocity (meters/sec) in local vehicle coordinates
    TelemVect3 mLocalAccel;        // acceleration (meters/sec^2) in local vehicle coordinates

    // Orientation and derivatives
    TelemVect3 mOri[3];            // rows of orientation matrix (use TelemQuat conversions if desired), also converts local
                                   // vehicle vectors into world X, Y, or Z using dot product of rows 0, 1, or 2 respectively
    TelemVect3 mLocalRot;          // rotation (radians/sec) in local vehicle coordinates
    TelemVect3 mLocalRotAccel;     // rotational acceleration (radians/sec^2) in local vehicle coordinates

    // Vehicle status
    qint32 mGear;                  // -1=reverse, 0=neutral, 1+=forward gears
    double mEngineRPM;             // engine RPM
    double mEngineWaterTemp;       // Celsius
    double mEngineOilTemp;         // Celsius
    double mClutchRPM;             // clutch RPM

    // Driver input
    double mUnfilteredThrottle;    // ranges  0.0-1.0
    double mUnfilteredBrake;       // ranges  0.0-1.0
    double mUnfilteredSteering;    // ranges -1.0-1.0 (left to right)
    double mUnfilteredClutch;      // ranges  0.0-1.0

    // Filtered input (various adjustments for rev or speed limiting, TC, ABS?, speed sensitive steering, clutch work for semi-automatic shifting, etc.)
    double mFilteredThrottle;      // ranges  0.0-1.0
    double mFilteredBrake;         // ranges  0.0-1.0
    double mFilteredSteering;      // ranges -1.0-1.0 (left to right)
    double mFilteredClutch;        // ranges  0.0-1.0

    // Misc
    double mSteeringShaftTorque;   // torque around steering shaft (used to be mSteeringArmForce, but that is not necessarily accurate for feedback purposes)
    double mFront3rdDeflection;    // deflection at front 3rd spring
    double mRear3rdDeflection;     // deflection at rear 3rd spring

    // Aerodynamics
    double mFrontWingHeight;       // front wing height
    double mFrontRideHeight;       // front ride height
    double mRearRideHeight;        // rear ride height
    double mDrag;                  // drag
    double mFrontDownforce;        // front downforce
    double mRearDownforce;         // rear downforce

    // State/damage info
    double mFuel;                  // amount of fuel (liters)
    double mEngineMaxRPM;          // rev limit
    unsigned char mScheduledStops; // number of scheduled pitstops
    bool  mOverheating;            // whether overheating icon is shown
    bool  mDetached;               // whether any parts (besides wheels) have been detached
    bool  mHeadlights;             // whether headlights are on
    unsigned char mDentSeverity[8];// dent severity at 8 locations around the car (0=none, 1=some, 2=more)
    double mLastImpactET;          // time of last impact
    double mLastImpactMagnitude;   // magnitude of last impact
    TelemVect3 mLastImpactPos;     // location of last impact

    // Expanded
    double mEngineTorque;          // current engine torque (including additive torque) (used to be mEngineTq, but there's little reason to abbreviate it)
    qint32 mCurrentSector;         // the current sector (zero-based) with the pitlane stored in the sign bit (example: entering pits from third sector gives 0x80000002)
    unsigned char mSpeedLimiter;   // whether speed limiter is on
    unsigned char mMaxGears;       // maximum forward gears
    unsigned char mFrontTireCompoundIndex;   // index within brand
    unsigned char mRearTireCompoundIndex;    // index within brand
    double mFuelCapacity;          // capacity in liters
    unsigned char mFrontFlapActivated;       // whether front flap is activated
    unsigned char mRearFlapActivated;        // whether rear flap is activated
    unsigned char mRearFlapLegalStatus;      // 0=disallowed, 1=criteria detected but not allowed quite yet, 2=allowed
    unsigned char mIgnitionStarter;          // 0=off 1=ignition 2=ignition+starter

    char mFrontTireCompoundName[18];         // name of front tire compound
    char mRearTireCompoundName[18];          // name of rear tire compound

    unsigned char mSpeedLimiterAvailable;    // whether speed limiter is available
    unsigned char mAntiStallActivated;       // whether (hard) anti-stall is activated
    unsigned char mUnused[2];                //
    float mVisualSteeringWheelRange;         // the *visual* steering wheel range

    double mRearBrakeBias;                   // fraction of brakes on rear
    double mTurboBoostPressure;              // current turbo boost pressure if available
    float mPhysicsToGraphicsOffset[3];       // offset from static CG to graphical center
    float mPhysicalSteeringWheelRange;       // the *physical* steering wheel range

    // deltabest
    double mDeltaBest;

    double mBatteryChargeFraction; // Battery charge as fraction [0.0-1.0]

    // electric boost motor
    double mElectricBoostMotorTorque; // current torque of boost motor (can be negative when in regenerating mode)
    double mElectricBoostMotorRPM; // current rpm of boost motor
    double mElectricBoostMotorTemperature; // current temperature of boost motor
    double mElectricBoostWaterTemperature; // current water temperature of boost motor cooler if present (0 otherwise)
    unsigned char mElectricBoostMotorState; // 0=unavailable 1=inactive, 2=propulsion, 3=regeneration
    bool mLapInvalidated;
    bool mABSActive;
    bool mTCActive;
    bool mSpeedLimiterActive;
    uint8_t mWiperState;
    uint8_t mTC;
    uint8_t mTCMax;
    uint8_t mTCSlip;
    uint8_t mTCSlipMax;
    uint8_t mTCCut;
    uint8_t mTCCutMax;
    uint8_t mABS;
    uint8_t mABSMax;
    uint8_t mMotorMap;
    uint8_t mMotorMapMax;
    uint8_t mMigration;
    uint8_t mMigrationMax;
    uint8_t mFrontAntiSway;
    uint8_t mFrontAntiSwayMax;
    uint8_t mRearAntiSway;
    uint8_t mRearAntiSwayMax;
    uint8_t mLiftAndCoastProgress;
    uint8_t mTrackLimitsSteps; // Normalized track limits points (TrackLimitPoints * TrackLimitStepsPerPoint)
    float mRegen; //kW
    float mSoC;
    float mVirtualEnergy;
    float mTimeGapCarAhead;
    float mTimeGapCarBehind;
    float mTimeGapPlaceAhead;
    float mTimeGapPlaceBehind;
    char mVehicleModel[30];
    IP_VehicleClass mVehicleClass;
    IP_VehicleChampionship mVehicleChampionship;

    // Future use
    unsigned char mExpansion[20]; // for future use (note that the slot ID has been moved to mID above)

    // keeping this at the end of the structure to make it easier to replace in future versions
    TelemWheelV01 mWheel[4];       // wheel info (front left, front right, rear left, rear right)
};

/// The 16 bytes in front of the payload.
///
/// All zeroes today. It is here so the wire format has room to grow a magic
/// number and a version without every existing sender changing length — which is
/// the one thing that would be genuinely painful to change later. Inventing the
/// field layout now, with nothing to tell apart yet, would only be guessing at
/// what those fields will need to say.
struct Rf2PacketHeader
{
    quint8 reserved[16];
};

/// Exactly what one telemetry datagram must measure. Anything else is not ours.
inline constexpr int kRf2DatagramSize = int(sizeof(Rf2PacketHeader) + sizeof(TelemInfoV01));

/// Loopback only: the sender is expected on this machine, whether that is a
/// forwarder or the sim itself under Wine.
inline constexpr quint16 kRf2ListenPort = 56789;

// --- The layout, proven rather than assumed ------------------------------------
//
// Sizes first, since a mismatch there means the port is wrong in some way this
// file cannot guess at, and then the offset of every field actually read. Follow
// AlphaEvoProtocol.h: a layout mistake should be a compile error, never a device
// acting on a number that came from the wrong place.

static_assert(sizeof(bool) == 1, "The SDK's bools are one byte on the sending side.");
static_assert(Q_BYTE_ORDER == Q_LITTLE_ENDIAN,
              "Doubles cross the wire raw, so both ends must agree on byte order.");

static_assert(sizeof(TelemVect3) == 24);
static_assert(sizeof(TelemWheelV01) == 260);
static_assert(sizeof(TelemInfoV01) == 1888);

static_assert(offsetof(TelemInfoV01, mEngineRPM) == 356);
static_assert(offsetof(TelemInfoV01, mEngineMaxRPM) == 532);
static_assert(offsetof(TelemInfoV01, mABSActive) == 746);
static_assert(offsetof(TelemInfoV01, mTCActive) == 747);
static_assert(offsetof(TelemInfoV01, mWheel) == 848);

// Within one wheel. The same rule applies inside the nested struct: an offset
// here that is wrong by eight bytes reads a tyre temperature out of a pressure.
static_assert(offsetof(TelemWheelV01, mBrakeTemp) == 24);
static_assert(offsetof(TelemWheelV01, mTemperature) == 128);
static_assert(offsetof(TelemWheelV01, mOptimalTemp) == 236);

static_assert(sizeof(Rf2PacketHeader) == 16);
static_assert(kRf2DatagramSize == 1904);

} // namespace hidcenter::rf2

#pragma pack(pop)
