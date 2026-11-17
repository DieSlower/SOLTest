/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneTrafficMath.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficSubsystem.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLLaneTunnelLabel.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #50 Part 50c HUD merge readout (SDD 50 section 4.8, decisions D7, D8, D18, contract case 13 and
// the label's last line), written against that contract before the production code existed (part 50c, test first).
// Universe ecliptic metres and sim seconds, double; doubles compared to 1e-9 relative unless a case says otherwise.
//
// API contract (from the test author; names the implementer follows):
//
// Traffic/SOLLaneTrafficMath.h (pure, no world):
//   enum class ESOLLaneMergeVerdict : uint8 { Matched, TooFast, TooSlow };
//   enum class ESOLLaneMergeBand : uint8 { Green, Amber, Red };
//   struct FSOLLaneMergeRating { bool bValid; double Delta; ESOLLaneMergeVerdict Verdict; ESOLLaneMergeBand Band; };
//   struct FSOLLanePoint { double AlongM; double LegArcM; double FootFraction; double Rho; double RightOffsetM;
//       double UpOffsetM; bool bInLeg; bool bInHalo; bool bValid; };
//   struct FSOLLaneMergeLane { FSOLLaneProfile Profile; FSOLLaneFrame Frame; FVector3d AVelocityMps;
//       FVector3d BVelocityMps; };
//   struct FSOLLaneMergeReadout { bool bShown; int32 Lane = INDEX_NONE; int32 DirectionSign; double Rho;
//       double TrafficSpeedMps; double PlayerSpeedMps; double Delta; ESOLLaneMergeVerdict Verdict;
//       ESOLLaneMergeBand Band; bool bWarpNote; FVector3d FrameVelocityMps; };
//   SOLLaneTraffic::RateMerge(trafficSpeedMps, playerSpeedMps) -> FSOLLaneMergeRating: delta = (v_pl - v_t) / v_t
//       computed in that order; |delta| <= NPC_LANE_MERGE_MATCH_FRACTION Matched / Green, else TooFast (delta > 0) or
//       TooSlow, Amber while |delta| <= NPC_LANE_MERGE_AMBER_FRACTION, else Red (a boundary belongs to the better
//       class); invalid for a traffic speed <= 0 or a non-finite input
//   SOLLaneTraffic::MergeDirectionSign(alongVelocityMps, alongForward, rightOffsetM) -> +1 (outbound A -> B) or -1:
//       the sign of the along-lane relative velocity when |v| >= NPC_LANE_MERGE_DIRECTION_MIN_MPS (NaN counts as
//       below), else the sign of the unit forward's along component when |f| > 0.1 (NaN as 0), else the player's half
//       (right offset >= 0, or NaN, = outbound)
//   SOLLaneTraffic::LanePointOf(profile, frame, pointM) -> FSOLLanePoint: AlongM = (p - A) . Axis, LegArcM = AlongM -
//       EndArcAM, FootFraction = AlongM / |B - A| clamped to [0, 1], RightOffsetM / UpOffsetM the lateral components on
//       Right / Up, Rho = lateral distance / WallRadiusM, bInLeg = 0 <= LegArcM <= CurrentLegM, bInHalo = bInLeg and
//       Rho <= HaloOuterRadiusM / WallRadiusM (= NPC_LANE_HALO_OUTER_WALL_RADII); bValid false (flags false) for an
//       invalid frame, a non-drivable profile or a non-finite point
//   SOLLaneTraffic::TrafficSpeedMps(profile, frame, legArcM, rho, directionSign) -> v_t = MeanSpeedMps(u, rho, l0) x
//       MetricMapSlope(u, l0, l), u = MetricMapInverseM(x, l0, l) with x = legArcM (clamped to [0, l]) for
//       directionSign > 0 and l - legArcM otherwise; 0 for an invalid frame, a non-drivable profile or a NaN input
//   SOLLaneTraffic::MergeReadout(lanes, playerM, playerVelocityMps, playerForward, timeWarpScale) ->
//       FSOLLaneMergeReadout: the lanes whose halo holds the player (LanePointOf bInHalo), the smallest Rho (the lower
//       index on a tie); none, or a non-finite position or velocity -> hidden (bShown false, Lane INDEX_NONE); frame
//       velocity lerp(V_A, V_B, FootFraction); direction from MergeDirectionSign((v - v_frame) . Axis, the normalised
//       forward . Axis (a zero or NaN forward as 0), RightOffsetM); TrafficSpeedMps at the player's leg arc and Rho;
//       PlayerSpeedMps = (v - v_frame) . (sign x Axis) (crossing motion counts as too slow); RateMerge of the two;
//       bWarpNote = timeWarpScale > 1 (NaN false); the speeds never depend on the warp (sim seconds, D7)
//
// UI/SOLLaneTunnelLabel.h (pure):
//   struct FSOLLaneMergeKey (operator==): equal exactly when FormatMergeReadoutLine's text would read the same
//   enum class ESOLLaneMergeWhere : uint8 { Hidden, Label, Fallback };
//   struct FSOLLaneMergeSpot { ESOLLaneMergeWhere Where = Hidden; int32 LabelIndex = INDEX_NONE; };
//   SOLLaneTunnelLabel::MergeVerdictText(verdict) -> SOLMenuText::LANE_TRAFFIC_MATCHED / _TOO_FAST / _TOO_SLOW
//   SOLLaneTunnelLabel::FormatMergeReadoutLine(trafficSpeedMps, playerSpeedMps, verdict, bWarpNote, outLine):
//       LANE_TRAFFIC_LABEL, AppendSpeedMps(traffic), LANE_TRAFFIC_SEPARATOR, LANE_TRAFFIC_YOU_LABEL,
//       AppendSpeedMps(player), LANE_TRAFFIC_SEPARATOR, the verdict text, then LANE_TRAFFIC_WARP_NOTE when bWarpNote
//       ("Traffic 182.00 km/s   You 175.00 km/s   MATCHED (at 1x)"); outLine overwritten; a speed below
//       NPC_LANE_MERGE_ZERO_SPEED_MPS (0.05 m/s) in size prints as 0 ("0.0 m/s", never "-0.0 m/s")
//   SOLLaneTunnelLabel::MakeMergeReadoutKey(trafficSpeedMps, playerSpeedMps, verdict, bWarpNote) -> FSOLLaneMergeKey
//   SOLLaneTunnelLabel::MergeBandColor(band) -> SOL::HudPalette::GOOD / AMBER (new) / BAD
//   SOLLaneTunnelLabel::IsMergeReadoutEnabled(bHudAllowed, bVerificationOverride, bHasSettings, bLanesRowShown,
//       bHasLanes, bHasPlayerShip) -> the HUD allows it and (the override, no settings or the Lanes row on) and a lane
//       table and a player ship exist
//   SOLLaneTunnelLabel::ChooseMergeReadoutSpot(bReadoutShown, readoutLane, labelLanes, placements, bGrownFits, previous,
//       deltaS) -> Hidden when the readout is hidden; Fallback when no entry of labelLanes (up to the shorter list)
//       names the readout lane and is placed; Fallback with bNoFit when that label is placed but the label grown by
//       the line fits nowhere (the user's Q10 answer, 2026-11-17); else Label (that entry), except that after a no-fit
//       fallback line for the same lane (bNoFit, or its wait under way, FitS > 0) the grown label must fit for
//       NPC_LANE_MERGE_LABEL_RETURN_S (FitS sums deltaS) before the readout returns to it; a label spot is left at once
//       when the grown label stops fitting; a label newly placed after a plain fallback line takes it at once
//   SOLLaneTunnelLabel::ChooseMergeFallbackSpot(spotBoxes, labelBoxes, hudBoxes, viewportPx, labelGapPx) -> the first
//       spot on the viewport, off every label grown by the gap and off every HUD panel (touching meets, invalid boxes
//       ignored), else the last spot; INDEX_NONE without spots or with a bad viewport
//   SOLLaneTunnelLabel::LaneLabelSizeWithLine(sizePx, lineWidthPx, lineHeightPx) -> (max(width, line width), height +
//       line height); the size unchanged for a non-finite or negative line width or height
//   SOLLaneTunnelLabel::PlaceLaneLabelWithLine(label, placed, lineWidthPx, lineHeightPx, avoidBoxes, viewportPx, gapPx)
//       -> the readout label grown by the line as its last line (the user's 50c decision, D18): both forms grown by
//       LaneLabelSizeWithLine (compact only when valid), the full form first (at the candidate the label took, then the
//       other), then the compact form in the same order; vertically on the anchor as PlaceLaneLabels, horizontally slid
//       as little as possible from centred while still covering the form's own centred box (OwnBoxPx: those columns,
//       the grown box's top rows); the guide-dot rows are not tested (the grown box may cover them), the viewport, the
//       avoid boxes (HUD panels and the other labels) and the gap are; not placed when placed is not or nothing fits
//
// Traffic/SOLLaneTrafficSubsystem.h: SetLanes(bodyA, bodyB, profiles) (the shortest list sets the count) and
//   ClearLanes(); GetLanes(); the readout verification override
//
// SOLConstants.h: SOL::HudPalette::AMBER (new, the readout's amber band); NPC_LANE_MERGE_MATCH_FRACTION 0.05,
// NPC_LANE_MERGE_AMBER_FRACTION 0.20, NPC_LANE_MERGE_DIRECTION_MIN_MPS 1000 and NPC_LANE_HALO_OUTER_WALL_RADII 2 (the
// readout shows from 2 wall radii inward) already exist. Menu/SOLMenuText.h: LANE_TRAFFIC_LABEL "Traffic ",
// LANE_TRAFFIC_YOU_LABEL "You ", LANE_TRAFFIC_SEPARATOR "   ", LANE_TRAFFIC_MATCHED "MATCHED", LANE_TRAFFIC_TOO_FAST
// "TOO FAST", LANE_TRAFFIC_TOO_SLOW "TOO SLOW", LANE_TRAFFIC_WARP_NOTE " (at 1x)".

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LANEMERGE_TOL = 1.0e-9;
    constexpr double LANEMERGE_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double LANEMERGE_INF = std::numeric_limits<double>::infinity();
    constexpr double LANEMERGE_WALL_RADIUS_M = 1.0e7;               // Rw of the test lanes
    constexpr double LANEMERGE_NOMINAL_LENGTH_M = 2.25e11;          // L0 (Earth - Mars scale)
    constexpr double LANEMERGE_RADIUS_A_M = 6.371e6;                // Earth
    constexpr double LANEMERGE_RADIUS_B_M = 3.3895e6;               // Mars
    const FVector3d LANEMERGE_VELOCITY_A(1.0e3, 2.98e4, 0.0);       // Body A's velocity (m/s)
    const FVector3d LANEMERGE_VELOCITY_B(-2.0e3, 2.41e4, 5.0e2);    // Body B's velocity (m/s)

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a is within relTol of b, relative to the larger magnitude (absolute below 1)
    bool LaneMergeNear(const double a, const double b, const double relTol = LANEMERGE_TOL)
    {
        return FMath::Abs(a - b) <= relTol * FMath::Max(1.0, FMath::Max(FMath::Abs(a), FMath::Abs(b)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the wall speed w
    double LaneMergeWall()
    {
        return SOL::NPC_LANE_WALL_SPEED_MPS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a merge lane from A to B with the test wall radius, end radii and body velocities
    FSOLLaneMergeLane LaneMergeMakeLane(const FVector3d& aM, const FVector3d& bM,
        const double wallRadiusM = LANEMERGE_WALL_RADIUS_M)
    {
        FSOLLaneMergeLane lane;
        lane.Profile = SOLLaneTraffic::MakeLaneProfile(wallRadiusM, LANEMERGE_NOMINAL_LENGTH_M, LANEMERGE_RADIUS_A_M,
            LANEMERGE_RADIUS_B_M);
        lane.Frame = SOLLaneTraffic::MakeLaneFrame(lane.Profile, aM, bM);
        lane.AVelocityMps = LANEMERGE_VELOCITY_A;
        lane.BVelocityMps = LANEMERGE_VELOCITY_B;
        return lane;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the test lane at its nominal length (current leg = nominal leg, the metric map the identity)
    FSOLLaneMergeLane LaneMergeNominalLane()
    {
        return LaneMergeMakeLane(FVector3d::ZeroVector, FVector3d(LANEMERGE_NOMINAL_LENGTH_M, 0.0, 0.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the test lane shortened to 0.6 of its nominal length (the metric map stretches the end zones)
    FSOLLaneMergeLane LaneMergeShortLane()
    {
        return LaneMergeMakeLane(FVector3d(1.0e9, -2.0e9, 3.0e8), FVector3d(1.0e9 + 0.6 * LANEMERGE_NOMINAL_LENGTH_M
            * 0.8, -2.0e9 + 0.6 * LANEMERGE_NOMINAL_LENGTH_M * 0.6, 3.0e8));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns |B - A| of a lane
    double LaneMergeSpan(const FSOLLaneMergeLane& lane)
    {
        return (lane.Frame.BM - lane.Frame.AM).Size();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point alongM along a lane's axis from A, rightRho and upRho wall radii off it on Right and Up
    FVector3d LaneMergePoint(const FSOLLaneMergeLane& lane, const double alongM, const double rightRho,
        const double upRho = 0.0)
    {
        const double rw = lane.Profile.WallRadiusM;
        return lane.Frame.AM + lane.Frame.Axis * alongM + lane.Frame.Right * (rightRho * rw)
            + lane.Frame.Up * (upRho * rw);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the lane frame velocity the readout uses at a foot fraction, lerp(V_A, V_B, s)
    FVector3d LaneMergeFrameVelocity(const FSOLLaneMergeLane& lane, const double footFraction)
    {
        return lane.AVelocityMps + (lane.BVelocityMps - lane.AVelocityMps) * footFraction;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the readout for one lane
    FSOLLaneMergeReadout LaneMergeReadoutOne(const FSOLLaneMergeLane& lane, const FVector3d& playerM,
        const FVector3d& velocityMps, const FVector3d& forward, const double warp = 1.0)
    {
        const FSOLLaneMergeLane lanes[] = { lane };
        return SOLLaneTraffic::MergeReadout(lanes, playerM, velocityMps, forward, warp);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an enum value as an integer for TestEqual
    template <typename TEnum>
    int32 LaneMergeInt(const TEnum value)
    {
        return static_cast<int32>(value);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected readout line built from the tokens and the HUD speed format
    FString LaneMergeExpectedLine(const double trafficMps, const double playerMps, const TCHAR* verdictText,
        const bool bWarpNote)
    {
        FString line = SOLMenuText::LANE_TRAFFIC_LABEL;
        SOLHudFormat::AppendSpeedMps(line, trafficMps);
        line += SOLMenuText::LANE_TRAFFIC_SEPARATOR;
        line += SOLMenuText::LANE_TRAFFIC_YOU_LABEL;
        SOLHudFormat::AppendSpeedMps(line, playerMps);
        line += SOLMenuText::LANE_TRAFFIC_SEPARATOR;
        line += verdictText;
        if (bWarpNote)
        {
            line += SOLMenuText::LANE_TRAFFIC_WARP_NOTE;
        }
        return line;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the readout line FormatMergeReadoutLine writes (into a pre-filled buffer, so it must overwrite)
    FString LaneMergeLine(const double trafficMps, const double playerMps, const ESOLLaneMergeVerdict verdict,
        const bool bWarpNote)
    {
        FString line = TEXT("stale text that must be overwritten");
        SOLLaneTunnelLabel::FormatMergeReadoutLine(trafficMps, playerMps, verdict, bWarpNote, line);
        return line;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeConstantsTest, "SOLTest.LaneMergeReadout.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 constants: MATCHED within 5 %, amber to 20 %, red beyond (D8); the readout shows from 2 wall radii inward (the
// halo, D6); the direction takes the velocity from 1 km/s; the amber colour is a palette constant of its own
bool FSOLLaneMergeConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("match band 0.05"), SOL::NPC_LANE_MERGE_MATCH_FRACTION, 0.05);
    TestEqual(TEXT("amber band 0.20"), SOL::NPC_LANE_MERGE_AMBER_FRACTION, 0.20);
    TestEqual(TEXT("readout from 2 wall radii"), SOL::NPC_LANE_HALO_OUTER_WALL_RADII, 2.0);
    TestEqual(TEXT("direction minimum 1 km/s"), SOL::NPC_LANE_MERGE_DIRECTION_MIN_MPS, 1000.0);
    TestFalse(TEXT("amber is not the palette green"), SOL::HudPalette::AMBER.Equals(SOL::HudPalette::GOOD, 1.0e-3f));
    TestFalse(TEXT("amber is not the palette red"), SOL::HudPalette::AMBER.Equals(SOL::HudPalette::BAD, 1.0e-3f));
    TestTrue(TEXT("amber: red above green above blue"), SOL::HudPalette::AMBER.R > SOL::HudPalette::AMBER.G
        && SOL::HudPalette::AMBER.G > SOL::HudPalette::AMBER.B);
    TestEqual(TEXT("amber is opaque"), SOL::HudPalette::AMBER.A, 1.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeRateTest, "SOLTest.LaneMergeReadout.Rate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 verdicts: delta = (v_pl - v_t) / v_t; MATCHED (green) at 0 and +-0.05, amber TOO FAST / TOO SLOW at +-0.0501
// and +-0.20, red at +-0.2001 and beyond (the boundaries belong to the better class); a player at rest or flying
// backwards is TOO SLOW red; a zero, negative or non-finite traffic speed, or a non-finite player speed, is invalid
bool FSOLLaneMergeRateTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::RateMerge;
    struct FRateCase
    {
        double PlayerMps;
        ESOLLaneMergeVerdict Verdict;
        ESOLLaneMergeBand Band;
        double Delta;
    };
    constexpr double traffic = 20000.0;
    const FRateCase cases[] = {
        { 20000.0, ESOLLaneMergeVerdict::Matched, ESOLLaneMergeBand::Green, 0.0 },
        { 21000.0, ESOLLaneMergeVerdict::Matched, ESOLLaneMergeBand::Green, 0.05 },
        { 19000.0, ESOLLaneMergeVerdict::Matched, ESOLLaneMergeBand::Green, -0.05 },
        { 21002.0, ESOLLaneMergeVerdict::TooFast, ESOLLaneMergeBand::Amber, 0.0501 },
        { 18998.0, ESOLLaneMergeVerdict::TooSlow, ESOLLaneMergeBand::Amber, -0.0501 },
        { 24000.0, ESOLLaneMergeVerdict::TooFast, ESOLLaneMergeBand::Amber, 0.20 },
        { 16000.0, ESOLLaneMergeVerdict::TooSlow, ESOLLaneMergeBand::Amber, -0.20 },
        { 24002.0, ESOLLaneMergeVerdict::TooFast, ESOLLaneMergeBand::Red, 0.2001 },
        { 15998.0, ESOLLaneMergeVerdict::TooSlow, ESOLLaneMergeBand::Red, -0.2001 },
        { 60000.0, ESOLLaneMergeVerdict::TooFast, ESOLLaneMergeBand::Red, 2.0 },
        { 0.0, ESOLLaneMergeVerdict::TooSlow, ESOLLaneMergeBand::Red, -1.0 },
        { -5000.0, ESOLLaneMergeVerdict::TooSlow, ESOLLaneMergeBand::Red, -1.25 },
    };
    for (const FRateCase& rateCase : cases)
    {
        const FString label = FString::Printf(TEXT("traffic %.0f, player %.0f"), traffic, rateCase.PlayerMps);
        const FSOLLaneMergeRating rating = RateMerge(traffic, rateCase.PlayerMps);
        TestTrue(label + TEXT(": valid"), rating.bValid);
        TestEqual(label + TEXT(": verdict"), LaneMergeInt(rating.Verdict), LaneMergeInt(rateCase.Verdict));
        TestEqual(label + TEXT(": band"), LaneMergeInt(rating.Band), LaneMergeInt(rateCase.Band));
        TestEqual(label + TEXT(": delta"), rating.Delta, rateCase.Delta, 1.0e-12);
    }

    // The bands scale with the traffic speed: the same fractions at 182 km/s
    TestEqual(TEXT("182 km/s, +4 %: matched"), LaneMergeInt(RateMerge(182000.0, 189280.0).Verdict),
        LaneMergeInt(ESOLLaneMergeVerdict::Matched));
    TestEqual(TEXT("182 km/s, -10 %: amber"), LaneMergeInt(RateMerge(182000.0, 163800.0).Band),
        LaneMergeInt(ESOLLaneMergeBand::Amber));
    TestEqual(TEXT("182 km/s, +30 %: red"), LaneMergeInt(RateMerge(182000.0, 236600.0).Band),
        LaneMergeInt(ESOLLaneMergeBand::Red));

    // Invalid inputs
    for (const double badTraffic : { 0.0, -20000.0, LANEMERGE_NAN, LANEMERGE_INF })
    {
        TestFalse(FString::Printf(TEXT("traffic %f: invalid"), badTraffic), RateMerge(badTraffic, 20000.0).bValid);
    }
    for (const double badPlayer : { LANEMERGE_NAN, LANEMERGE_INF, -LANEMERGE_INF })
    {
        TestFalse(FString::Printf(TEXT("player %f: invalid"), badPlayer), RateMerge(20000.0, badPlayer).bValid);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeDirectionTest, "SOLTest.LaneMergeReadout.DirectionSign",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 direction rules: the sign of the along-lane relative velocity when it is at least 1 km/s; below that the
// sign of the forward's along component (the lane direction closest to the player's heading) unless it is within 0.1
// of 0; then the player's half (right of the axis = outbound, on the axis outbound); NaN velocity counts as below the
// minimum, a NaN forward as 0, a NaN side as outbound
bool FSOLLaneMergeDirectionTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::MergeDirectionSign;
    struct FDirectionCase
    {
        double AlongVelocityMps;
        double AlongForward;
        double RightOffsetM;
        int32 Sign;
        const TCHAR* Description;
    };
    const FDirectionCase cases[] = {
        { 5000.0, -1.0, -1.0e6, 1, TEXT("fast outbound wins over forward and side") },
        { -5000.0, 1.0, 1.0e6, -1, TEXT("fast inbound wins over forward and side") },
        { 1000.0, -1.0, -1.0e6, 1, TEXT("exactly 1 km/s outbound takes the velocity") },
        { -1000.0, 1.0, 1.0e6, -1, TEXT("exactly 1 km/s inbound takes the velocity") },
        { 999.0, -1.0, 1.0e6, -1, TEXT("below 1 km/s: the forward (inbound)") },
        { -999.0, 0.5, -1.0e6, 1, TEXT("below 1 km/s: the forward (outbound)") },
        { 0.0, 0.2, -1.0e6, 1, TEXT("at rest, forward 0.2 along: outbound") },
        { 0.0, -0.2, 1.0e6, -1, TEXT("at rest, forward -0.2 along: inbound") },
        { 0.0, 0.05, -1.0e6, -1, TEXT("forward across the lane, left half: inbound") },
        { 0.0, -0.05, 1.0e6, 1, TEXT("forward across the lane, right half: outbound") },
        { 0.0, 0.0, 0.0, 1, TEXT("on the axis, no forward: outbound") },
        { LANEMERGE_NAN, -1.0, 1.0e6, -1, TEXT("NaN velocity: the forward") },
        { 0.0, LANEMERGE_NAN, -1.0e6, -1, TEXT("NaN forward: the side") },
        { 0.0, 0.0, LANEMERGE_NAN, 1, TEXT("NaN side: outbound") },
    };
    for (const FDirectionCase& directionCase : cases)
    {
        TestEqual(directionCase.Description, MergeDirectionSign(directionCase.AlongVelocityMps,
            directionCase.AlongForward, directionCase.RightOffsetM), directionCase.Sign);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergePointTest, "SOLTest.LaneMergeReadout.LanePoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 lane choice inputs: a point's along-axis distance from A, its arc from end ring A, the foot fraction, its
// lateral offsets and rho in wall radii; inside the leg between the two end rings and inside the
// halo (2 wall radii) or not; invalid for a bad frame, a non-drivable profile or a non-finite point
bool FSOLLaneMergePointTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::LanePointOf;
    for (const FSOLLaneMergeLane& lane : { LaneMergeNominalLane(), LaneMergeShortLane() })
    {
        const FString name = LaneMergeNear(LaneMergeSpan(lane), LANEMERGE_NOMINAL_LENGTH_M, 1.0e-6)
            ? TEXT("nominal lane") : TEXT("short lane");
        if (!TestTrue(name + TEXT(": the test lane is drivable"), lane.Profile.bDrivable && lane.Frame.bValid))
        {
            continue;
        }
        const double span = LaneMergeSpan(lane);
        const double rw = lane.Profile.WallRadiusM;
        const double ringA = lane.Profile.EndArcAM;
        const double ringB = ringA + lane.Frame.CurrentLegM;

        // A point mid-lane 0.6 Rw right and 0.8 Rw up: rho 1 (on the wall)
        const double along = 0.37 * span;
        const FSOLLanePoint point = LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, along, 0.6, 0.8));
        TestTrue(name + TEXT(": valid"), point.bValid);
        TestTrue(name + TEXT(": along"), LaneMergeNear(point.AlongM, along, 1.0e-12));
        TestTrue(name + TEXT(": leg arc from ring A"), LaneMergeNear(point.LegArcM, along - ringA, 1.0e-12));
        TestTrue(name + TEXT(": foot fraction"), LaneMergeNear(point.FootFraction, along / span, 1.0e-12));
        TestTrue(name + TEXT(": right offset"), FMath::Abs(point.RightOffsetM - 0.6 * rw) < 1.0e-3);
        TestTrue(name + TEXT(": up offset"), FMath::Abs(point.UpOffsetM - 0.8 * rw) < 1.0e-3);
        TestTrue(name + TEXT(": rho 1"), FMath::Abs(point.Rho - 1.0) < 1.0e-9);
        TestTrue(name + TEXT(": in the leg"), point.bInLeg);
        TestTrue(name + TEXT(": in the halo"), point.bInHalo);

        // The left half has a negative right offset; rho is the lateral distance whichever the side
        const FSOLLanePoint left = LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, along, -1.5));
        TestTrue(name + TEXT(": left half, negative right offset"), left.RightOffsetM < 0.0);
        TestTrue(name + TEXT(": left half, rho 1.5"), FMath::Abs(left.Rho - 1.5) < 1.0e-9);
        TestTrue(name + TEXT(": left half, in the halo"), left.bInHalo);

        // Halo edge: 1.99 Rw inside, 2.01 Rw and 2.5 Rw outside (still in the leg)
        TestTrue(name + TEXT(": 1.99 Rw in the halo"),
            LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, along, 0.0, 1.99)).bInHalo);
        const FSOLLanePoint outside = LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, along, 0.0, 2.01));
        TestFalse(name + TEXT(": 2.01 Rw outside the halo"), outside.bInHalo);
        TestTrue(name + TEXT(": 2.01 Rw still in the leg"), outside.bInLeg);
        TestFalse(name + TEXT(": 2.5 Rw outside the halo"),
            LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, along, -2.5)).bInHalo);

        // The leg's ends: just inside ring A in the leg; before ring A and past ring B outside
        TestTrue(name + TEXT(": 1 m inside ring A, in the leg"),
            LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, ringA + 1.0, 0.5)).bInLeg);
        const FSOLLanePoint beforeA = LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, ringA - 1.0e3, 0.5));
        TestFalse(name + TEXT(": 1 km before ring A, not in the leg"), beforeA.bInLeg);
        TestFalse(name + TEXT(": 1 km before ring A, not in the halo"), beforeA.bInHalo);
        TestTrue(name + TEXT(": 1 km before ring A, negative leg arc"), beforeA.LegArcM < 0.0);
        TestTrue(name + TEXT(": 1 km inside ring B, in the leg"),
            LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, ringB - 1.0e3, 0.5)).bInLeg);
        TestFalse(name + TEXT(": 1 km past ring B, not in the halo"),
            LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, ringB + 1.0e3, 0.5)).bInHalo);

        // Behind A the foot fraction is clamped to 0, beyond B to 1
        TestEqual(name + TEXT(": behind A, foot 0"),
            LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, -1.0e8, 0.0)).FootFraction, 0.0);
        TestEqual(name + TEXT(": beyond B, foot 1"),
            LanePointOf(lane.Profile, lane.Frame, LaneMergePoint(lane, span + 1.0e8, 0.0)).FootFraction, 1.0);
    }

    // Invalid inputs
    const FSOLLaneMergeLane good = LaneMergeNominalLane();
    const FVector3d mid = LaneMergePoint(good, 0.5 * LaneMergeSpan(good), 0.5);
    FSOLLaneFrame badFrame = SOLLaneTraffic::MakeLaneFrame(good.Profile, FVector3d::ZeroVector, FVector3d::ZeroVector);
    const FSOLLanePoint noFrame = LanePointOf(good.Profile, badFrame, mid);
    TestFalse(TEXT("A == B: invalid"), noFrame.bValid);
    TestFalse(TEXT("A == B: not in the halo"), noFrame.bInHalo);
    const FSOLLaneProfile badProfile = SOLLaneTraffic::MakeLaneProfile(0.0, LANEMERGE_NOMINAL_LENGTH_M,
        LANEMERGE_RADIUS_A_M, LANEMERGE_RADIUS_B_M);
    TestFalse(TEXT("zero wall radius: invalid"), LanePointOf(badProfile, good.Frame, mid).bValid);
    TestFalse(TEXT("zero wall radius: not in the halo"), LanePointOf(badProfile, good.Frame, mid).bInHalo);
    TestFalse(TEXT("NaN point: invalid"), LanePointOf(good.Profile, good.Frame,
        FVector3d(LANEMERGE_NAN, 0.0, 0.0)).bValid);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeTrafficSpeedTest, "SOLTest.LaneMergeReadout.TrafficSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 traffic speed: v_t = MeanSpeedMps(u_d, rho) x X'(u_d), u_d the nominal leg coordinate for the direction (X
