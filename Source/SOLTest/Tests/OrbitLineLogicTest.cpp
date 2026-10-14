/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineLogic.h"
#include "OrbitLines/SOLOrbitLineMath.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"

#include "SOLConstants.h"
#include "Universe/SOLAnchorSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Written against the orbit-line logic contract (SDD 17), not the implementation: the target key of a targeting
// candidate, the round-robin drift-rebuild selection and cursor, the visuals actor's toggle in a bare world, the
// orbit orientation and line rotation, the shape-only rebuild rule, the scene-depth trust limit and line visibility.
// Helpers here carry names unique across the test files so unity builds do not collide.
namespace
{
    // Returns the slots a selection returned, as a string for test messages
    FString OrbitLineLogicSlotsText(const TArray<int32>& slots)
    {
        TArray<FString> parts;
        for (const int32 slot : slots)
        {
            parts.Add(FString::FromInt(slot));
        }
        return FString::Printf(TEXT("{%s}"), *FString::Join(parts, TEXT(", ")));
    }

    // Returns whether the selection equals the expected slots in order
    bool OrbitLineLogicSlotsEqual(const TArray<int32>& slots, std::initializer_list<int32> expected)
    {
        return slots == TArray<int32>(expected);
    }

    constexpr double ORBIT_LINE_LOGIC_TEST_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double ORBIT_LINE_LOGIC_TEST_INF = std::numeric_limits<double>::infinity();

    // Returns Kepler elements from the given values (metres, radians)
    FSOLKeplerElements OrbitLineLogicElements(double semiMajorAxisM, double eccentricity, double inclinationRad,
        double nodeRad, double argumentRad, double meanAnomalyRad)
    {
        FSOLKeplerElements elements;
        elements.SemiMajorAxisM = semiMajorAxisM;
        elements.Eccentricity = eccentricity;
        elements.InclinationRad = inclinationRad;
        elements.LongitudeOfAscendingNodeRad = nodeRad;
        elements.ArgumentOfPeriapsisRad = argumentRad;
        elements.MeanAnomalyRad = meanAnomalyRad;
        return elements;
    }

    // Returns the periapsis unit vector P of the elements' orientation, typed from the textbook formula
    FVector3d OrbitLineLogicPeriapsisAxis(const FSOLKeplerElements& elements)
    {
        const double cn = FMath::Cos(elements.LongitudeOfAscendingNodeRad);
        const double sn = FMath::Sin(elements.LongitudeOfAscendingNodeRad);
        const double cw = FMath::Cos(elements.ArgumentOfPeriapsisRad);
        const double sw = FMath::Sin(elements.ArgumentOfPeriapsisRad);
        const double ci = FMath::Cos(elements.InclinationRad);
        const double si = FMath::Sin(elements.InclinationRad);
        return FVector3d(cn * cw - sn * sw * ci, sn * cw + cn * sw * ci, sw * si);
    }

    // Returns the in-plane unit vector Q (90 degrees prograde of periapsis), typed from the textbook formula
    FVector3d OrbitLineLogicQuadratureAxis(const FSOLKeplerElements& elements)
    {
        const double cn = FMath::Cos(elements.LongitudeOfAscendingNodeRad);
        const double sn = FMath::Sin(elements.LongitudeOfAscendingNodeRad);
        const double cw = FMath::Cos(elements.ArgumentOfPeriapsisRad);
        const double sw = FMath::Sin(elements.ArgumentOfPeriapsisRad);
        const double ci = FMath::Cos(elements.InclinationRad);
        const double si = FMath::Sin(elements.InclinationRad);
        return FVector3d(-cn * sw - sn * cw * ci, -sn * sw + cn * cw * ci, cw * si);
    }

    // Returns the orbit pole (unit angular momentum direction), typed from the textbook formula
    FVector3d OrbitLineLogicPoleAxis(const FSOLKeplerElements& elements)
    {
        const double cn = FMath::Cos(elements.LongitudeOfAscendingNodeRad);
        const double sn = FMath::Sin(elements.LongitudeOfAscendingNodeRad);
        const double si = FMath::Sin(elements.InclinationRad);
        return FVector3d(sn * si, -cn * si, FMath::Cos(elements.InclinationRad));
    }

