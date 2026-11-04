/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// Tunable space dust parameters (SDD 43 sections 5.1 and 5.2)
struct FSOLSpaceDustParams
{
    double DeadZoneMps = SOL::SpaceDust::DEAD_ZONE_MPS;                         // below this relative speed: no dust
    double FullOpacityMps = SOL::SpaceDust::FULL_OPACITY_MPS;                   // fully faded in at this speed
    double ApparentMaxMps = SOL::SpaceDust::APPARENT_MAX_MPS;                   // cap on the apparent speed
    double StreakStartMps = SOL::SpaceDust::STREAK_START_MPS;                   // streaks begin at this apparent speed
    double StreakTimeS = SOL::SpaceDust::STREAK_TIME_S;                         // streak length = apparent speed x this
    double StreakMaxCm = SOL::SpaceDust::STREAK_MAX_CM;                         // cap on the streak length
    double StreakMinAlpha = SOL::SpaceDust::STREAK_MIN_ALPHA;                   // alpha floor of a full-length streak
    double PointSizeCm = SOL::SpaceDust::POINT_SIZE_CM;                         // base point size
    double BoxSizeCm = SOL::SpaceDust::BOX_SIZE_CM;                             // edge of the wrapping box
    double RadialFadeStartFraction = SOL::SpaceDust::RADIAL_FADE_START_FRACTION;  // radial fade start / box half-size
    double NearFadeStartCm = SOL::SpaceDust::NEAR_FADE_START_CM;                // invisible closer than this
    double NearFadeEndCm = SOL::SpaceDust::NEAR_FADE_END_CM;                    // fully visible beyond this
    double SmoothingTauS = SOL::SpaceDust::SMOOTHING_TAU_S;                     // smoothing time constant
    double MaxTimeWarp = SOL::SpaceDust::MAX_TIME_WARP;                         // hidden above this time-warp factor
};

// Everything that decides whether the dust effect runs this frame (SDD 43 sections 4.3 and 7)
struct FSOLSpaceDustGate
{
    bool bHasShip = false;
    bool bSettingEnabled = false;   // The Settings "Space dust" option (SDD 34 section 9)
    bool bCvarEnabled = true;
    double TimeWarpFactor = 1.0;
    bool bJumpWarping = false;
    bool bMapOpen = false;
    bool bPaused = false;
};

namespace SOLSpaceDust
{
    // Returns the particle count for an effects quality level (0 Low, 1 Medium, 2 and above High)
    SOLTEST_API int32 ParticleCountForQuality(int32 effectsQuality);

    // Returns the apparent speed (m/s) of a relative speed: 0 in the dead zone, then saturating below ApparentMaxMps
    SOLTEST_API double ApparentSpeedMps(double relativeSpeedMps, const FSOLSpaceDustParams& params);

    // Returns the dust opacity [0, 1] for a relative speed: 0 inside the dead zone, ramping to 1 at FullOpacityMps
    SOLTEST_API double Opacity(double relativeSpeedMps, const FSOLSpaceDustParams& params);

    // Returns the apparent velocity (cm/s) of the dust for a relative velocity (m/s), its length below ApparentMaxMps
    SOLTEST_API FVector3d ApparentVelocityCmPerS(const FVector3d& relativeVelocityMps,
        const FSOLSpaceDustParams& params);

    // Returns the streak length (cm) for an apparent speed: the point size up to StreakStartMps, then growing by the
    // speed above it x StreakTimeS, capped at StreakMaxCm
    SOLTEST_API double StreakLengthCm(double apparentSpeedMps, const FSOLSpaceDustParams& params);

    // Returns the streak alpha: 1 for a point, falling to StreakMinAlpha at StreakMaxCm
    SOLTEST_API double StreakAlpha(double streakLengthCm, const FSOLSpaceDustParams& params);

    // Returns current moved toward target with an exponential time constant (frame-rate independent)
    SOLTEST_API double SmoothScalar(double current, double target, double dtS, double tauS);

    // Returns the vector current moved toward target with an exponential time constant (frame-rate independent)
    SOLTEST_API FVector3d SmoothVector(const FVector3d& current, const FVector3d& target, double dtS, double tauS);

    // Returns the box offset advanced by velocity x dt and wrapped into [0, boxCm) per axis
    SOLTEST_API FVector3d AdvanceOffsetCm(const FVector3d& offsetCm, const FVector3d& velocityCmPerS, double dtS,
        double boxCm);

    // Returns the offset wrapped into [0, boxCm) per axis
    SOLTEST_API FVector3d WrapOffsetCm(const FVector3d& offsetCm, double boxCm);

    // Returns a particle's camera-local position: anchor - offset wrapped into [-boxCm / 2, boxCm / 2) per axis
    SOLTEST_API FVector3f WrappedLocalPositionCm(const FVector3f& anchorCm, const FVector3f& offsetCm, float boxCm);

    // Returns the fade [0, 1] of a particle: radial fade toward the box edge times the near fade around the camera
    SOLTEST_API float DustFade(const FVector3f& localCm, const FVector3f& cameraLocalCm,
        const FSOLSpaceDustParams& params);

    // Returns whether the effect runs this frame: ship present, setting and cvar on, time warp within MaxTimeWarp, and
    // no jump warp, open map or pause
    SOLTEST_API bool ShouldRun(const FSOLSpaceDustGate& gate, const FSOLSpaceDustParams& params);
}