// inverted); the wall speed at the wall and at both end rings, fastest at the axis mid-lane, the same for both
// directions at a point (the profile and map are symmetric), on a shortened lane the stretched closed form; 0 for a
// bad lane or a NaN input
bool FSOLLaneMergeTrafficSpeedTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTraffic::TrafficSpeedMps;
    const double w = LaneMergeWall();

    // Nominal length: X is the identity, so v_t is the mean speed field at the leg arc
    {
        const FSOLLaneMergeLane lane = LaneMergeNominalLane();
        const FSOLLaneProfile& profile = lane.Profile;
        const FSOLLaneFrame& frame = lane.Frame;
        const double l0 = profile.NominalLegM;
        TestTrue(TEXT("nominal lane: current leg = nominal leg"), LaneMergeNear(frame.CurrentLegM, l0, 1.0e-12));
        for (const double fraction : { 0.0, 0.01, 0.1, 0.37, 0.5, 0.8, 1.0 })
        {
            for (const double rho : { 0.0, 0.2, 0.5, 0.8, 1.0, 1.5, 2.0 })
            {
                const double x = fraction * l0;
                const FString label = FString::Printf(TEXT("nominal, x %.2f l0, rho %.1f"), fraction, rho);
                const double outbound = TrafficSpeedMps(profile, frame, x, rho, 1);
                const double inbound = TrafficSpeedMps(profile, frame, x, rho, -1);
                TestTrue(label + TEXT(": outbound = MeanSpeedMps(x)"),
                    LaneMergeNear(outbound, SOLLaneTraffic::MeanSpeedMps(x, rho, l0)));
                TestTrue(label + TEXT(": inbound = MeanSpeedMps(l - x)"),
                    LaneMergeNear(inbound, SOLLaneTraffic::MeanSpeedMps(l0 - x, rho, l0)));
                TestTrue(label + TEXT(": both directions agree"), LaneMergeNear(outbound, inbound));
                TestTrue(label + TEXT(": at least the wall speed"), outbound >= w * (1.0 - LANEMERGE_TOL));
            }
        }

        // The wall speed at the wall everywhere and at both end rings for every rho
        for (const double fraction : { 0.0, 0.2, 0.5, 0.9, 1.0 })
        {
            TestTrue(FString::Printf(TEXT("wall (rho 1) at x %.1f l0: w"), fraction),
                LaneMergeNear(TrafficSpeedMps(profile, frame, fraction * l0, 1.0, 1), w));
        }
        for (const double rho : { 0.0, 0.5, 1.0, 1.7 })
        {
            TestTrue(FString::Printf(TEXT("ring A, rho %.1f: w"), rho),
                LaneMergeNear(TrafficSpeedMps(profile, frame, 0.0, rho, 1), w));
            TestTrue(FString::Printf(TEXT("ring B, rho %.1f: w"), rho),
                LaneMergeNear(TrafficSpeedMps(profile, frame, l0, rho, -1), w));
        }

        // Wall versus centre mid-lane: strictly faster toward the axis; the halo just above the wall speed
        const double mid = 0.5 * l0;
        const double axis = TrafficSpeedMps(profile, frame, mid, 0.0, 1);
        const double half = TrafficSpeedMps(profile, frame, mid, 0.5, 1);
        const double wall = TrafficSpeedMps(profile, frame, mid, 1.0, 1);
        const double halo = TrafficSpeedMps(profile, frame, mid, 1.5, 1);
        TestTrue(TEXT("mid-lane: axis faster than rho 0.5"), axis > half);
        TestTrue(TEXT("mid-lane: rho 0.5 faster than the wall"), half > wall);
        TestTrue(TEXT("mid-lane: the axis well above the wall speed"), axis > 2.0 * w);
        TestTrue(TEXT("mid-lane: the halo at or above the wall speed"), halo >= wall * (1.0 - LANEMERGE_TOL));
        TestTrue(TEXT("mid-lane: the halo below rho 0.5"), halo < half);
        TestTrue(TEXT("mid-lane: never above the cap"), axis <= SOL::NPC_LANE_SPEED_CAP_MPS * (1.0 + LANEMERGE_TOL));

        // Literal values: 20 km/s at the wall, the 300 km/s cap on the axis mid-lane of this Earth - Mars scale lane
        TestTrue(FString::Printf(TEXT("literal: the wall mid-lane 20000 m/s (got %.6f)"), wall),
            FMath::Abs(wall - 20000.0) < 1.0e-6);
        TestTrue(FString::Printf(TEXT("literal: the axis mid-lane 300000 m/s (got %.6f)"), axis),
            FMath::Abs(axis - 300000.0) < 1.0e-6);
        TestTrue(TEXT("literal: ring A on the axis 20000 m/s"),
            FMath::Abs(TrafficSpeedMps(profile, frame, 0.0, 0.0, 1) - 20000.0) < 1.0e-6);

        // A leg arc outside the leg is clamped to it
        TestTrue(TEXT("before ring A: clamped to ring A"), LaneMergeNear(TrafficSpeedMps(profile, frame, -1.0e7, 0.0, 1),
            TrafficSpeedMps(profile, frame, 0.0, 0.0, 1)));
        TestTrue(TEXT("past ring B: clamped to ring B"), LaneMergeNear(TrafficSpeedMps(profile, frame, l0 + 1.0e7, 0.0,
            1), TrafficSpeedMps(profile, frame, l0, 0.0, 1)));
    }

    // A shortened lane: the stretched closed form, both directions agreeing
    {
        const FSOLLaneMergeLane lane = LaneMergeShortLane();
        const FSOLLaneProfile& profile = lane.Profile;
        const FSOLLaneFrame& frame = lane.Frame;
        const double l0 = profile.NominalLegM;
        const double l = frame.CurrentLegM;
        TestTrue(TEXT("short lane: the current leg is shorter"), l < 0.7 * l0 && l > 0.5 * l0);
        for (const double fraction : { 0.001, 0.02, 0.04, 0.3, 0.5, 0.97 })
        {
            for (const double rho : { 0.0, 0.6, 1.0, 1.4 })
            {
                const double x = fraction * l;
                const FString label = FString::Printf(TEXT("short, x %.3f l, rho %.1f"), fraction, rho);
                const double uOut = SOLLaneTraffic::MetricMapInverseM(x, l0, l);
                const double uIn = SOLLaneTraffic::MetricMapInverseM(l - x, l0, l);
                const double expectedOut = SOLLaneTraffic::MeanSpeedMps(uOut, rho, l0)
                    * SOLLaneTraffic::MetricMapSlope(uOut, l0, l);
                const double expectedIn = SOLLaneTraffic::MeanSpeedMps(uIn, rho, l0)
                    * SOLLaneTraffic::MetricMapSlope(uIn, l0, l);
                const double outbound = TrafficSpeedMps(profile, frame, x, rho, 1);
                const double inbound = TrafficSpeedMps(profile, frame, x, rho, -1);
                TestTrue(label + TEXT(": outbound closed form"), LaneMergeNear(outbound, expectedOut));
                TestTrue(label + TEXT(": inbound closed form"), LaneMergeNear(inbound, expectedIn));
                TestTrue(label + TEXT(": both directions agree"), LaneMergeNear(outbound, inbound, 1.0e-6));
            }
        }
        TestTrue(TEXT("short lane: the wall speed at ring A"), LaneMergeNear(TrafficSpeedMps(profile, frame, 0.0, 0.3,
            1), w));
    }

    // Bad inputs
    {
        const FSOLLaneMergeLane lane = LaneMergeNominalLane();
        const double mid = 0.5 * lane.Profile.NominalLegM;
        const FSOLLaneFrame badFrame = SOLLaneTraffic::MakeLaneFrame(lane.Profile, FVector3d::ZeroVector,
            FVector3d::ZeroVector);
        const FSOLLaneProfile badProfile = SOLLaneTraffic::MakeLaneProfile(0.0, LANEMERGE_NOMINAL_LENGTH_M,
            LANEMERGE_RADIUS_A_M, LANEMERGE_RADIUS_B_M);
        TestEqual(TEXT("invalid frame: 0"), TrafficSpeedMps(lane.Profile, badFrame, mid, 0.5, 1), 0.0);
        TestEqual(TEXT("non-drivable profile: 0"), TrafficSpeedMps(badProfile, lane.Frame, mid, 0.5, 1), 0.0);
        TestEqual(TEXT("NaN leg arc: 0"), TrafficSpeedMps(lane.Profile, lane.Frame, LANEMERGE_NAN, 0.5, 1), 0.0);
        TestEqual(TEXT("NaN rho: 0"), TrafficSpeedMps(lane.Profile, lane.Frame, mid, LANEMERGE_NAN, 1), 0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeLaneChoiceTest, "SOLTest.LaneMergeReadout.LaneChoice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 lane choice: the readout shows for a lane whose halo holds the player (from 2 wall radii inward, between the
// two end rings); with several, the smallest rho wins (whatever the lane order); hidden with no lanes, outside every
// halo, outside the leg, and for a non-drivable or invalid lane even when the player sits on its axis
bool FSOLLaneMergeLaneChoiceTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane0 = LaneMergeNominalLane();
    const double span = LaneMergeSpan(lane0);
    const double rw = lane0.Profile.WallRadiusM;
    const double along = 0.5 * span;
    const FVector3d forward = lane0.Frame.Axis;
    const FVector3d frameVelocity = LaneMergeFrameVelocity(lane0, 0.5);
    const FVector3d velocity = frameVelocity + lane0.Frame.Axis * 25000.0;

    // One lane: shown inside 2 Rw, hidden outside
    for (const double rho : { 0.0, 0.2, 0.8, 1.5, 1.99 })
    {
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane0, LaneMergePoint(lane0, along, rho), velocity,
            forward);
        TestTrue(FString::Printf(TEXT("rho %.2f: shown"), rho), readout.bShown);
        TestEqual(FString::Printf(TEXT("rho %.2f: lane 0"), rho), readout.Lane, 0);
        TestTrue(FString::Printf(TEXT("rho %.2f: rho reported"), rho), FMath::Abs(readout.Rho - rho) < 1.0e-9);
    }
    for (const double rho : { 2.01, 2.5, 10.0 })
    {
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane0, LaneMergePoint(lane0, along, -rho), velocity,
            forward);
        TestFalse(FString::Printf(TEXT("rho %.2f: hidden"), rho), readout.bShown);
        TestEqual(FString::Printf(TEXT("rho %.2f: no lane"), rho), readout.Lane, static_cast<int32>(INDEX_NONE));
    }

    // Outside the leg: before ring A and past ring B, even on the axis
    const double ringA = lane0.Profile.EndArcAM;
    const double ringB = ringA + lane0.Frame.CurrentLegM;
    TestFalse(TEXT("before ring A: hidden"), LaneMergeReadoutOne(lane0, LaneMergePoint(lane0, ringA - 1.0e3, 0.0),
        velocity, forward).bShown);
    TestTrue(TEXT("just inside ring A: shown"), LaneMergeReadoutOne(lane0, LaneMergePoint(lane0, ringA + 1.0e3, 0.0),
        velocity, forward).bShown);
    TestTrue(TEXT("just inside ring B: shown"), LaneMergeReadoutOne(lane0, LaneMergePoint(lane0, ringB - 1.0e3, 0.0),
        velocity, forward).bShown);
    TestFalse(TEXT("past ring B: hidden"), LaneMergeReadoutOne(lane0, LaneMergePoint(lane0, ringB + 1.0e3, 0.0),
        velocity, forward).bShown);

    // No lanes: hidden
    const FSOLLaneMergeReadout none = SOLLaneTraffic::MergeReadout(TConstArrayView<FSOLLaneMergeLane>(),
        LaneMergePoint(lane0, along, 0.0), velocity, forward, 1.0);
    TestFalse(TEXT("no lanes: hidden"), none.bShown);
    TestEqual(TEXT("no lanes: no lane"), none.Lane, static_cast<int32>(INDEX_NONE));

    // Two parallel lanes 1 Rw apart (lane 1 shifted along lane 0's Right): the nearer axis wins, whatever the order
    const FSOLLaneMergeLane lane1 = LaneMergeMakeLane(lane0.Frame.AM + lane0.Frame.Right * rw,
        lane0.Frame.BM + lane0.Frame.Right * rw);
    const TArray<FSOLLaneMergeLane> both = { lane0, lane1 };
    const TArray<FSOLLaneMergeLane> swapped = { lane1, lane0 };
    struct FChoiceCase
    {
        double RightRho;            // Off lane 0's axis
        int32 Lane;                 // Expected index in { lane0, lane1 }
    };
    const FChoiceCase choices[] = { { 0.2, 0 }, { 0.4, 0 }, { 0.6, 1 }, { 0.7, 1 }, { 1.8, 1 }, { -1.5, 0 } };
    for (const FChoiceCase& choice : choices)
    {
        const FVector3d player = LaneMergePoint(lane0, along, choice.RightRho);
        const FSOLLaneMergeReadout readout = SOLLaneTraffic::MergeReadout(both, player, velocity, forward, 1.0);
        const FSOLLaneMergeReadout readoutSwapped = SOLLaneTraffic::MergeReadout(swapped, player, velocity, forward,
            1.0);
        const FString label = FString::Printf(TEXT("two lanes, %.1f Rw right of lane 0"), choice.RightRho);
        TestTrue(label + TEXT(": shown"), readout.bShown);
        TestEqual(label + TEXT(": the nearer lane"), readout.Lane, choice.Lane);
        TestEqual(label + TEXT(": the nearer lane (swapped order)"), readoutSwapped.Lane, 1 - choice.Lane);
        const double expectedRho = choice.Lane == 0 ? FMath::Abs(choice.RightRho) : FMath::Abs(choice.RightRho - 1.0);
        TestTrue(label + TEXT(": its rho"), FMath::Abs(readout.Rho - expectedRho) < 1.0e-9);
    }

    // Two identical lanes: the same rho, the lower index wins
    const TArray<FSOLLaneMergeLane> twins = { lane0, lane0 };
    const FSOLLaneMergeReadout tie = SOLLaneTraffic::MergeReadout(twins, LaneMergePoint(lane0, along, 0.5), velocity,
        forward, 1.0);
    TestTrue(TEXT("a tie: shown"), tie.bShown);
    TestEqual(TEXT("a tie: the lower lane index"), tie.Lane, 0);

    // A non-drivable lane and an invalid lane are skipped, even with the player on their axis
    FSOLLaneMergeLane noWall = lane0;
    noWall.Profile = SOLLaneTraffic::MakeLaneProfile(0.0, LANEMERGE_NOMINAL_LENGTH_M, LANEMERGE_RADIUS_A_M,
        LANEMERGE_RADIUS_B_M);
    FSOLLaneMergeLane noFrame = lane0;
    noFrame.Frame = SOLLaneTraffic::MakeLaneFrame(lane0.Profile, FVector3d::ZeroVector, FVector3d::ZeroVector);
    const FVector3d onAxis = LaneMergePoint(lane0, along, 0.0);
    TestFalse(TEXT("non-drivable lane only: hidden"), LaneMergeReadoutOne(noWall, onAxis, velocity, forward).bShown);
    TestFalse(TEXT("invalid frame only: hidden"), LaneMergeReadoutOne(noFrame, onAxis, velocity, forward).bShown);
    const TArray<FSOLLaneMergeLane> withBad = { noWall, noFrame, lane1 };
    const FSOLLaneMergeReadout skipBad = SOLLaneTraffic::MergeReadout(withBad, onAxis, velocity, forward, 1.0);
    TestTrue(TEXT("bad lanes first: the good lane shown"), skipBad.bShown);
    TestEqual(TEXT("bad lanes first: the good lane's index"), skipBad.Lane, 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeVerdictGridTest, "SOLTest.LaneMergeReadout.VerdictGrid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 and smoke V6 as a unit test: the player mid-lane at rho 1.5, 0.8 and 0.2 on the outbound half with 0.9, 1.0
// and 1.3 x v_t along the lane relative to the lane frame reads TOO SLOW amber, MATCHED green and TOO FAST red; v_t is
// TrafficSpeedMps at the player's arc and rho; the frame velocity is lerp(V_A, V_B, s), the player speed the along-lane
// component relative to it
bool FSOLLaneMergeVerdictGridTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneMergeNominalLane();
    const double span = LaneMergeSpan(lane);
    const double along = 0.5 * span;
    const FVector3d frameVelocity = LaneMergeFrameVelocity(lane, 0.5);
    struct FGridSpeed
    {
        double Factor;
        ESOLLaneMergeVerdict Verdict;
        ESOLLaneMergeBand Band;
    };
    const FGridSpeed speeds[] = {
        { 0.9, ESOLLaneMergeVerdict::TooSlow, ESOLLaneMergeBand::Amber },
        { 1.0, ESOLLaneMergeVerdict::Matched, ESOLLaneMergeBand::Green },
        { 1.3, ESOLLaneMergeVerdict::TooFast, ESOLLaneMergeBand::Red },
    };
    for (const double rho : { 1.5, 0.8, 0.2 })
    {
        const double trafficMps = SOLLaneTraffic::TrafficSpeedMps(lane.Profile, lane.Frame,
            along - lane.Profile.EndArcAM, rho, 1);
        for (const FGridSpeed& speed : speeds)
        {
            const FString label = FString::Printf(TEXT("rho %.1f, %.1f x v_t"), rho, speed.Factor);
            const FVector3d velocity = frameVelocity + lane.Frame.Axis * (speed.Factor * trafficMps);
            const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, rho), velocity,
                lane.Frame.Axis);
            if (!TestTrue(label + TEXT(": shown"), readout.bShown))
            {
                continue;
            }
            TestEqual(label + TEXT(": outbound"), readout.DirectionSign, 1);
            TestTrue(label + TEXT(": traffic speed = TrafficSpeedMps"), LaneMergeNear(readout.TrafficSpeedMps,
                trafficMps));
            TestTrue(label + TEXT(": player speed"), LaneMergeNear(readout.PlayerSpeedMps, speed.Factor * trafficMps));
            TestTrue(label + TEXT(": delta"), FMath::Abs(readout.Delta - (speed.Factor - 1.0)) < 1.0e-9);
            TestEqual(label + TEXT(": verdict"), LaneMergeInt(readout.Verdict), LaneMergeInt(speed.Verdict));
            TestEqual(label + TEXT(": band"), LaneMergeInt(readout.Band), LaneMergeInt(speed.Band));
            TestTrue(label + TEXT(": frame velocity at mid-lane"),
                readout.FrameVelocityMps.Equals(frameVelocity, 1.0e-6));
        }
    }

    // The traffic speed at the wall rho beats the halo and the centre beats the wall (the readout follows the field)
    const auto trafficAt = [&](const double rho)
    {
        return LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, rho), frameVelocity, lane.Frame.Axis)
            .TrafficSpeedMps;
    };
    TestTrue(TEXT("readout: centre traffic faster than at the wall"), trafficAt(0.1) > trafficAt(1.0));
    TestTrue(TEXT("readout: the wall traffic at the wall speed"), LaneMergeNear(trafficAt(1.0), LaneMergeWall()));

    // Off-centre the frame velocity is lerp(V_A, V_B, s), s = along / |B - A|
    {
        const double quarter = 0.25 * span;
        const FVector3d quarterFrame = LaneMergeFrameVelocity(lane, 0.25);
        const double trafficMps = SOLLaneTraffic::TrafficSpeedMps(lane.Profile, lane.Frame,
            quarter - lane.Profile.EndArcAM, 0.5, 1);
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, LaneMergePoint(lane, quarter, 0.5),
            quarterFrame + lane.Frame.Axis * trafficMps, lane.Frame.Axis);
        TestTrue(TEXT("quarter: shown"), readout.bShown);
        TestTrue(TEXT("quarter: frame velocity = lerp(V_A, V_B, 0.25)"),
            readout.FrameVelocityMps.Equals(quarterFrame, 1.0e-6));
        TestEqual(TEXT("quarter: matched against the lerped frame"), LaneMergeInt(readout.Verdict),
            LaneMergeInt(ESOLLaneMergeVerdict::Matched));

        // The same absolute velocity judged against body A's velocity instead would be off by |V_B - V_A| / 4
        TestTrue(TEXT("quarter: the player speed is relative to the lerped frame"),
            FMath::Abs(readout.PlayerSpeedMps - trafficMps) < 1.0e-6);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeReadoutDirectionTest, "SOLTest.LaneMergeReadout.ReadoutDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 direction in the readout: flying inbound compares against the inbound traffic (the same speed at that point,