    // Returns a quaternion's rotation angle in radians, independent of its sign and accurate near the identity
    double OrbitLineLogicQuatAngle(const FQuat4d& q)
    {
        const double vectorLength = FMath::Sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z);
        return 2.0 * FMath::Atan2(vectorLength, FMath::Abs(q.W));
    }

    // Returns a quaternion's norm
    double OrbitLineLogicQuatNorm(const FQuat4d& q)
    {
        return FMath::Sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
    }

    // Returns the expected shape drift bound, typed from the contract formula
    double OrbitLineLogicExpectedShapeDrift(const FSOLKeplerElements& built, const FSOLKeplerElements& now)
    {
        const double aMax = FMath::Max(built.SemiMajorAxisM, now.SemiMajorAxisM);
        const double eMax = FMath::Max(built.Eccentricity, now.Eccentricity);
        return FMath::Abs(now.SemiMajorAxisM - built.SemiMajorAxisM) * (1.0 + eMax)
            + aMax * FMath::Abs(now.Eccentricity - built.Eccentricity)
            * (1.0 + eMax / FMath::Sqrt(1.0 - eMax * eMax));
    }

    // Returns the log-compressed render depth (cm) of a true distance (cm) beyond the 1:1 zone, typed independently
    double OrbitLineLogicCompressedDepthCm(double trueDistanceCm, double maxRenderDistanceCm)
    {
        if (trueDistanceCm <= maxRenderDistanceCm)
        {
            return trueDistanceCm;
        }
        return maxRenderDistanceCm * (1.0 + 0.1 * FMath::Loge(trueDistanceCm / maxRenderDistanceCm));
    }

    // A temporary game-preview world with begun play: no SOL subsystems (they only exist in SOL game worlds), no game
    // mode and no game instance; destroyed at scope exit
    struct FSOLOrbitLineLogicTestWorld
    {
        UWorld* World = nullptr;

        //////////////////////////////////////////////////////////////////////////
        // Creates the world and its context and marks play as begun so spawned actors run BeginPlay
        FSOLOrbitLineLogicTestWorld()
        {
            World = UWorld::CreateWorld(EWorldType::GamePreview, false, TEXT("SOLOrbitLineLogicTestWorld"));
            FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::GamePreview);
            context.SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
            World->SetBegunPlay(true);
        }

        //////////////////////////////////////////////////////////////////////////
        // Ends play on every actor, then destroys the world and its context
        ~FSOLOrbitLineLogicTestWorld()
        {
            for (TActorIterator<AActor> it(World); it; ++it)
            {
                it->RouteEndPlay(EEndPlayReason::Quit);
            }
            World->SetBegunPlay(false);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicTargetKeyTest, "SOLTest.OrbitLineLogic.TargetKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A registry candidate maps to its body's key with an orbit only when it has a parent; anything else is None
bool FSOLOrbitLineLogicTargetKeyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOrbitLineMath;
    using SOLOrbitLineLogic::TargetKeyForCandidate;
    // Sun (no parent), Earth (Sun), Moon (Earth)
    const TArray<int32> parents = { INDEX_NONE, 0, 1 };
    const FLineKey none;

    bool bHasOrbit = true;
    FLineKey key = TargetKeyForCandidate(0, parents, bHasOrbit);
    TestTrue(TEXT("root body key"), key == FLineKey{ ELineSource::RegistryBody, 0 });
    TestFalse(TEXT("root body has no orbit"), bHasOrbit);

    bHasOrbit = false;
    key = TargetKeyForCandidate(1, parents, bHasOrbit);
    TestTrue(TEXT("planet key"), key == FLineKey{ ELineSource::RegistryBody, 1 });
    TestTrue(TEXT("planet has an orbit"), bHasOrbit);

    bHasOrbit = false;
    key = TargetKeyForCandidate(2, parents, bHasOrbit);
    TestTrue(TEXT("moon key"), key == FLineKey{ ELineSource::RegistryBody, 2 });
    TestTrue(TEXT("moon has an orbit"), bHasOrbit);

    // Non-registry candidates (past the bodies), no selection and negatives give None and no orbit
    const int32 invalidCandidates[] = { 3, 17, INDEX_NONE, -5 };
    for (const int32 candidate : invalidCandidates)
    {
        bHasOrbit = true;
        key = TargetKeyForCandidate(candidate, parents, bHasOrbit);
        const FString label = FString::Printf(TEXT("candidate %d"), candidate);
        TestTrue(label + TEXT(" is None"), key == none);
        TestTrue(label + TEXT(" source None"), key.Source == ELineSource::None);
        TestEqual(label + TEXT(" index none"), key.Index, static_cast<int32>(INDEX_NONE));
        TestFalse(label + TEXT(" has no orbit"), bHasOrbit);
    }

    // Empty parents: even candidate 0 is not a registry body
    bHasOrbit = true;
    key = TargetKeyForCandidate(0, TConstArrayView<int32>(), bHasOrbit);
    TestTrue(TEXT("empty parents gives None"), key == none);
    TestFalse(TEXT("empty parents has no orbit"), bHasOrbit);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicSelectRebuildsTest, "SOLTest.OrbitLineLogic.SelectRebuilds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The drift selection scans N slots from the wrapped start, in scan order, capped at maxRebuilds
bool FSOLOrbitLineLogicSelectRebuildsTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::SelectDriftRebuilds;
    const TArray<bool> flags = { true, false, true, true, false };
    TArray<int32> slots;

    // The contract's example
    int32 count = SelectDriftRebuilds(flags, 3, 2, slots);
    TestEqual(TEXT("start 3 max 2: count"), count, 2);
    TestTrue(TEXT("start 3 max 2: {3, 0} got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 3, 0 }));

    // Uncapped scans visit each flagged slot once, in scan order from the start
    count = SelectDriftRebuilds(flags, 0, 10, slots);
    TestEqual(TEXT("start 0: count"), count, 3);
    TestTrue(TEXT("start 0: {0, 2, 3} got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 0, 2, 3 }));
    count = SelectDriftRebuilds(flags, 4, 10, slots);
    TestEqual(TEXT("start 4: count"), count, 3);
    TestTrue(TEXT("start 4: {0, 2, 3} got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 0, 2, 3 }));
    count = SelectDriftRebuilds(flags, 1, 10, slots);
    TestTrue(TEXT("start 1: {2, 3, 0} got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 2, 3, 0 }));
    TestEqual(TEXT("start 1: returns the count"), count, slots.Num());

    // Starts outside [0, N) wrap: -1 is N-1, N+3 is 3, -N-2 is N-2
    count = SelectDriftRebuilds(flags, -1, 10, slots);
    TestTrue(TEXT("start -1 like start 4, got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 0, 2, 3 }));
    count = SelectDriftRebuilds(flags, 8, 2, slots);
    TestTrue(TEXT("start 8 like start 3, got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 3, 0 }));
    count = SelectDriftRebuilds(flags, -7, 1, slots);
    TestTrue(TEXT("start -7 like start 3, got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 3 }));
    TestEqual(TEXT("start -7 max 1: count"), count, 1);

    // The cap is exact
    count = SelectDriftRebuilds(flags, 0, 1, slots);
    TestTrue(TEXT("max 1: {0} got ") + OrbitLineLogicSlotsText(slots), OrbitLineLogicSlotsEqual(slots, { 0 }));
    TestEqual(TEXT("max 1: count"), count, 1);

    // Nothing flagged: nothing selected
    const TArray<bool> clean = { false, false, false };
    count = SelectDriftRebuilds(clean, 1, 4, slots);
    TestEqual(TEXT("nothing flagged: count"), count, 0);
    TestEqual(TEXT("nothing flagged: empty"), slots.Num(), 0);

    // Previous contents are reset, capacity is kept
    slots.Reset();
    slots.Reserve(32);
    slots.Add(99);
    slots.Add(98);
    count = SelectDriftRebuilds(flags, 2, 1, slots);
    TestTrue(TEXT("reset before filling, got ") + OrbitLineLogicSlotsText(slots),
        OrbitLineLogicSlotsEqual(slots, { 2 }));
    TestTrue(TEXT("capacity kept"), slots.Max() >= 32);

    // No lines or no budget: 0 and empty
    slots.Add(7);
    count = SelectDriftRebuilds(TConstArrayView<bool>(), 0, 4, slots);
    TestEqual(TEXT("no lines: count"), count, 0);
    TestEqual(TEXT("no lines: empty"), slots.Num(), 0);
    slots.Add(7);
    count = SelectDriftRebuilds(flags, 0, 0, slots);
    TestEqual(TEXT("max 0: count"), count, 0);
    TestEqual(TEXT("max 0: empty"), slots.Num(), 0);
    slots.Add(7);
    count = SelectDriftRebuilds(flags, 0, -3, slots);
    TestEqual(TEXT("negative max: count"), count, 0);
    TestEqual(TEXT("negative max: empty"), slots.Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicNextCursorTest, "SOLTest.OrbitLineLogic.NextCursor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rebuild cursor steps by one and wraps into [0, lineCount); no lines gives 0
bool FSOLOrbitLineLogicNextCursorTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::NextRebuildCursor;
    TestEqual(TEXT("0 -> 1"), NextRebuildCursor(0, 5), 1);
    TestEqual(TEXT("3 -> 4"), NextRebuildCursor(3, 5), 4);
    TestEqual(TEXT("last wraps to 0"), NextRebuildCursor(4, 5), 0);
    TestEqual(TEXT("past the end wraps"), NextRebuildCursor(7, 5), 3);
    TestEqual(TEXT("-1 -> 0"), NextRebuildCursor(-1, 5), 0);
    TestEqual(TEXT("-3 -> 3"), NextRebuildCursor(-3, 5), 3);
    TestEqual(TEXT("-8 -> 3"), NextRebuildCursor(-8, 5), 3);
    TestEqual(TEXT("one line stays 0"), NextRebuildCursor(0, 1), 0);
    TestEqual(TEXT("one line, any cursor is 0"), NextRebuildCursor(41, 1), 0);
    TestEqual(TEXT("no lines"), NextRebuildCursor(3, 0), 0);
    TestEqual(TEXT("negative line count"), NextRebuildCursor(3, -2), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicDriftCapTest, "SOLTest.OrbitLineLogic.DriftCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With every line drifting, each frame rebuilds at most the per-frame cap and the round robin reaches every line
bool FSOLOrbitLineLogicDriftCapTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOrbitLineLogic;
    constexpr int32 lineCount = 10;
    const int32 cap = SOL::ORBIT_LINE_MAX_REBUILDS_PER_FRAME;
    TestEqual(TEXT("cap is 4"), cap, 4);

    TArray<bool> flags;
    flags.Init(true, lineCount);
    TArray<int32> slots;
    TArray<int32> timesSelected;
    timesSelected.Init(0, lineCount);
    int32 cursor = 0;

    // Flags stay set (lines keep drifting); like the visuals actor, the cursor advances by exactly one slot per frame
    // whatever was selected
    for (int32 frame = 0; frame < lineCount; ++frame)
    {
        const int32 count = SelectDriftRebuilds(flags, cursor, cap, slots);
        TestTrue(FString::Printf(TEXT("frame %d within the cap"), frame), count <= cap && slots.Num() <= cap);
        TestEqual(FString::Printf(TEXT("frame %d uses the full budget"), frame), count, cap);
        TestTrue(FString::Printf(TEXT("frame %d starts at the cursor"), frame), slots.Num() > 0 && slots[0] == cursor);
        for (const int32 slot : slots)
        {
            if (TestTrue(TEXT("slot in range"), slot >= 0 && slot < lineCount))
            {
                ++timesSelected[slot];
            }
        }
        cursor = NextRebuildCursor(cursor, lineCount);
    }
    for (int32 slot = 0; slot < lineCount; ++slot)
    {
        TestTrue(FString::Printf(TEXT("slot %d rebuilt at least once"), slot), timesSelected[slot] >= 1);
    }

    // When each rebuild clears its flag, every line is rebuilt within ceil(N / cap) frames (the selection scans all N
    // slots from any start, so each frame takes the full budget while enough lines are left), and never more than N
    flags.Init(true, lineCount);
    cursor = 0;
    const int32 framesBound = (lineCount + cap - 1) / cap;
    int32 framesUsed = 0;
    for (int32 frame = 0; frame < lineCount && flags.Contains(true); ++frame)
    {
        SelectDriftRebuilds(flags, cursor, cap, slots);
        TestTrue(FString::Printf(TEXT("clearing frame %d within the cap"), frame), slots.Num() <= cap);
        for (const int32 slot : slots)
        {
            if (slot >= 0 && slot < lineCount)
            {
                flags[slot] = false;
            }
        }
        ++framesUsed;
        cursor = NextRebuildCursor(cursor, lineCount);
    }
    TestFalse(TEXT("every line rebuilt"), flags.Contains(true));
    TestTrue(FString::Printf(TEXT("done in %d frames, bound %d"), framesUsed, framesBound), framesUsed <= framesBound);
    TestTrue(TEXT("done within N frames"), framesUsed <= lineCount);
    TestEqual(TEXT("nothing left to rebuild"), SelectDriftRebuilds(flags, cursor, cap, slots), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicActorToggleTest, "SOLTest.OrbitLineLogic.ActorToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Without the game's subsystems the visuals build nothing, and the show/hide toggle keeps its own state
bool FSOLOrbitLineLogicActorToggleTest::RunTest(const FString& /*parameters*/)
{
    FSOLOrbitLineLogicTestWorld testWorld;
    if (!TestNull(TEXT("No anchor subsystem in the bare world"), testWorld.World->GetSubsystem<USOLAnchorSubsystem>()))
    {
        return false;
    }
    ASOLOrbitLineVisuals* visuals = testWorld.World->SpawnActor<ASOLOrbitLineVisuals>();
    if (!TestNotNull(TEXT("Visuals spawned"), visuals))
    {
        return false;
    }
    TestTrue(TEXT("BeginPlay ran"), visuals->HasActorBegunPlay());
    TestEqual(TEXT("No permanent lines without the registry"), visuals->GetPermanentLineCount(), 0);
    TestEqual(TEXT("No vertices without the registry"), visuals->GetTotalVertexCount(), 0);

    // The toggle flips the shown state and back
    TestTrue(TEXT("Lines enabled by default"), visuals->AreLinesEnabled());
    visuals->ToggleLines();
    TestFalse(TEXT("Toggle hides"), visuals->AreLinesEnabled());
    visuals->ToggleLines();
    TestTrue(TEXT("Toggle shows again"), visuals->AreLinesEnabled());

    // Hiding shows no transient line and highlights nothing
    visuals->SetLinesEnabled(false);
    TestFalse(TEXT("SetLinesEnabled(false) hides"), visuals->AreLinesEnabled());
    TestFalse(TEXT("No transient line while hidden"), visuals->IsTransientLineShown());
    TestEqual(TEXT("No highlight while hidden"), visuals->GetHighlightedSlot(), static_cast<int32>(INDEX_NONE));
    visuals->SetLinesEnabled(true);
    TestTrue(TEXT("SetLinesEnabled(true) shows"), visuals->AreLinesEnabled());
    TestEqual(TEXT("Still no permanent lines"), visuals->GetPermanentLineCount(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicOrientationTest, "SOLTest.OrbitLineLogic.Orientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The orbit orientation is a unit quaternion turning X into P, Y into Q and Z into the pole
bool FSOLOrbitLineLogicOrientationTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::OrbitOrientation;
    constexpr double tolerance = 1.0e-12;
    // { inclination, node, argument } sets, including zero, retrograde, negative and past-2-PI angles
    const double angleSets[][3] = {
        { 0.0, 0.0, 0.0 },
        { 0.1, 0.5, 1.0 },
        { 1.2, -2.0, 0.3 },
        { 3.0, 4.0, -5.5 },
        { UE_DOUBLE_PI * 0.5, UE_DOUBLE_PI, UE_DOUBLE_PI * 1.5 },
        { 0.0001, 7.5, 2.2 },
    };
    for (const double* angles : angleSets)
    {
        const FSOLKeplerElements elements = OrbitLineLogicElements(1.0e9, 0.3, angles[0], angles[1], angles[2], 0.7);
        const FQuat4d q = OrbitOrientation(elements);
        const FString label = FString::Printf(TEXT("i %.4f node %.4f arg %.4f"), angles[0], angles[1], angles[2]);
        TestEqual(label + TEXT(": unit length"), OrbitLineLogicQuatNorm(q), 1.0, tolerance);
        const FVector3d p = q.RotateVector(FVector3d(1.0, 0.0, 0.0));
        const FVector3d qAxis = q.RotateVector(FVector3d(0.0, 1.0, 0.0));
        const FVector3d pole = q.RotateVector(FVector3d(0.0, 0.0, 1.0));
        TestTrue(label + TEXT(": X turns into P"), p.Equals(OrbitLineLogicPeriapsisAxis(elements), tolerance));
        TestTrue(label + TEXT(": Y turns into Q"), qAxis.Equals(OrbitLineLogicQuadratureAxis(elements), tolerance));
        TestTrue(label + TEXT(": Z turns into the pole"), pole.Equals(OrbitLineLogicPoleAxis(elements), tolerance));
    }

    // Shape and mean anomaly do not change the orientation
    const FQuat4d a = OrbitOrientation(OrbitLineLogicElements(1.0e9, 0.1, 0.4, 0.5, 0.6, 0.0));
    const FQuat4d b = OrbitOrientation(OrbitLineLogicElements(3.0e11, 0.8, 0.4, 0.5, 0.6, 2.5));
    TestTrue(TEXT("shape and mean anomaly ignored"), OrbitLineLogicQuatAngle(a * b.Inverse()) < tolerance);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicRotationBetweenTest, "SOLTest.OrbitLineLogic.RotationBetween",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rotation between two orientations turns a line sampled from built into built's shape in now's orientation
bool FSOLOrbitLineLogicRotationBetweenTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLOrbitLineLogic;
    constexpr int32 segments = 64;
    // { built, now } pairs: different shapes, angles and mean anomalies
    const FSOLKeplerElements pairs[][2] = {
        { OrbitLineLogicElements(1.0e9, 0.2, 0.1, 0.5, 1.0, 0.3),
            OrbitLineLogicElements(1.05e9, 0.25, 0.3, 1.2, -0.7, 2.0) },
        { OrbitLineLogicElements(7.8e11, 0.05, 0.02, -1.0, 4.0, 0.0),
            OrbitLineLogicElements(7.8e11, 0.05, 0.0200001, -0.9999999, 4.0000002, 1.0) },
        { OrbitLineLogicElements(4.2e8, 0.6, 2.8, 3.0, 0.0, 0.0),
            OrbitLineLogicElements(4.0e8, 0.55, 0.0, 0.0, 0.0, 5.0) },
    };
    int32 pairIndex = 0;
    for (const auto& pair : pairs)
    {
        const FSOLKeplerElements& built = pair[0];
        const FSOLKeplerElements& now = pair[1];
        const FString label = FString::Printf(TEXT("pair %d"), pairIndex++);
        const FQuat4d rotation = OrbitRotationBetween(built, now);
        TestEqual(label + TEXT(": unit length"), OrbitLineLogicQuatNorm(rotation), 1.0, 1.0e-12);
        TestTrue(label + TEXT(": equals now * built^-1"), OrbitLineLogicQuatAngle(rotation
            * (OrbitOrientation(now) * OrbitOrientation(built).Inverse()).Inverse()) < 1.0e-12);

        // The oracle: built's shape sampled in now's orientation
        FSOLKeplerElements expectedElements = now;
        expectedElements.SemiMajorAxisM = built.SemiMajorAxisM;
        expectedElements.Eccentricity = built.Eccentricity;
        expectedElements.MeanAnomalyRad = built.MeanAnomalyRad;
        TArray<FVector3d> builtPoints;
        TArray<FVector3d> builtTangents;
        TArray<double> builtArcs;
        TArray<FVector3d> expectedPoints;
        TArray<FVector3d> expectedTangents;
        TArray<double> expectedArcs;
        const int32 builtCount = SOLOrbitLineMath::SampleOrbit(built, segments, builtPoints, builtTangents, builtArcs);
        const int32 expectedCount = SOLOrbitLineMath::SampleOrbit(expectedElements, segments, expectedPoints,
            expectedTangents, expectedArcs);
        if (!TestEqual(label + TEXT(": built sampled"), builtCount, segments + 1)
            || !TestEqual(label + TEXT(": expected sampled"), expectedCount, segments + 1))
        {
            continue;
        }
        const double tolerance = 1.0e-6 * built.SemiMajorAxisM;
        double worstError = 0.0;
        for (int32 i = 0; i < builtCount; ++i)
        {
            const FVector3d rotated = rotation.RotateVector(builtPoints[i]);
            worstError = FMath::Max(worstError, (rotated - expectedPoints[i]).Length());
        }
        TestTrue(FString::Printf(TEXT("%s: rotated line matches, worst error %.6g m, tolerance %.6g m"), *label,
            worstError, tolerance), worstError <= tolerance);
    }

    // Changing only the mean anomaly, a or e leaves the orientation alone: the identity
    const FSOLKeplerElements base = OrbitLineLogicElements(1.5e11, 0.0167, 0.3, 2.0, -1.4, 0.1);
    FSOLKeplerElements meanOnly = base;
    meanOnly.MeanAnomalyRad = 3.0;
    FSOLKeplerElements shapeOnly = base;
    shapeOnly.SemiMajorAxisM = 1.6e11;
    shapeOnly.Eccentricity = 0.4;
    TestTrue(TEXT("mean anomaly only: identity"), OrbitLineLogicQuatAngle(OrbitRotationBetween(base, meanOnly))
        < 1.0e-12);
    TestTrue(TEXT("shape only: identity"), OrbitLineLogicQuatAngle(OrbitRotationBetween(base, shapeOnly)) < 1.0e-12);
    TestTrue(TEXT("same elements: identity"), OrbitLineLogicQuatAngle(OrbitRotationBetween(base, base)) < 1.0e-12);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicRotationOffsetTest, "SOLTest.OrbitLineLogic.RotationOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rotation offset is q v q^-1 - v, exactly zero for the identity and relatively precise for tiny rotations
bool FSOLOrbitLineLogicRotationOffsetTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::RotationOffset;
    const FVector3d farPoint(4.5e12, -3.0e11, 1.0e10);
    const FVector3d identityOffset = RotationOffset(FQuat4d::Identity, farPoint);
    TestTrue(TEXT("identity offset is exactly zero"), identityOffset.X == 0.0 && identityOffset.Y == 0.0
        && identityOffset.Z == 0.0);

    // A large rotation agrees with RotateVector minus the point
    const FQuat4d large(FVector3d(1.0, 2.0, -0.5).GetSafeNormal(), 0.7);
    const FVector3d point(1.0e6, 2.0e6, 3.0e6);
    const FVector3d largeOffset = RotationOffset(large, point);
    const FVector3d largeExpected = large.RotateVector(point) - point;
    TestTrue(TEXT("large rotation equals RotateVector - v"), (largeOffset - largeExpected).Length()
        <= 1.0e-9 * point.Length());

    // 1e-9 rad about Z of a point 4.5e12 m out moves it ~4.5 km along Y, kept to 1e-6 relative
    const double angle = 1.0e-9;
    const FQuat4d tiny(FVector3d(0.0, 0.0, 1.0), angle);
    const double radius = 4.5e12;
    const FVector3d tinyOffset = RotationOffset(tiny, FVector3d(radius, 0.0, 0.0));
    const double expectedY = radius * FMath::Sin(angle);
    const double tolerance = 1.0e-6 * expectedY;
    TestTrue(FString::Printf(TEXT("tiny rotation: Y %.9g, expected %.9g"), tinyOffset.Y, expectedY),
        FMath::Abs(tinyOffset.Y - expectedY) <= tolerance);
    TestTrue(FString::Printf(TEXT("tiny rotation: X %.9g near zero"), tinyOffset.X),
        FMath::Abs(tinyOffset.X) <= tolerance);
    TestTrue(FString::Printf(TEXT("tiny rotation: Z %.9g near zero"), tinyOffset.Z),
        FMath::Abs(tinyOffset.Z) <= tolerance);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicShapeDriftTest, "SOLTest.OrbitLineLogic.ShapeDrift",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shape drift bound counts only a and e changes, and is infinite for an undrawable set
bool FSOLOrbitLineLogicShapeDriftTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::OrbitShapeDriftM;
    const FSOLKeplerElements built = OrbitLineLogicElements(1.0e9, 0.2, 0.1, 0.5, 1.0, 0.3);

    // a only, e only, both (in both directions)
    FSOLKeplerElements aOnly = built;
    aOnly.SemiMajorAxisM = 1.001e9;
    FSOLKeplerElements eOnly = built;
    eOnly.Eccentricity = 0.21;
    FSOLKeplerElements both = built;
    both.SemiMajorAxisM = 0.999e9;
    both.Eccentricity = 0.35;
    both.InclinationRad = 1.0;
    const FSOLKeplerElements* changed[] = { &aOnly, &eOnly, &both };
    const TCHAR* names[] = { TEXT("a only"), TEXT("e only"), TEXT("a, e and angles") };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(changed)); ++i)
    {
        const double expected = OrbitLineLogicExpectedShapeDrift(built, *changed[i]);
        TestEqual(FString(names[i]) + TEXT(": built -> now"), OrbitShapeDriftM(built, *changed[i]), expected,
            1.0e-9 * expected);
        TestEqual(FString(names[i]) + TEXT(": now -> built"), OrbitShapeDriftM(*changed[i], built), expected,
            1.0e-9 * expected);
    }

    // Orientation and mean anomaly changes give no shape drift
    FSOLKeplerElements turned = built;
    turned.InclinationRad = 2.5;
    turned.LongitudeOfAscendingNodeRad = -1.0;
    turned.ArgumentOfPeriapsisRad = 3.0;
    turned.MeanAnomalyRad = 5.0;
    TestEqual(TEXT("orientation and mean anomaly: zero"), OrbitShapeDriftM(built, turned), 0.0);
    TestEqual(TEXT("same elements: zero"), OrbitShapeDriftM(built, built), 0.0);

    // Either set undrawable: +inf
    FSOLKeplerElements zeroA = built;
    zeroA.SemiMajorAxisM = 0.0;
    FSOLKeplerElements hyperbolic = built;
    hyperbolic.Eccentricity = 1.0;
    FSOLKeplerElements nanAngle = built;
    nanAngle.InclinationRad = ORBIT_LINE_LOGIC_TEST_NAN;
    FSOLKeplerElements infA = built;
    infA.SemiMajorAxisM = ORBIT_LINE_LOGIC_TEST_INF;
    const FSOLKeplerElements* bad[] = { &zeroA, &hyperbolic, &nanAngle, &infA };
    for (const FSOLKeplerElements* badElements : bad)
    {
        TestTrue(TEXT("bad now gives +inf"), OrbitShapeDriftM(built, *badElements) == ORBIT_LINE_LOGIC_TEST_INF);
        TestTrue(TEXT("bad built gives +inf"), OrbitShapeDriftM(*badElements, built) == ORBIT_LINE_LOGIC_TEST_INF);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicShapeRebuildTest, "SOLTest.OrbitLineLogic.ShapeRebuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A shape rebuild is due only when the shape drift passes the radius (or fallback) tolerance, never for orientation
bool FSOLOrbitLineLogicShapeRebuildTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::NeedsShapeRebuild;
    const FSOLKeplerElements built = OrbitLineLogicElements(1.0e9, 0.1, 0.1, 0.5, 1.0, 0.3);
    const double radius = 1.0e6;
    TestEqual(TEXT("radius fraction is 0.05"), SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION, 0.05);
    TestEqual(TEXT("fallback fraction is 1e-6"), SOL::ORBIT_LINE_REBUILD_FALLBACK_FRACTION, 1.0e-6);

    // Radius tolerance 5e4 m: drift 4e4 * 1.1 = 4.4e4 is within, 5e4 * 1.1 = 5.5e4 is beyond
    FSOLKeplerElements within = built;
    within.SemiMajorAxisM += 4.0e4;
    FSOLKeplerElements beyond = built;
    beyond.SemiMajorAxisM += 5.0e4;
    TestFalse(TEXT("drift within the radius tolerance"), NeedsShapeRebuild(built, within, radius));
    TestTrue(TEXT("drift beyond the radius tolerance"), NeedsShapeRebuild(built, beyond, radius));
    TestFalse(TEXT("no change"), NeedsShapeRebuild(built, built, radius));

    // Any pure orientation (or mean anomaly) change never needs a rebuild
    FSOLKeplerElements turned = built;
    turned.InclinationRad = 2.9;
    turned.LongitudeOfAscendingNodeRad = 4.0;
    turned.ArgumentOfPeriapsisRad = -2.0;
    turned.MeanAnomalyRad = 1.0;
    TestFalse(TEXT("pure orientation change"), NeedsShapeRebuild(built, turned, radius));
    TestFalse(TEXT("pure orientation change, fallback tolerance"), NeedsShapeRebuild(built, turned, 0.0));

    // Without a usable radius the tolerance is 1e-6 of now's a (1e3 m here): 550 m within, 1100 m beyond
    FSOLKeplerElements smallDrift = built;
    smallDrift.SemiMajorAxisM += 500.0;
    FSOLKeplerElements largeDrift = built;
    largeDrift.SemiMajorAxisM += 1000.0;
    const double badRadii[] = { 0.0, -5.0, ORBIT_LINE_LOGIC_TEST_NAN, ORBIT_LINE_LOGIC_TEST_INF };
    for (const double badRadius : badRadii)
    {
        const FString label = FString::Printf(TEXT("radius %g"), badRadius);
        TestFalse(label + TEXT(": within the fallback"), NeedsShapeRebuild(built, smallDrift, badRadius));
        TestTrue(label + TEXT(": beyond the fallback"), NeedsShapeRebuild(built, largeDrift, badRadius));
    }

    // A large shape change is beyond the fallback too (built a = 1e9, now a = 2e9)
    FSOLKeplerElements doubled = built;
    doubled.SemiMajorAxisM = 2.0e9;
    TestTrue(TEXT("large change with the fallback"), NeedsShapeRebuild(built, doubled, 0.0));

    // Now undrawable: false whatever built is; built undrawable (now fine): true
    FSOLKeplerElements badNow = built;
    badNow.Eccentricity = 1.5;
    FSOLKeplerElements badBuilt = built;
    badBuilt.SemiMajorAxisM = ORBIT_LINE_LOGIC_TEST_NAN;
    TestFalse(TEXT("now undrawable"), NeedsShapeRebuild(built, badNow, radius));
    TestFalse(TEXT("both undrawable"), NeedsShapeRebuild(badBuilt, badNow, radius));
    TestTrue(TEXT("built undrawable"), NeedsShapeRebuild(badBuilt, built, radius));
    TestTrue(TEXT("built undrawable, fallback tolerance"), NeedsShapeRebuild(badBuilt, built, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicSceneDepthTrustTest, "SOLTest.OrbitLineLogic.SceneDepthTrust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The trust limit is max * (1 - largest R/d of the compressed bodies); 1:1-zone and invalid bodies are ignored
bool FSOLOrbitLineLogicSceneDepthTrustTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::SceneDepthTrustLimitCm;
    constexpr double maxCm = 1.0e11;
    const FVector3d viewpoint(2.0e8, -1.0e8, 5.0e7);

    // No bodies: the full 1:1 zone
    TestEqual(TEXT("no bodies"), SceneDepthTrustLimitCm(viewpoint, TConstArrayView<FVector3d>(),
        TConstArrayView<double>(), maxCm), maxCm);

    // A body inside the 1:1 zone (5e8 m = 5e10 cm away) does not lower it, however large
    const TArray<FVector3d> nearCenters = { viewpoint + FVector3d(5.0e8, 0.0, 0.0) };
    const TArray<double> nearRadii = { 4.0e8 };
    TestEqual(TEXT("1:1-zone body ignored"), SceneDepthTrustLimitCm(viewpoint, nearCenters, nearRadii, maxCm), maxCm);

    // Compressed bodies: the largest R/d wins
    const TArray<FVector3d> centers = {
        viewpoint + FVector3d(5.0e8, 0.0, 0.0),
        viewpoint + FVector3d(0.0, 2.0e9, 0.0),
        viewpoint + FVector3d(0.0, 0.0, -4.0e9),
        viewpoint + FVector3d(3.0e9, 4.0e9, 0.0),
    };
    const TArray<double> radii = { 4.0e8, 1.0e8, 6.0e7, 5.0e8 };
    // Ratios beyond the zone: 0.05, 0.015, 0.1 (5e8 / 5e9)
    const double expected = maxCm * (1.0 - 0.1);
    TestEqual(TEXT("largest R/d wins"), SceneDepthTrustLimitCm(viewpoint, centers, radii, maxCm), expected,
        1.0e-6 * maxCm);

    // Only the common prefix counts
    const TArray<double> shortRadii = { 4.0e8, 1.0e8, 6.0e7 };
    TestEqual(TEXT("common prefix only"), SceneDepthTrustLimitCm(viewpoint, centers, shortRadii, maxCm),
        maxCm * (1.0 - 0.05), 1.0e-6 * maxCm);

    // Invalid entries (non-finite centre, zero, negative, NaN or infinite radius) are skipped
    const TArray<FVector3d> invalidCenters = {
        FVector3d(ORBIT_LINE_LOGIC_TEST_NAN, 0.0, 0.0),
        viewpoint + FVector3d(2.0e9, 0.0, 0.0),
        viewpoint + FVector3d(2.0e9, 0.0, 0.0),
        viewpoint + FVector3d(2.0e9, 0.0, 0.0),
        viewpoint + FVector3d(2.0e9, 0.0, 0.0),
        viewpoint + FVector3d(0.0, 2.0e9, 0.0),
    };
    const TArray<double> invalidRadii = { 1.0e9, 0.0, -1.0e9, ORBIT_LINE_LOGIC_TEST_NAN, ORBIT_LINE_LOGIC_TEST_INF,
        2.0e7 };
    TestEqual(TEXT("invalid entries skipped"), SceneDepthTrustLimitCm(viewpoint, invalidCenters, invalidRadii,
        maxCm), maxCm * (1.0 - 0.01), 1.0e-6 * maxCm);

    // R/d is capped at 1: a viewpoint inside a compressed body's radius gives 0
    const TArray<FVector3d> insideCenters = { viewpoint + FVector3d(2.0e9, 0.0, 0.0) };
    const TArray<double> insideRadii = { 3.0e9 };
    TestEqual(TEXT("R/d capped at 1"), SceneDepthTrustLimitCm(viewpoint, insideCenters, insideRadii, maxCm), 0.0,
        1.0e-6 * maxCm);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicSceneDepthStraddleTest, "SOLTest.OrbitLineLogic.SceneDepthStraddle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Jupiter's compressed near surface lands inside the 1:1 zone in front of an Io-orbit point that is truly in front
// of Jupiter: the full max wrongly hides that point, the trust limit does not, and real 1:1 occlusion still works
bool FSOLOrbitLineLogicSceneDepthStraddleTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::SceneDepthTrustLimitCm;
    using SOLOrbitLineMath::IsHiddenBySceneDepth;
    constexpr double maxCm = 1.0e11;
    const FVector3d viewpoint = FVector3d::ZeroVector;
    const double jupiterRadiusM = 6.9911e7;
    const double jupiterDistanceM = 1.5e9;
    const TArray<FVector3d> centers = { FVector3d(jupiterDistanceM, 0.0, 0.0) };
    const TArray<double> radii = { jupiterRadiusM };

    // Compressed placement: centre at d', radius scaled by d'/d; the near surface lies inside the 1:1 zone
    const double trueDistanceCm = jupiterDistanceM * 100.0;
    const double placedDistanceCm = OrbitLineLogicCompressedDepthCm(trueDistanceCm, maxCm);
    const double nearSurfaceCm = placedDistanceCm - jupiterRadiusM * 100.0 * placedDistanceCm / trueDistanceCm;
    TestTrue(FString::Printf(TEXT("Jupiter's near surface %.6g cm is inside max"), nearSurfaceCm),
        nearSurfaceCm > 0.0 && nearSurfaceCm < maxCm);

    // An Io-orbit point truly in front of Jupiter (1.08e6 km), placed compressed
    const double ioPointCm = OrbitLineLogicCompressedDepthCm(1.08e9 * 100.0, maxCm);
    TestTrue(TEXT("the Io point is truly in front of Jupiter's surface"),
        1.08e9 < jupiterDistanceM - jupiterRadiusM);
    TestTrue(TEXT("the full max wrongly hides it"), IsHiddenBySceneDepth(ioPointCm, nearSurfaceCm, maxCm));

    const double trustCm = SceneDepthTrustLimitCm(viewpoint, centers, radii, maxCm);
    TestEqual(TEXT("trust limit is max * (1 - R/d)"), trustCm, maxCm * (1.0 - jupiterRadiusM / jupiterDistanceM),
        1.0e-6 * maxCm);
    TestTrue(FString::Printf(TEXT("trust limit %.6g cm below the near surface"), trustCm), trustCm < nearSurfaceCm);
    TestFalse(TEXT("the trust limit keeps the Io point"), IsHiddenBySceneDepth(ioPointCm, nearSurfaceCm, trustCm));

    // A real 1:1-zone occluder well inside the trust limit still hides a line behind it
    TestTrue(TEXT("1:1 occlusion still hides"), IsHiddenBySceneDepth(6.0e10, 5.0e10, trustCm));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicBeyondFadeTest, "SOLTest.OrbitLineLogic.BeyondFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A line is beyond the fade only when its nearest possible point is FADE_END semi-major axes away and not highlighted
bool FSOLOrbitLineLogicBeyondFadeTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::IsLineBeyondFade;
    TestEqual(TEXT("fade end is 200 a"), SOL::ORBIT_LINE_FADE_END_SMA, 200.0);
    const double a = 1.0e9;
    const double e = 0.2;
    // distance - a(1 + e) >= 200 a  <=>  distance >= 201.2 a
    const double threshold = a * (1.0 + e) + SOL::ORBIT_LINE_FADE_END_SMA * a;

    TestTrue(TEXT("just beyond"), IsLineBeyondFade(threshold * (1.0 + 1.0e-9), a, e, false));
    TestTrue(TEXT("far beyond"), IsLineBeyondFade(threshold * 10.0, a, e, false));
    TestFalse(TEXT("just inside"), IsLineBeyondFade(threshold * (1.0 - 1.0e-9), a, e, false));
    TestFalse(TEXT("at 200 a without the apoapsis margin"), IsLineBeyondFade(200.5 * a, a, e, false));
    TestFalse(TEXT("near"), IsLineBeyondFade(0.0, a, e, false));
    TestTrue(TEXT("circular, beyond"), IsLineBeyondFade(201.0 * a * (1.0 + 1.0e-9), a, 0.0, false));

    // The highlighted line is never hidden
    TestFalse(TEXT("highlighted"), IsLineBeyondFade(threshold * 10.0, a, e, true));

    // Bad input is never hidden
    TestFalse(TEXT("NaN distance"), IsLineBeyondFade(ORBIT_LINE_LOGIC_TEST_NAN, a, e, false));
    TestFalse(TEXT("infinite distance"), IsLineBeyondFade(ORBIT_LINE_LOGIC_TEST_INF, a, e, false));
    TestFalse(TEXT("zero a"), IsLineBeyondFade(threshold * 10.0, 0.0, e, false));
    TestFalse(TEXT("negative a"), IsLineBeyondFade(threshold * 10.0, -a, e, false));
    TestFalse(TEXT("NaN a"), IsLineBeyondFade(threshold * 10.0, ORBIT_LINE_LOGIC_TEST_NAN, e, false));
    TestFalse(TEXT("infinite a"), IsLineBeyondFade(threshold * 10.0, ORBIT_LINE_LOGIC_TEST_INF, e, false));
    TestFalse(TEXT("NaN e"), IsLineBeyondFade(threshold * 10.0, a, ORBIT_LINE_LOGIC_TEST_NAN, false));
    TestFalse(TEXT("infinite e"), IsLineBeyondFade(threshold * 10.0, a, ORBIT_LINE_LOGIC_TEST_INF, false));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLOrbitLineLogicLinesVisibleTest, "SOLTest.OrbitLineLogic.LinesVisible",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Lines are drawn only with the setting on and the main menu not showing
bool FSOLOrbitLineLogicLinesVisibleTest::RunTest(const FString& /*parameters*/)
{
    using SOLOrbitLineLogic::AreLinesVisible;
    TestTrue(TEXT("shown, in game"), AreLinesVisible(true, false));
    TestFalse(TEXT("shown, main menu"), AreLinesVisible(true, true));
    TestFalse(TEXT("hidden, in game"), AreLinesVisible(false, false));
    TestFalse(TEXT("hidden, main menu"), AreLinesVisible(false, true));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