// a positive player speed); the velocity decides above 1 km/s even on the wrong half; below it the heading picks the
// lane direction; at rest and heading across the lane the half decides; crossing motion and rest read TOO SLOW red
bool FSOLLaneMergeReadoutDirectionTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneMergeNominalLane();
    const double along = 0.5 * LaneMergeSpan(lane);
    const double legArc = along - lane.Profile.EndArcAM;
    const FVector3d axis = lane.Frame.Axis;
    const FVector3d up = lane.Frame.Up;
    const FVector3d frameVelocity = LaneMergeFrameVelocity(lane, 0.5);
    const double trafficMps = SOLLaneTraffic::TrafficSpeedMps(lane.Profile, lane.Frame, legArc, 0.5, 1);

    // Inbound on the left half at the traffic speed: matched, positive player speed
    {
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, -0.5),
            frameVelocity - axis * trafficMps, -axis);
        TestTrue(TEXT("inbound, left: shown"), readout.bShown);
        TestEqual(TEXT("inbound, left: inbound"), readout.DirectionSign, -1);
        TestTrue(TEXT("inbound, left: the same traffic speed"), LaneMergeNear(readout.TrafficSpeedMps, trafficMps,
            1.0e-6));
        TestTrue(TEXT("inbound, left: positive player speed"), LaneMergeNear(readout.PlayerSpeedMps, trafficMps));
        TestEqual(TEXT("inbound, left: matched"), LaneMergeInt(readout.Verdict),
            LaneMergeInt(ESOLLaneMergeVerdict::Matched));
    }

    // Inbound on the right half (the outbound side) faster than 1 km/s: the velocity decides
    {
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, 0.5),
            frameVelocity - axis * trafficMps, axis);
        TestEqual(TEXT("inbound on the right half: inbound"), readout.DirectionSign, -1);
        TestEqual(TEXT("inbound on the right half: matched"), LaneMergeInt(readout.Verdict),
            LaneMergeInt(ESOLLaneMergeVerdict::Matched));
    }

    // Slow (500 m/s outbound) but heading inbound: inbound, a negative player speed, TOO SLOW red
    {
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, 0.5),
            frameVelocity + axis * 500.0, -axis);
        TestEqual(TEXT("slow, heading inbound: inbound"), readout.DirectionSign, -1);
        TestTrue(TEXT("slow, heading inbound: player speed -500"), LaneMergeNear(readout.PlayerSpeedMps, -500.0,
            1.0e-9));
        TestEqual(TEXT("slow, heading inbound: too slow"), LaneMergeInt(readout.Verdict),
            LaneMergeInt(ESOLLaneMergeVerdict::TooSlow));
        TestEqual(TEXT("slow, heading inbound: red"), LaneMergeInt(readout.Band), LaneMergeInt(ESOLLaneMergeBand::Red));
    }

    // At rest relative to the lane, heading across it (up): the half decides; rest is TOO SLOW red, delta -1
    {
        const FSOLLaneMergeReadout left = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, -0.5), frameVelocity,
            up);
        TestEqual(TEXT("at rest, left half: inbound"), left.DirectionSign, -1);
        TestTrue(TEXT("at rest, left half: player speed 0"), FMath::Abs(left.PlayerSpeedMps) < 1.0e-9);
        TestTrue(TEXT("at rest, left half: delta -1"), FMath::Abs(left.Delta + 1.0) < 1.0e-12);
        TestEqual(TEXT("at rest, left half: too slow"), LaneMergeInt(left.Verdict),
            LaneMergeInt(ESOLLaneMergeVerdict::TooSlow));
        TestEqual(TEXT("at rest, left half: red"), LaneMergeInt(left.Band), LaneMergeInt(ESOLLaneMergeBand::Red));
        const FSOLLaneMergeReadout right = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, 0.5), frameVelocity,
            up);
        TestEqual(TEXT("at rest, right half: outbound"), right.DirectionSign, 1);

        // A zero or NaN forward also falls to the half
        TestEqual(TEXT("at rest, zero forward, left half: inbound"), LaneMergeReadoutOne(lane,
            LaneMergePoint(lane, along, -0.5), frameVelocity, FVector3d::ZeroVector).DirectionSign, -1);
        const FSOLLaneMergeReadout nanForward = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, 0.5),
            frameVelocity, FVector3d(LANEMERGE_NAN, 0.0, 0.0));
        TestTrue(TEXT("at rest, NaN forward: still shown"), nanForward.bShown);
        TestEqual(TEXT("at rest, NaN forward, right half: outbound"), nanForward.DirectionSign, 1);
    }

    // Crossing the lane at 50 km/s (no along component), heading outbound: TOO SLOW red, player speed 0
    {
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, 0.5),
            frameVelocity + up * 50000.0, axis);
        TestTrue(TEXT("crossing: shown"), readout.bShown);
        TestEqual(TEXT("crossing: outbound (the heading)"), readout.DirectionSign, 1);
        TestTrue(TEXT("crossing: player speed 0"), FMath::Abs(readout.PlayerSpeedMps) < 1.0e-6);
        TestEqual(TEXT("crossing: too slow"), LaneMergeInt(readout.Verdict),
            LaneMergeInt(ESOLLaneMergeVerdict::TooSlow));
        TestEqual(TEXT("crossing: red"), LaneMergeInt(readout.Band), LaneMergeInt(ESOLLaneMergeBand::Red));
    }

    // A forward that is not unit length is normalised: 0.05 x axis still heads outbound
    {
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, LaneMergePoint(lane, along, -0.5),
            frameVelocity, axis * 0.05);
        TestEqual(TEXT("short forward along the axis: outbound"), readout.DirectionSign, 1);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeWarpTest, "SOLTest.LaneMergeReadout.WarpAndBadInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 and D7: the warp note shows only above 1x, the speeds are sim seconds and identical at every warp; a NaN or
// infinite player position or velocity hides the readout
bool FSOLLaneMergeWarpTest::RunTest(const FString& /*parameters*/)
{
    const FSOLLaneMergeLane lane = LaneMergeNominalLane();
    const double along = 0.5 * LaneMergeSpan(lane);
    const FVector3d player = LaneMergePoint(lane, along, 0.4);
    const FVector3d velocity = LaneMergeFrameVelocity(lane, 0.5) + lane.Frame.Axis * 31000.0;
    const FVector3d forward = lane.Frame.Axis;
    const FSOLLaneMergeReadout atOne = LaneMergeReadoutOne(lane, player, velocity, forward, 1.0);
    TestTrue(TEXT("1x: shown"), atOne.bShown);
    TestFalse(TEXT("1x: no warp note"), atOne.bWarpNote);
    struct FWarpCase
    {
        double Warp;
        bool bNote;
    };
    const FWarpCase warps[] = { { 0.0, false }, { -5.0, false }, { 0.5, false }, { 1.0, false }, { 2.0, true },
        { 1000.0, true }, { 1.0e6, true }, { LANEMERGE_NAN, false } };
    for (const FWarpCase& warpCase : warps)
    {
        const FString label = FString::Printf(TEXT("warp %f"), warpCase.Warp);
        const FSOLLaneMergeReadout readout = LaneMergeReadoutOne(lane, player, velocity, forward, warpCase.Warp);
        TestTrue(label + TEXT(": shown"), readout.bShown);
        TestEqual(label + TEXT(": warp note"), readout.bWarpNote, warpCase.bNote);
        TestEqual(label + TEXT(": traffic speed unscaled"), readout.TrafficSpeedMps, atOne.TrafficSpeedMps);
        TestEqual(label + TEXT(": player speed unscaled"), readout.PlayerSpeedMps, atOne.PlayerSpeedMps);
        TestEqual(label + TEXT(": verdict unchanged"), LaneMergeInt(readout.Verdict), LaneMergeInt(atOne.Verdict));
    }

    // Bad player state: hidden
    for (const double bad : { LANEMERGE_NAN, LANEMERGE_INF })
    {
        const FString label = FString::Printf(TEXT("%f"), bad);
        TestFalse(label + TEXT(" position: hidden"), LaneMergeReadoutOne(lane, FVector3d(player.X, bad, player.Z),
            velocity, forward).bShown);
        TestFalse(label + TEXT(" velocity: hidden"), LaneMergeReadoutOne(lane, player,
            FVector3d(velocity.X, velocity.Y, bad), forward).bShown);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeTextTest, "SOLTest.LaneMergeReadout.TextLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 text: "Traffic <speed>   You <speed>   MATCHED", plus " (at 1x)" above 1x (D7), from the SOLMenuText tokens
// and SOLHudFormat's speed format; the verdict words; the line overwrites its buffer and is plain ASCII with a '.'
// decimal point and no placeholder whatever the verdict, sign or size (no locale-dependent formatting)
bool FSOLLaneMergeTextTest::RunTest(const FString& /*parameters*/)
{
    // The tokens
    TestEqual(TEXT("traffic label"), FString(SOLMenuText::LANE_TRAFFIC_LABEL), FString(TEXT("Traffic ")));
    TestEqual(TEXT("you label"), FString(SOLMenuText::LANE_TRAFFIC_YOU_LABEL), FString(TEXT("You ")));
    TestEqual(TEXT("separator"), FString(SOLMenuText::LANE_TRAFFIC_SEPARATOR), FString(TEXT("   ")));
    TestEqual(TEXT("matched"), FString(SOLMenuText::LANE_TRAFFIC_MATCHED), FString(TEXT("MATCHED")));
    TestEqual(TEXT("too fast"), FString(SOLMenuText::LANE_TRAFFIC_TOO_FAST), FString(TEXT("TOO FAST")));
    TestEqual(TEXT("too slow"), FString(SOLMenuText::LANE_TRAFFIC_TOO_SLOW), FString(TEXT("TOO SLOW")));
    TestEqual(TEXT("warp note"), FString(SOLMenuText::LANE_TRAFFIC_WARP_NOTE), FString(TEXT(" (at 1x)")));

    // The verdict words
    TestEqual(TEXT("verdict text: matched"), FString(SOLLaneTunnelLabel::MergeVerdictText(
        ESOLLaneMergeVerdict::Matched)), FString(SOLMenuText::LANE_TRAFFIC_MATCHED));
    TestEqual(TEXT("verdict text: too fast"), FString(SOLLaneTunnelLabel::MergeVerdictText(
        ESOLLaneMergeVerdict::TooFast)), FString(SOLMenuText::LANE_TRAFFIC_TOO_FAST));
    TestEqual(TEXT("verdict text: too slow"), FString(SOLLaneTunnelLabel::MergeVerdictText(
        ESOLLaneMergeVerdict::TooSlow)), FString(SOLMenuText::LANE_TRAFFIC_TOO_SLOW));

    // The line, with and without the warp note
    struct FTextCase
    {
        double TrafficMps;
        double PlayerMps;
        ESOLLaneMergeVerdict Verdict;
        bool bWarpNote;
    };
    const FTextCase cases[] = {
        { 182000.0, 175000.0, ESOLLaneMergeVerdict::Matched, false },
        { 182000.0, 175000.0, ESOLLaneMergeVerdict::Matched, true },
        { 20000.0, 26000.0, ESOLLaneMergeVerdict::TooFast, false },
        { 20000.0, 18000.0, ESOLLaneMergeVerdict::TooSlow, true },
        { 20000.0, 0.0, ESOLLaneMergeVerdict::TooSlow, false },
        { 20000.0, -500.0, ESOLLaneMergeVerdict::TooSlow, false },
        { 20000.0, 999.0, ESOLLaneMergeVerdict::TooSlow, false },
        { 295000.0, 1.9e6, ESOLLaneMergeVerdict::TooFast, true },
    };
    for (const FTextCase& textCase : cases)
    {
        const FString line = LaneMergeLine(textCase.TrafficMps, textCase.PlayerMps, textCase.Verdict,
            textCase.bWarpNote);
        const FString expected = LaneMergeExpectedLine(textCase.TrafficMps, textCase.PlayerMps,
            SOLLaneTunnelLabel::MergeVerdictText(textCase.Verdict), textCase.bWarpNote);
        const FString label = FString::Printf(TEXT("traffic %.0f, you %.0f"), textCase.TrafficMps, textCase.PlayerMps);
        TestEqual(label + TEXT(": the line"), line, expected);
        TestTrue(label + TEXT(": starts with Traffic"), line.StartsWith(TEXT("Traffic "), ESearchCase::CaseSensitive));
        TestEqual(label + TEXT(": the warp note only above 1x"), line.EndsWith(TEXT(" (at 1x)"),
            ESearchCase::CaseSensitive), textCase.bWarpNote);
        TestFalse(label + TEXT(": no placeholder"), line.Contains(TEXT("{")) || line.Contains(TEXT("}")));
        TestFalse(label + TEXT(": no thousands or decimal comma"), line.Contains(TEXT(",")));
        bool bAscii = true;
        for (const TCHAR character : line)
        {
            bAscii &= character >= TEXT(' ') && character <= TEXT('~');
        }
        TestTrue(label + TEXT(": plain ASCII, one line"), bAscii);
    }

    // Literal lines (the SDD's worked example and the unit switches)
    TestEqual(TEXT("literal: the worked example"), LaneMergeLine(182000.0, 175000.0, ESOLLaneMergeVerdict::Matched,
        false), FString(TEXT("Traffic 182.00 km/s   You 175.00 km/s   MATCHED")));
    TestEqual(TEXT("literal: the worked example under warp"), LaneMergeLine(182000.0, 175000.0,
        ESOLLaneMergeVerdict::Matched, true), FString(TEXT("Traffic 182.00 km/s   You 175.00 km/s   MATCHED (at 1x)")));
    TestEqual(TEXT("literal: m/s below 1 km/s"), LaneMergeLine(20000.0, 999.0, ESOLLaneMergeVerdict::TooSlow, false),
        FString(TEXT("Traffic 20.00 km/s   You 999.0 m/s   TOO SLOW")));
    TestEqual(TEXT("literal: flying backwards"), LaneMergeLine(20000.0, -500.0, ESOLLaneMergeVerdict::TooSlow, false),
        FString(TEXT("Traffic 20.00 km/s   You -500.0 m/s   TOO SLOW")));
    TestEqual(TEXT("literal: four digits of km/s"), LaneMergeLine(295000.0, 1.9e6, ESOLLaneMergeVerdict::TooFast,
        true), FString(TEXT("Traffic 295.00 km/s   You 1900.00 km/s   TOO FAST (at 1x)")));

    // Speeds under 0.05 m/s in size print as 0, never "-0.0" (and the key agrees, so the line does not flicker)
    for (const double nearZero : { -0.049, -0.04, -1.0e-9, -0.0, 0.0, 1.0e-9, 0.04, 0.049 })
    {
        const FString label = FString::Printf(TEXT("you %g m/s"), nearZero);
        TestEqual(label + TEXT(": prints 0.0 m/s"), LaneMergeLine(20000.0, nearZero, ESOLLaneMergeVerdict::TooSlow,
            false), FString(TEXT("Traffic 20.00 km/s   You 0.0 m/s   TOO SLOW")));
        TestTrue(label + TEXT(": the key of 0"), SOLLaneTunnelLabel::MakeMergeReadoutKey(20000.0, nearZero,
            ESOLLaneMergeVerdict::TooSlow, false) == SOLLaneTunnelLabel::MakeMergeReadoutKey(20000.0, 0.0,
            ESOLLaneMergeVerdict::TooSlow, false));
    }
    TestEqual(TEXT("you -0.06 m/s: still negative"), LaneMergeLine(20000.0, -0.06, ESOLLaneMergeVerdict::TooSlow,
        false), FString(TEXT("Traffic 20.00 km/s   You -0.1 m/s   TOO SLOW")));
    TestEqual(TEXT("traffic 0.01 m/s: prints 0.0 m/s"), LaneMergeLine(0.01, 0.0, ESOLLaneMergeVerdict::Matched,
        false), FString(TEXT("Traffic 0.0 m/s   You 0.0 m/s   MATCHED")));

    // The worked example reads in the documented order
    const FString example = LaneMergeLine(182000.0, 175000.0, ESOLLaneMergeVerdict::Matched, true);
    const int32 trafficAt = example.Find(TEXT("Traffic "), ESearchCase::CaseSensitive);
    const int32 youAt = example.Find(TEXT("   You "), ESearchCase::CaseSensitive);
    const int32 verdictAt = example.Find(TEXT("   MATCHED"), ESearchCase::CaseSensitive);
    const int32 noteAt = example.Find(TEXT(" (at 1x)"), ESearchCase::CaseSensitive);
    TestTrue(TEXT("example: Traffic, You, verdict, note in order"), trafficAt == 0 && youAt > trafficAt
        && verdictAt > youAt && noteAt > verdictAt);
    TestTrue(TEXT("example: km/s speeds"), example.Contains(TEXT(" km/s"), ESearchCase::CaseSensitive));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeKeyTest, "SOLTest.LaneMergeReadout.TextKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 rebuild rule: the readout's key is equal exactly when its line would read the same (rebuilt only when the
// printed speeds, the verdict or the warp note change, keyed like the #49 label lines): over dense sweeps of both
// speeds (both signs, across the m/s - km/s unit boundary and the rounding steps) neighbouring samples have equal keys
// exactly when their lines are equal, and across a sparse sample every pair does
bool FSOLLaneMergeKeyTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::MakeMergeReadoutKey;
    int32 failures = 0;
    FString firstFailure;
    int32 equalPairs = 0;
    int32 differentPairs = 0;

    // Compares one pair of states: keys equal exactly when lines are equal
    const auto checkPair = [&](const double trafficA, const double playerA, const double trafficB,
        const double playerB)
    {
        const ESOLLaneMergeVerdict verdict = ESOLLaneMergeVerdict::TooSlow;
        const bool bLinesEqual = LaneMergeLine(trafficA, playerA, verdict, false)
            == LaneMergeLine(trafficB, playerB, verdict, false);
        const bool bKeysEqual = MakeMergeReadoutKey(trafficA, playerA, verdict, false)
            == MakeMergeReadoutKey(trafficB, playerB, verdict, false);
        (bLinesEqual ? equalPairs : differentPairs) += 1;
        if (bLinesEqual != bKeysEqual)
        {
            if (failures == 0)
            {
                firstFailure = FString::Printf(TEXT("(%.17g, %.17g) vs (%.17g, %.17g): lines %s, keys %s"), trafficA,
                    playerA, trafficB, playerB, bLinesEqual ? TEXT("equal") : TEXT("differ"),
                    bKeysEqual ? TEXT("equal") : TEXT("differ"));
            }
            ++failures;
        }
    };

    // Player speed sweep, both signs and zero, traffic fixed; then a traffic sweep, player fixed
    TArray<double> players;
    for (double magnitude = 0.5; magnitude < 4.0e6; magnitude *= 1.0005)
    {
        players.Add(magnitude);
        players.Add(-magnitude);
    }
    players.Add(0.0);
    for (const double centre : { 999.99, 1000.0, 1000.005, 20000.0, 20004.999, 182345.0, 299999.0 })
    {
        for (int32 index = -300; index <= 300; ++index)
        {
            players.Add(centre + 1.0e-3 * (static_cast<double>(index) + 0.37));
        }
    }
    players.Sort();
    for (int32 index = 1; index < players.Num(); ++index)
    {
        checkPair(21000.0, players[index - 1], 21000.0, players[index]);
    }
    TArray<double> traffics;
    for (double speed = 1.0e4; speed < 2.5e6; speed *= 1.0002)
    {
        traffics.Add(speed);
    }
    traffics.Sort();
    for (int32 index = 1; index < traffics.Num(); ++index)
    {
        checkPair(traffics[index - 1], 18000.0, traffics[index], 18000.0);
    }

    // Every pair of a sparse sample (so equal keys never stand for different lines far apart, e.g. another unit)
    TArray<double> sparse;
    for (int32 index = 0; index < players.Num(); index += players.Num() / 300)
    {
        sparse.Add(players[index]);
    }
    sparse.Add(999.0);
    sparse.Add(999.0e3);
    for (int32 first = 0; first < sparse.Num(); ++first)
    {
        for (int32 second = first + 1; second < sparse.Num(); ++second)
        {
            checkPair(21000.0, sparse[first], 21000.0, sparse[second]);
        }
    }
    AddInfo(FString::Printf(TEXT("%d equal pairs, %d different pairs"), equalPairs, differentPairs));
    TestTrue(TEXT("the sweep saw both equal and different neighbours"), equalPairs > 1000 && differentPairs > 1000);
    if (!TestEqual(TEXT("keys equal exactly when the lines are equal"), failures, 0))
    {
        AddInfo(TEXT("first: ") + firstFailure);
    }

    // The verdict and the warp note are part of the key
    const FSOLLaneMergeKey base = MakeMergeReadoutKey(20000.0, 20500.0, ESOLLaneMergeVerdict::Matched, false);
    TestTrue(TEXT("same state: equal keys"), base == MakeMergeReadoutKey(20000.0, 20500.0,
        ESOLLaneMergeVerdict::Matched, false));
    TestFalse(TEXT("another verdict: another key"), base == MakeMergeReadoutKey(20000.0, 20500.0,
        ESOLLaneMergeVerdict::TooFast, false));
    TestFalse(TEXT("the warp note: another key"), base == MakeMergeReadoutKey(20000.0, 20500.0,
        ESOLLaneMergeVerdict::Matched, true));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeColorTest, "SOLTest.LaneMergeReadout.BandColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 look: the readout is green when matched, amber when close and red when far, from the HUD palette constants
bool FSOLLaneMergeColorTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::MergeBandColor;
    TestTrue(TEXT("green: the palette's GOOD"), MergeBandColor(ESOLLaneMergeBand::Green).Equals(SOL::HudPalette::GOOD,
        1.0e-6f));
    TestTrue(TEXT("amber: the palette's AMBER"), MergeBandColor(ESOLLaneMergeBand::Amber).Equals(
        SOL::HudPalette::AMBER, 1.0e-6f));
    TestTrue(TEXT("red: the palette's BAD"), MergeBandColor(ESOLLaneMergeBand::Red).Equals(SOL::HudPalette::BAD,
        1.0e-6f));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeSpotTest, "SOLTest.LaneMergeReadout.Spot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 where (D18, Q10): the readout is the last line of its lane's placed tunnel label while that label grown by
// the line fits (the fourth line of the full form, the second of the compact one); when no label of its lane is placed,
// or it is placed but the grown label fits nowhere (bNoFit, the user's Q10 answer), it is the fixed line under the
// speed readout; nothing when the readout is hidden. Hysteresis: a label spot is left at once when the grown label
// stops fitting, and after a no-fit fallback line the readout returns to its label only once the grown label has fit
// for NPC_LANE_MERGE_LABEL_RETURN_S (a label newly placed takes it at once)
bool FSOLLaneMergeSpotTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::ChooseMergeReadoutSpot;
    const double returnS = SOL::NPC_LANE_MERGE_LABEL_RETURN_S;
    TestEqual(TEXT("return time 0.25 s"), returnS, 0.25);

    // Returns a placement, placed or not, in the full or compact form
    const auto placement = [](const bool bPlaced, const bool bCompact)
    {
        FSOLLaneLabelPlacement out;
        out.bPlaced = bPlaced;
        out.bCompact = bPlaced && bCompact;
        out.Candidate = bPlaced ? 0 : INDEX_NONE;
        if (bPlaced)
        {
            out.BoxPx = FBox2D(FVector2D(100.0, 100.0), FVector2D(280.0, 172.0));
        }
        return out;
    };

    // Returns whether a spot is at the given place, entry, lane and no-fit flag
    const auto isSpot = [](const FSOLLaneMergeSpot& spot, const ESOLLaneMergeWhere where, const int32 labelIndex,
        const int32 lane, const bool bNoFit)
    {
        return spot.Where == where && spot.LabelIndex == labelIndex && spot.Lane == lane && spot.bNoFit == bNoFit;
    };
    const int32 labelLanes[] = { 3, 0, 5 };
    const FSOLLaneLabelPlacement placements[] = { placement(true, false), placement(false, false),
        placement(true, true) };
    const FSOLLaneMergeSpot none;
    constexpr double STEP_S = 0.1;

    const FSOLLaneMergeSpot hidden = ChooseMergeReadoutSpot(false, 3, labelLanes, placements, true, none, STEP_S);
    TestTrue(TEXT("readout hidden: hidden, no label, no lane"), isSpot(hidden, ESOLLaneMergeWhere::Hidden, INDEX_NONE,
        INDEX_NONE, false));

    // Placed and the grown label fits: in the label at once (no earlier fallback line), in either form
    TestTrue(TEXT("lane 3, label placed, grown fits: label 0"), isSpot(ChooseMergeReadoutSpot(true, 3, labelLanes,
        placements, true, none, STEP_S), ESOLLaneMergeWhere::Label, 0, 3, false));
    TestTrue(TEXT("lane 5, compact label placed, grown fits: label 2"), isSpot(ChooseMergeReadoutSpot(true, 5,
        labelLanes, placements, true, none, STEP_S), ESOLLaneMergeWhere::Label, 2, 5, false));

    // No placed label of the lane: the fallback line (whatever bGrownFits says), not a no-fit
    TestTrue(TEXT("lane 0, label not placed: fallback"), isSpot(ChooseMergeReadoutSpot(true, 0, labelLanes,
        placements, true, none, STEP_S), ESOLLaneMergeWhere::Fallback, INDEX_NONE, 0, false));
    TestTrue(TEXT("lane 7, no label: fallback"), isSpot(ChooseMergeReadoutSpot(true, 7, labelLanes, placements, true,
        none, STEP_S), ESOLLaneMergeWhere::Fallback, INDEX_NONE, 7, false));
    TestTrue(TEXT("no labels at all: fallback"), isSpot(ChooseMergeReadoutSpot(true, 3, TConstArrayView<int32>(),
        TConstArrayView<FSOLLaneLabelPlacement>(), true, none, STEP_S), ESOLLaneMergeWhere::Fallback, INDEX_NONE, 3,
        false));
    TestTrue(TEXT("placements shorter than the lanes: fallback"), isSpot(ChooseMergeReadoutSpot(true, 5, labelLanes,
        TConstArrayView<FSOLLaneLabelPlacement>(placements, 2), true, none, STEP_S), ESOLLaneMergeWhere::Fallback,
        INDEX_NONE, 5, false));

    // A lane listed twice: the placed entry
    const int32 twice[] = { 4, 4 };
    const FSOLLaneLabelPlacement twicePlaced[] = { placement(false, false), placement(true, false) };
    TestTrue(TEXT("lane listed twice: the placed entry"), isSpot(ChooseMergeReadoutSpot(true, 4, twice, twicePlaced,
        true, none, STEP_S), ESOLLaneMergeWhere::Label, 1, 4, false));

    // Q10: placed, but the grown label fits nowhere: the fallback line with bNoFit, from any previous spot
    TestTrue(TEXT("Q10: placed, grown fits nowhere: fallback, no-fit"), isSpot(ChooseMergeReadoutSpot(true, 3,
        labelLanes, placements, false, none, STEP_S), ESOLLaneMergeWhere::Fallback, INDEX_NONE, 3, true));
    const FSOLLaneMergeSpot inLabel = ChooseMergeReadoutSpot(true, 3, labelLanes, placements, true, none, STEP_S);
    const FSOLLaneMergeSpot leave = ChooseMergeReadoutSpot(true, 3, labelLanes, placements, false, inLabel, STEP_S);
    TestTrue(TEXT("in the label, grown stops fitting: fallback at once"), isSpot(leave, ESOLLaneMergeWhere::Fallback,
        INDEX_NONE, 3, true));
    TestEqual(TEXT("in the label, grown stops fitting: no fit time"), leave.FitS, 0.0);
    TestTrue(TEXT("in the label, still fits: stays"), isSpot(ChooseMergeReadoutSpot(true, 3, labelLanes, placements,
        true, inLabel, STEP_S), ESOLLaneMergeWhere::Label, 0, 3, false));

    // Back from the fallback line: only after the grown label has fit for the return time (0.1, 0.2: stays; 0.3: back)
    const FSOLLaneMergeSpot wait1 = ChooseMergeReadoutSpot(true, 3, labelLanes, placements, true, leave, STEP_S);
    TestTrue(TEXT("fits again 0.1 s: still the fallback line"), isSpot(wait1, ESOLLaneMergeWhere::Fallback, INDEX_NONE,
        3, false));
    TestEqual(TEXT("fits again 0.1 s: fit time"), wait1.FitS, 0.1, 1.0e-12);
    const FSOLLaneMergeSpot wait2 = ChooseMergeReadoutSpot(true, 3, labelLanes, placements, true, wait1, STEP_S);
    TestTrue(TEXT("fits again 0.2 s: still the fallback line"), isSpot(wait2, ESOLLaneMergeWhere::Fallback, INDEX_NONE,
        3, false));
    const FSOLLaneMergeSpot back = ChooseMergeReadoutSpot(true, 3, labelLanes, placements, true, wait2, STEP_S);
    TestTrue(TEXT("fits again 0.3 s: back in the label"), isSpot(back, ESOLLaneMergeWhere::Label, 0, 3, false));
    TestEqual(TEXT("back in the label: fit time cleared"), back.FitS, 0.0);

    // A miss while waiting restarts the wait; one long frame returns at once
    const FSOLLaneMergeSpot missed = ChooseMergeReadoutSpot(true, 3, labelLanes, placements, false, wait2, STEP_S);
    TestTrue(TEXT("a miss while waiting: no-fit again"), isSpot(missed, ESOLLaneMergeWhere::Fallback, INDEX_NONE, 3,
        true));
    TestEqual(TEXT("a miss while waiting: the wait restarts"), missed.FitS, 0.0);
    TestTrue(TEXT("one 0.25 s frame: back in the label"), isSpot(ChooseMergeReadoutSpot(true, 3, labelLanes,
        placements, true, leave, returnS), ESOLLaneMergeWhere::Label, 0, 3, false));

    // No wait after a plain fallback line (no label placed: the label newly appearing), nor after another lane's
    const FSOLLaneMergeSpot noLabel = ChooseMergeReadoutSpot(true, 3, TConstArrayView<int32>(),
        TConstArrayView<FSOLLaneLabelPlacement>(), true, none, STEP_S);
    TestTrue(TEXT("label appears after a plain fallback line: in the label at once"), isSpot(ChooseMergeReadoutSpot(
        true, 3, labelLanes, placements, true, noLabel, STEP_S), ESOLLaneMergeWhere::Label, 0, 3, false));
    const FSOLLaneMergeSpot otherLane = ChooseMergeReadoutSpot(true, 5, labelLanes, placements, false, none, STEP_S);
    TestTrue(TEXT("after another lane's no-fit: in the label at once"), isSpot(ChooseMergeReadoutSpot(true, 3,
        labelLanes, placements, true, otherLane, STEP_S), ESOLLaneMergeWhere::Label, 0, 3, false));

    // A NaN or negative step adds nothing
    TestEqual(TEXT("NaN step: no fit time added"), ChooseMergeReadoutSpot(true, 3, labelLanes, placements, true,
        wait1, LANEMERGE_NAN).FitS, 0.1, 1.0e-12);
    TestEqual(TEXT("negative step: no fit time added"), ChooseMergeReadoutSpot(true, 3, labelLanes, placements, true,
        wait1, -1.0).FitS, 0.1, 1.0e-12);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeFallbackSpotTest, "SOLTest.LaneMergeReadout.FallbackSpot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The fallback line's spot (D18): the first of its spots (right of the speed card under the large speed readout, on
// that readout's row, above the card) on the viewport, clear of every drawn label grown by the gap and of every HUD
// panel (the radar at a large HUD scale); else the last spot (above the card)
bool FSOLLaneMergeFallbackSpotTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::ChooseMergeFallbackSpot;
    const FVector2D viewport(1280.0, 720.0);
    constexpr double GAP_PX = 6.0;
    const FBox2D spots[] = {
        FBox2D(FVector2D(400.0, 600.0), FVector2D(700.0, 620.0)),
        FBox2D(FVector2D(400.0, 560.0), FVector2D(700.0, 580.0)),
        FBox2D(FVector2D(20.0, 500.0), FVector2D(320.0, 520.0)),
    };
    const TConstArrayView<FBox2D> noBoxes;
    TestEqual(TEXT("nothing in the way: under the large speed readout"), ChooseMergeFallbackSpot(spots, noBoxes,
        noBoxes, viewport, GAP_PX), 0);

    // A label over the first spot, or within the gap of it: the speed readout's row; beyond the gap: the first
    const FBox2D overFirst(FVector2D(500.0, 590.0), FVector2D(650.0, 610.0));
    TestEqual(TEXT("a label over the first spot: the second"), ChooseMergeFallbackSpot(spots,
        TConstArrayView<FBox2D>(&overFirst, 1), noBoxes, viewport, GAP_PX), 1);
    const FBox2D nearFirst(FVector2D(705.0, 600.0), FVector2D(800.0, 620.0));
    TestEqual(TEXT("a label 5 px right of the first spot (gap 6): the second"), ChooseMergeFallbackSpot(spots,
        TConstArrayView<FBox2D>(&nearFirst, 1), noBoxes, viewport, GAP_PX), 1);
    TestEqual(TEXT("the same label with a 4 px gap: the first"), ChooseMergeFallbackSpot(spots,
        TConstArrayView<FBox2D>(&nearFirst, 1), noBoxes, viewport, 4.0), 0);

    // A HUD panel (the radar) over both spots right of the card: above the card; touching a panel counts
    const FBox2D radar(FVector2D(650.0, 540.0), FVector2D(1270.0, 710.0));
    TestEqual(TEXT("the radar over both right spots: above the card"), ChooseMergeFallbackSpot(spots, noBoxes,
        TConstArrayView<FBox2D>(&radar, 1), viewport, GAP_PX), 2);
    const FBox2D touching(FVector2D(700.0, 600.0), FVector2D(800.0, 620.0));
    TestEqual(TEXT("a panel touching the first spot: the second"), ChooseMergeFallbackSpot(spots, noBoxes,
        TConstArrayView<FBox2D>(&touching, 1), viewport, GAP_PX), 1);

    // Off the viewport's right edge (a narrow viewport, a large HUD scale): above the card
    TestEqual(TEXT("both right spots off the right edge: above the card"), ChooseMergeFallbackSpot(spots, noBoxes,
        noBoxes, FVector2D(650.0, 720.0), GAP_PX), 2);

    // Nothing clear: the last spot; invalid boxes ignored
    const FBox2D everywhere[] = { FBox2D(FVector2D(0.0, 0.0), FVector2D(1280.0, 720.0)) };
    TestEqual(TEXT("every spot covered: the last"), ChooseMergeFallbackSpot(spots, everywhere, everywhere, viewport,
        GAP_PX), 2);
    const FBox2D invalid(ForceInit);
    TestEqual(TEXT("invalid boxes ignored"), ChooseMergeFallbackSpot(spots, TConstArrayView<FBox2D>(&invalid, 1),
        TConstArrayView<FBox2D>(&invalid, 1), viewport, GAP_PX), 0);

    // No spots or a bad viewport: none
    TestEqual(TEXT("no spots: none"), ChooseMergeFallbackSpot(noBoxes, noBoxes, noBoxes, viewport, GAP_PX),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("zero viewport: none"), ChooseMergeFallbackSpot(spots, noBoxes, noBoxes, FVector2D::ZeroVector,
        GAP_PX), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("NaN viewport: none"), ChooseMergeFallbackSpot(spots, noBoxes, noBoxes,
        FVector2D(LANEMERGE_NAN, 720.0), GAP_PX), static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeEnabledTest, "SOLTest.LaneMergeReadout.Enabled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13 show conditions (D8): computed only while the HUD allows it (a view, no map; menus, warp jumps and a hidden
// HUD never reach it), the Lanes row is on (no settings read as on; the verification override ignores it), a lane
// table exists (a population) and the player has a ship
bool FSOLLaneMergeEnabledTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::IsMergeReadoutEnabled;
    TestTrue(TEXT("everything on: shown"), IsMergeReadoutEnabled(true, false, true, true, true, true));
    TestFalse(TEXT("map or no view: hidden"), IsMergeReadoutEnabled(false, false, true, true, true, true));
    TestFalse(TEXT("Lanes row off: hidden"), IsMergeReadoutEnabled(true, false, true, false, true, true));
    TestTrue(TEXT("Lanes row off, verification override: shown"), IsMergeReadoutEnabled(true, true, true, false, true,
        true));
    TestTrue(TEXT("no settings: shown"), IsMergeReadoutEnabled(true, false, false, false, true, true));
    TestFalse(TEXT("no lane table: hidden"), IsMergeReadoutEnabled(true, true, true, true, false, true));
    TestFalse(TEXT("no player ship: hidden"), IsMergeReadoutEnabled(true, true, true, true, true, false));
    TestFalse(TEXT("override, but the map: hidden"), IsMergeReadoutEnabled(false, true, false, true, true, true));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeLaneTableTest, "SOLTest.LaneMergeReadout.LaneTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The lane table (SDD 50 section 4.10): SetLanes writes one entry per lane in lane order (the shortest list sets the
// count) and replaces the previous table, ClearLanes empties it; the readout's verification override toggles
bool FSOLLaneMergeLaneTableTest::RunTest(const FString& /*parameters*/)
{
    const TStrongObjectPtr<USOLLaneTrafficSubsystem> table(NewObject<USOLLaneTrafficSubsystem>(GetTransientPackage()));
    if (!TestTrue(TEXT("the subsystem object exists"), table.IsValid()))
    {
        return false;
    }
    TestEqual(TEXT("new: empty"), table->GetLanes().Num(), 0);
    TestFalse(TEXT("new: no override"), table->IsReadoutVerificationOverride());

    const FSOLLaneProfile profileA = LaneMergeNominalLane().Profile;
    const FSOLLaneProfile profileB = LaneMergeShortLane().Profile;
    const int32 bodyA[] = { 3, 3, 4 };
    const int32 bodyB[] = { 4, 9, 9 };
    const FSOLLaneProfile profiles[] = { profileA, profileB };
    table->SetLanes(bodyA, bodyB, profiles);
    if (TestEqual(TEXT("set: the shortest list's count"), table->GetLanes().Num(), 2))
    {
        TestEqual(TEXT("lane 0: body A"), table->GetLanes()[0].BodyA, 3);
        TestEqual(TEXT("lane 0: body B"), table->GetLanes()[0].BodyB, 4);
        TestEqual(TEXT("lane 1: body B"), table->GetLanes()[1].BodyB, 9);
        TestEqual(TEXT("lane 0: its profile"), table->GetLanes()[0].Profile.WallRadiusM, profileA.WallRadiusM);
        TestEqual(TEXT("lane 1: its profile"), table->GetLanes()[1].Profile.NominalLegM, profileB.NominalLegM);
    }

    // Set again: replaced, not appended
    const int32 oneA[] = { 7 };
    const int32 oneB[] = { 8 };
    const FSOLLaneProfile oneProfile[] = { profileB };
    table->SetLanes(oneA, oneB, oneProfile);
    TestEqual(TEXT("set again: replaced"), table->GetLanes().Num(), 1);
    TestEqual(TEXT("set again: the new lane"), table->GetLanes().IsEmpty() ? INDEX_NONE : table->GetLanes()[0].BodyA,
        7);

    // Clear, and the override
    table->ClearLanes();
    TestEqual(TEXT("cleared: empty"), table->GetLanes().Num(), 0);
    table->SetReadoutVerificationOverride(true);
    TestTrue(TEXT("override on"), table->IsReadoutVerificationOverride());
    table->SetReadoutVerificationOverride(false);
    TestFalse(TEXT("override off"), table->IsReadoutVerificationOverride());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeLabelSizeTest, "SOLTest.LaneMergeReadout.LabelSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The label box grows by one line for the readout (SDD 50 section 4.8): as wide as the wider of the label and the line,
// one line height taller; a bad line size leaves the box unchanged
bool FSOLLaneMergeLabelSizeTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::LaneLabelSizeWithLine;
    const FVector2D size(180.0, 54.0);
    TestTrue(TEXT("a wider line widens the box"), LaneLabelSizeWithLine(size, 240.0, 18.0).Equals(
        FVector2D(240.0, 72.0), LANEMERGE_TOL));
    TestTrue(TEXT("a narrower line keeps the width"), LaneLabelSizeWithLine(size, 100.0, 18.0).Equals(
        FVector2D(180.0, 72.0), LANEMERGE_TOL));
    TestTrue(TEXT("an empty line adds nothing"), LaneLabelSizeWithLine(size, 0.0, 0.0).Equals(size, LANEMERGE_TOL));
    for (const FVector2D& bad : { FVector2D(LANEMERGE_NAN, 18.0), FVector2D(240.0, LANEMERGE_INF),
        FVector2D(-5.0, 18.0), FVector2D(240.0, -18.0) })
    {
        TestTrue(FString::Printf(TEXT("line (%f, %f): unchanged"), bad.X, bad.Y),
            LaneLabelSizeWithLine(size, bad.X, bad.Y).Equals(size, LANEMERGE_TOL));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneMergeLabelWithLineTest, "SOLTest.LaneMergeReadout.LabelWithLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The readout label grown by the line (D18, the user's 50c decision): at the candidate the label took (bottom edge on
// a 12 o'clock anchor, top edge on a 6 o'clock one, centred), the full form first, as wide as the wider line; when the
// centred box leaves the viewport or meets an avoid box (a HUD panel or another label) grown by the gap it slides
// sideways as little as possible while still covering the form's own box (OwnBoxPx, where its lines go), else it tries
// the next spot (the full form at the other candidate, then the compact form); it is not placed when nothing fits or
// the label was not placed. The dot rows are never tested
bool FSOLLaneMergeLabelWithLineTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::PlaceLaneLabelWithLine;
    const FVector2D viewport(1920.0, 1080.0);
    constexpr double GAP_PX = 8.0;
    constexpr double LINE_W = 240.0;
    constexpr double LINE_H = 18.0;

    // A label with both anchors on screen, its full box placed above the 12 o'clock anchor
    FSOLLaneLabelInput label;
    label.TopAnchorPx = FVector2D(960.0, 500.0);
    label.BottomAnchorPx = FVector2D(960.0, 700.0);
    label.SizePx = FVector2D(180.0, 54.0);
    label.CompactSizePx = FVector2D(150.0, 18.0);
    label.bTopInFront = true;
    label.bBottomInFront = true;
    FSOLLaneLabelPlacement placed;
    placed.bPlaced = true;
    placed.Candidate = 0;
    placed.BoxPx = FBox2D(FVector2D(870.0, 446.0), FVector2D(1050.0, 500.0));

    // Returns whether a placement is placed in the given form at the given candidate and box
    const auto isAt = [](const FSOLLaneLabelPlacement& out, const int32 candidate, const bool bCompact,
        const FBox2D& box)
    {
        return out.bPlaced && out.Candidate == candidate && out.bCompact == bCompact
            && out.BoxPx.Min.Equals(box.Min, LANEMERGE_TOL) && out.BoxPx.Max.Equals(box.Max, LANEMERGE_TOL);
    };

    // Same spot and form, one line taller (upward from the anchor) and as wide as the line
    const FSOLLaneLabelPlacement same = PlaceLaneLabelWithLine(label, placed, LINE_W, LINE_H,
        TConstArrayView<FBox2D>(), viewport, GAP_PX);
    TestTrue(TEXT("grown at the same spot: 4 lines above the 12 o'clock anchor"), isAt(same, 0, false,
        FBox2D(FVector2D(840.0, 428.0), FVector2D(1080.0, 500.0))));
    TestTrue(TEXT("grown at the same spot: its own lines centred on the anchor at the box's top"),
        same.OwnBoxPx.Min.Equals(FVector2D(870.0, 428.0), LANEMERGE_TOL)
        && same.OwnBoxPx.Max.Equals(FVector2D(1050.0, 482.0), LANEMERGE_TOL));

    // Another label over the grown part's right end only: slid left just clear of it (left edge 1085 - 8 - 240 = 837),
    // still covering the label's own box (870 - 1050)
    const FBox2D other(FVector2D(1085.0, 430.0), FVector2D(1200.0, 440.0));
    const FSOLLaneLabelPlacement slid = PlaceLaneLabelWithLine(label, placed, LINE_W, LINE_H,
        TConstArrayView<FBox2D>(&other, 1), viewport, GAP_PX);
    TestTrue(TEXT("another label over the grown box's end: slid left just clear"), isAt(slid, 0, false,
        FBox2D(FVector2D(837.0, 428.0), FVector2D(1077.0, 500.0))));
    TestTrue(TEXT("slid: its own lines stay centred on the anchor"), slid.OwnBoxPx.Min.Equals(FVector2D(870.0, 428.0),
        LANEMERGE_TOL) && slid.OwnBoxPx.Max.Equals(FVector2D(1050.0, 482.0), LANEMERGE_TOL));

    // A label across every slid position (the slide may not uncover the own box): the 6 o'clock spot, growing down
    const FBox2D wide(FVector2D(800.0, 430.0), FVector2D(1100.0, 440.0));
    const FSOLLaneLabelPlacement moved = PlaceLaneLabelWithLine(label, placed, LINE_W, LINE_H,
        TConstArrayView<FBox2D>(&wide, 1), viewport, GAP_PX);
    TestTrue(TEXT("another label across every slide: 6 o'clock"), isAt(moved, 1, false,
        FBox2D(FVector2D(840.0, 700.0), FVector2D(1080.0, 772.0))));

    // The #49 T4b view: a compact label at 6 o'clock just right of the speed card. The full form (300 px wide, tried
    // first) cannot clear the card while covering its own box (left edge at most 396 < 398 + 8), so the compact form
    // grown slides right until it touches the card's gap (left edge 406, own box 444 - 648)
    FSOLLaneLabelInput beside;
    beside.TopAnchorPx = FVector2D(546.0, 900.0);
    beside.BottomAnchorPx = FVector2D(546.0, 928.0);
    beside.SizePx = FVector2D(300.0, 69.0);
    beside.CompactSizePx = FVector2D(204.0, 23.0);
    beside.bBottomInFront = true;
    FSOLLaneLabelPlacement placedBeside;
    placedBeside.bPlaced = true;
    placedBeside.bCompact = true;
    placedBeside.Candidate = 1;
    placedBeside.BoxPx = FBox2D(FVector2D(444.0, 928.0), FVector2D(648.0, 951.0));
    const FBox2D speedCard(FVector2D(10.0, 815.0), FVector2D(398.0, 1065.0));
    const FSOLLaneLabelPlacement t4b = PlaceLaneLabelWithLine(beside, placedBeside, 404.0, 22.0,
        TConstArrayView<FBox2D>(&speedCard, 1), viewport, GAP_PX);
    TestTrue(TEXT("T4b: the compact form slid right off the speed card, still over the label"), isAt(t4b, 1, true,
        FBox2D(FVector2D(406.0, 928.0), FVector2D(810.0, 973.0))));
    TestTrue(TEXT("T4b: its own line centred on the anchor under it"), t4b.OwnBoxPx.Min.Equals(FVector2D(444.0, 928.0),
        LANEMERGE_TOL) && t4b.OwnBoxPx.Max.Equals(FVector2D(648.0, 951.0), LANEMERGE_TOL));

    // An invalid avoid box is ignored
    const FBox2D invalid(ForceInit);
    TestTrue(TEXT("an invalid avoid box is ignored"), isAt(PlaceLaneLabelWithLine(label, placed, LINE_W, LINE_H,
        TConstArrayView<FBox2D>(&invalid, 1), viewport, GAP_PX), 0, false,
        FBox2D(FVector2D(840.0, 428.0), FVector2D(1080.0, 500.0))));

    // A compact label: the full form grown when it fits (a label is not held compact by its readout)
    FSOLLaneLabelPlacement placedCompact = placed;
    placedCompact.bCompact = true;
    placedCompact.BoxPx = FBox2D(FVector2D(885.0, 482.0), FVector2D(1035.0, 500.0));
    TestTrue(TEXT("compact placed, the full form fits: the full form grown"), isAt(PlaceLaneLabelWithLine(label,
        placedCompact, LINE_W, LINE_H, TConstArrayView<FBox2D>(), viewport, GAP_PX), 0, false,
        FBox2D(FVector2D(840.0, 428.0), FVector2D(1080.0, 500.0))));

    // The full form blocked at both anchors: the compact form, the readout its second line, at the spot it took
    const FBox2D blockers[] = { FBox2D(FVector2D(0.0, 400.0), FVector2D(1920.0, 440.0)),
        FBox2D(FVector2D(0.0, 760.0), FVector2D(1920.0, 800.0)) };
    const FSOLLaneLabelPlacement compact = PlaceLaneLabelWithLine(label, placedCompact, LINE_W, LINE_H, blockers,
        viewport, GAP_PX);
    TestTrue(TEXT("full form blocked at both anchors: two lines at 12 o'clock"), isAt(compact, 0, true,
        FBox2D(FVector2D(840.0, 464.0), FVector2D(1080.0, 500.0))));
    TestTrue(TEXT("compact: its own line centred on the anchor"), compact.OwnBoxPx.Min.Equals(FVector2D(885.0, 464.0),
        LANEMERGE_TOL) && compact.OwnBoxPx.Max.Equals(FVector2D(1035.0, 482.0), LANEMERGE_TOL));

    // Compact placed but no valid compact size: the full form grown
    FSOLLaneLabelInput noCompact = label;
    noCompact.CompactSizePx = FVector2D::ZeroVector;
    TestTrue(TEXT("no compact size: the full form"), isAt(PlaceLaneLabelWithLine(noCompact, placedCompact, LINE_W,
        LINE_H, TConstArrayView<FBox2D>(), viewport, GAP_PX), 0, false,
        FBox2D(FVector2D(840.0, 428.0), FVector2D(1080.0, 500.0))));

    // Placed at 6 o'clock: grown downward from that anchor
    FSOLLaneLabelPlacement placedBottom = placed;
    placedBottom.Candidate = 1;
    placedBottom.BoxPx = FBox2D(FVector2D(870.0, 700.0), FVector2D(1050.0, 754.0));
    TestTrue(TEXT("6 o'clock: grown downward"), isAt(PlaceLaneLabelWithLine(label, placedBottom, LINE_W, LINE_H,
        TConstArrayView<FBox2D>(), viewport, GAP_PX), 1, false,
        FBox2D(FVector2D(840.0, 700.0), FVector2D(1080.0, 772.0))));

    // Too wide for the viewport at either anchor (the line centred 60 px from the left edge): not placed
    FSOLLaneLabelInput atEdge = label;
    atEdge.TopAnchorPx.X = 60.0;
    atEdge.BottomAnchorPx.X = 60.0;
    TestFalse(TEXT("off the viewport in every form: not placed"), PlaceLaneLabelWithLine(atEdge, placed, LINE_W,
        LINE_H, TConstArrayView<FBox2D>(), viewport, GAP_PX).bPlaced);

    // A label that was not placed stays unplaced
    TestFalse(TEXT("label not placed: not placed"), PlaceLaneLabelWithLine(label, FSOLLaneLabelPlacement(), LINE_W,
        LINE_H, TConstArrayView<FBox2D>(), viewport, GAP_PX).bPlaced);

    // A bad line leaves the size alone: the label as placed
    TestTrue(TEXT("a NaN line: the label as placed"), isAt(PlaceLaneLabelWithLine(label, placed, LANEMERGE_NAN, LINE_H,
        TConstArrayView<FBox2D>(), viewport, GAP_PX), 0, false, placed.BoxPx));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
