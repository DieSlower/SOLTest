/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcAiMath.h"
#include "SOLConstants.h"

#include "Tests/SOLTestHelpers.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #11 (Part 10d) pure NPC AI logic, written against the 10d contract in
// Docs/Plans/11-scale-and-performance-demo-plan.md before any production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Tolerance for exact formulas and geometric angle checks
    constexpr double NPCAI_EPS = 1.0e-9;

    // Seed used by the randomized property cases
    constexpr int32 NPCAI_SEED = 20261015;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double NpcAiNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double NpcAiInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a vector whose components are all NaN
    FVector3d NpcAiNaNVector()
    {
        return FVector3d(NpcAiNaN(), NpcAiNaN(), NpcAiNaN());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two vectors match within an absolute tolerance
    bool NpcAiVecNear(const FVector3d& actual, const FVector3d& expected, const double tolerance)
    {
        return SOLTestHelpers::IsFiniteVector(actual) && (actual - expected).Size() <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angle in radians between two non-zero vectors (atan2 form, accurate near 0 and PI)
    double NpcAiAngle(const FVector3d& a, const FVector3d& b)
    {
        return FMath::Atan2(FVector3d::CrossProduct(a, b).Size(), FVector3d::DotProduct(a, b));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a random vector with a length in [minLength, maxLength)
    FVector3d NpcAiRandomVector(FRandomStream& stream, const double minLength, const double maxLength)
    {
        return FVector3d(stream.VRand()).GetSafeNormal() * (minLength + (maxLength - minLength) * stream.FRand());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every field of two classes is identical
    bool NpcAiClassesEqual(const SOLNpcAi::FClassParams& a, const SOLNpcAi::FClassParams& b)
    {
        return a.MaxSpeedMps == b.MaxSpeedMps && a.MaxAccelMps2 == b.MaxAccelMps2
            && a.MaxTurnRateRadPerS == b.MaxTurnRateRadPerS && a.LeashM == b.LeashM && a.HitRadiusM == b.HitRadiusM
            && a.MaxHealth == b.MaxHealth && a.FireRatePerS == b.FireRatePerS && a.MuzzleSpeedMps == b.MuzzleSpeedMps
            && a.BoltDamage == b.BoltDamage && a.BoltLifetimeS == b.BoltLifetimeS && a.EngageRangeM == b.EngageRangeM
            && a.FireRangeM == b.FireRangeM && a.FireConeRad == b.FireConeRad && a.StandoffM == b.StandoffM
            && a.SeparationRadiusM == b.SeparationRadiusM && a.AimSpreadRad == b.AimSpreadRad
            && a.RespawnDelayS == b.RespawnDelayS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a target candidate with the given fields
    SOLNpcAi::FTargetCandidate NpcAiCandidate(const int32 index, const FVector3d& positionM, const int32 faction,
        const bool bAlive = true)
    {
        SOLNpcAi::FTargetCandidate candidate;
        candidate.Index = index;
        candidate.PositionM = positionM;
        candidate.Faction = faction;
        candidate.bAlive = bAlive;
        return candidate;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a combat state with the given fields
    SOLNpcAi::FCombatState NpcAiState(const double health, const double respawnS, const bool bAlive)
    {
        SOLNpcAi::FCombatState state;
        state.Health = health;
        state.RespawnS = respawnS;
        state.bAlive = bAlive;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two combat states are identical field by field
    bool NpcAiStatesEqual(const SOLNpcAi::FCombatState& a, const SOLNpcAi::FCombatState& b)
    {
        return a.Health == b.Health && a.RespawnS == b.RespawnS && a.bAlive == b.bAlive;
    }

    //////////////////////////////////////////////////////////////////////////
    // Solves |r + w t| = s t for its smallest positive root t (independent of the code under test)
    bool NpcAiSmallestPositiveRoot(const FVector3d& r, const FVector3d& w, const double s, double& outT)
    {
        // (w.w - s^2) t^2 + 2 (r.w) t + r.r = 0
        const double a = FVector3d::DotProduct(w, w) - s * s;
        const double b = 2.0 * FVector3d::DotProduct(r, w);
        const double c = FVector3d::DotProduct(r, r);
        if (FMath::Abs(a) < 1.0e-12 * FMath::Max(1.0, s * s))
        {
            // Linear case
            if (b >= 0.0)
            {
                return false;
            }
            outT = -c / b;
            return outT > 0.0;
        }
        const double discriminant = b * b - 4.0 * a * c;
        if (discriminant < 0.0)
        {
            return false;
        }
        const double root = FMath::Sqrt(discriminant);
        const double t1 = (-b - root) / (2.0 * a);
        const double t2 = (-b + root) / (2.0 * a);
        const double lo = FMath::Min(t1, t2);
        const double hi = FMath::Max(t1, t2);
        if (lo > 0.0)
        {
            outT = lo;
            return true;
        }
        if (hi > 0.0)
        {
            outT = hi;
            return true;
        }
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Constants and classes
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathConstantsTest, "SOLTest.NpcAiMath.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The contract's constants and enum values hold their specified values
bool FSOLNpcAiMathConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestEqual(TEXT("STEER_EPSILON"), STEER_EPSILON, 1.0e-6);
    TestEqual(TEXT("LEASH_SOFT_FRACTION"), LEASH_SOFT_FRACTION, 0.8);
    TestEqual(TEXT("TRANSVERSE_ERROR_FRACTION"), TRANSVERSE_ERROR_FRACTION, 0.15);
    TestEqual(TEXT("Bolts = 0"), static_cast<int32>(ECombatModel::Bolts), 0);
    TestEqual(TEXT("Statistical = 1"), static_cast<int32>(ECombatModel::Statistical), 1);
    TestEqual(TEXT("Hybrid = 2"), static_cast<int32>(ECombatModel::Hybrid), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathClassValidityTest, "SOLTest.NpcAiMath.ClassValidity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// There are at least two classes, each satisfies every invariant, and at least two differ in speed
bool FSOLNpcAiMathClassValidityTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const int32 count = ClassCount();
    TestTrue(TEXT("At least 2 classes"), count >= 2);

    double minSpeed = NpcAiInf();
    double maxSpeed = -NpcAiInf();
    for (int32 classIndex = 0; classIndex < count; ++classIndex)
    {
        const FClassParams& params = GetClass(classIndex);
        const double fields[] = { params.MaxSpeedMps, params.MaxAccelMps2, params.MaxTurnRateRadPerS, params.LeashM,
            params.HitRadiusM, params.MaxHealth, params.FireRatePerS, params.MuzzleSpeedMps, params.BoltDamage,
            params.BoltLifetimeS, params.EngageRangeM, params.FireRangeM, params.FireConeRad, params.StandoffM,
            params.SeparationRadiusM, params.RespawnDelayS };
        bool bPositiveFinite = true;
        for (const double field : fields)
        {
            bPositiveFinite = bPositiveFinite && FMath::IsFinite(field) && field > 0.0;
        }

        // Every invariant of the Classes row
        TestTrue(*FString::Printf(TEXT("Class %d: every field except AimSpread finite and > 0"), classIndex),
            bPositiveFinite);
        TestTrue(*FString::Printf(TEXT("Class %d: AimSpread finite and >= 0"), classIndex),
            FMath::IsFinite(params.AimSpreadRad) && params.AimSpreadRad >= 0.0);
        TestTrue(*FString::Printf(TEXT("Class %d: FireCone <= PI"), classIndex), params.FireConeRad <= UE_DOUBLE_PI);
        TestTrue(*FString::Printf(TEXT("Class %d: Standoff < FireRange"), classIndex),
            params.StandoffM < params.FireRangeM);
        TestTrue(*FString::Printf(TEXT("Class %d: FireRange <= EngageRange"), classIndex),
            params.FireRangeM <= params.EngageRangeM);
        TestTrue(*FString::Printf(TEXT("Class %d: FireRange <= bolt reach"), classIndex),
            params.FireRangeM <= params.MuzzleSpeedMps * params.BoltLifetimeS);
        TestTrue(*FString::Printf(TEXT("Class %d: Separation >= 2 * HitRadius"), classIndex),
            params.SeparationRadiusM >= 2.0 * params.HitRadiusM);
        TestTrue(*FString::Printf(TEXT("Class %d: BoltDamage < MaxHealth"), classIndex),
            params.BoltDamage < params.MaxHealth);
        TestTrue(*FString::Printf(TEXT("Class %d: MaxSpeed < MuzzleSpeed"), classIndex),
            params.MaxSpeedMps < params.MuzzleSpeedMps);
        minSpeed = FMath::Min(minSpeed, params.MaxSpeedMps);
        maxSpeed = FMath::Max(maxSpeed, params.MaxSpeedMps);
    }
    TestTrue(TEXT("At least two classes differ in MaxSpeed"), maxSpeed > minSpeed);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathClassClampTest, "SOLTest.NpcAiMath.ClassClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Out-of-range class indices clamp to the first or last class
bool FSOLNpcAiMathClassClampTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const int32 last = ClassCount() - 1;
    TestTrue(TEXT("-1 is class 0"), NpcAiClassesEqual(GetClass(-1), GetClass(0)));
    TestTrue(TEXT("INT32_MIN is class 0"), NpcAiClassesEqual(GetClass(MIN_int32), GetClass(0)));
    TestTrue(TEXT("Count is the last class"), NpcAiClassesEqual(GetClass(last + 1), GetClass(last)));
    TestTrue(TEXT("INT32_MAX is the last class"), NpcAiClassesEqual(GetClass(MAX_int32), GetClass(last)));
    TestTrue(TEXT("In-range lookups are stable"), NpcAiClassesEqual(GetClass(last), GetClass(last)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathPickClassTest, "SOLTest.NpcAiMath.PickClass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PickClass is floor(u * count) with u clamped to [0, 1) and NaN as 0
bool FSOLNpcAiMathPickClassTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const int32 count = ClassCount();
    for (int32 classIndex = 0; classIndex < count; ++classIndex)
    {
        const double unit = (classIndex + 0.5) / count;
        TestEqual(*FString::Printf(TEXT("Mid-bucket unit picks class %d"), classIndex), PickClass(unit), classIndex);
    }
    TestEqual(TEXT("0 picks class 0"), PickClass(0.0), 0);
    TestEqual(TEXT("Just below 1 picks the last"), PickClass(0.999999), count - 1);
    TestEqual(TEXT("1 clamps below 1"), PickClass(1.0), count - 1);
    TestEqual(TEXT("5 clamps below 1"), PickClass(5.0), count - 1);
    TestEqual(TEXT("+inf clamps below 1"), PickClass(NpcAiInf()), count - 1);
    TestEqual(TEXT("-1 clamps to 0"), PickClass(-1.0), 0);
    TestEqual(TEXT("-inf clamps to 0"), PickClass(-NpcAiInf()), 0);
    TestEqual(TEXT("NaN counts as 0"), PickClass(NpcAiNaN()), 0);

    // Always in range over a sweep
    bool bInRange = true;
    for (int32 step = -100; step <= 1100; ++step)
    {
        const int32 picked = PickClass(step / 1000.0);
        bInRange = bInRange && picked >= 0 && picked < count;
    }
    TestTrue(TEXT("Always in [0, count)"), bInRange);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// LimitSteering
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLimitSteeringReachableTest, "SOLTest.NpcAiMath.LimitSteeringReachable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A desired velocity both limits allow is returned exactly; one too fast is clamped to maxSpeed, direction kept
bool FSOLNpcAiMathLimitSteeringReachableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d current(100.0, 0.0, 0.0);
    TestTrue(TEXT("Reachable desired returned"),
        NpcAiVecNear(LimitSteering(current, FVector3d(110.0, 5.0, 0.0), 1000.0, 1.0e6, 10.0, 0.1),
            FVector3d(110.0, 5.0, 0.0), NPCAI_EPS * 110.0));
    TestTrue(TEXT("Small turn inside the turn limit returned"),
        NpcAiVecNear(LimitSteering(current, FVector3d(100.0, 5.0, 0.0), 1000.0, 1.0e6, 1.0, 0.1),
            FVector3d(100.0, 5.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("Desired equal to current returned"),
        NpcAiVecNear(LimitSteering(current, current, 1000.0, 1.0, 1.0, 0.1), current, NPCAI_EPS * 100.0));

    // Speed clamp from rest (no turn limit with a zero current)
    TestTrue(TEXT("Desired 500 m/s clamped to 100 along its direction"),
        NpcAiVecNear(LimitSteering(FVector3d::ZeroVector, FVector3d(300.0, 400.0, 0.0), 100.0, 1.0e9, 0.01, 0.1),
            FVector3d(60.0, 80.0, 0.0), NPCAI_EPS * 100.0));

    // Slowing below the current speed is allowed
    TestTrue(TEXT("Slowing down to a slower desired"),
        NpcAiVecNear(LimitSteering(current, FVector3d(20.0, 0.0, 0.0), 50.0, 1.0e6, 1.0, 0.1),
            FVector3d(20.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLimitSteeringAccelTest, "SOLTest.NpcAiMath.LimitSteeringAccel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The velocity change is shortened to maxAccel * dt along the change direction
bool FSOLNpcAiMathLimitSteeringAccelTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestTrue(TEXT("From rest: 10 m/s^2 for 0.5 s gives 5 m/s"),
        NpcAiVecNear(LimitSteering(FVector3d::ZeroVector, FVector3d(100.0, 0.0, 0.0), 1000.0, 10.0, 1.0, 0.5),
            FVector3d(5.0, 0.0, 0.0), NPCAI_EPS * 10.0));
    TestTrue(TEXT("Change direction kept"),
        NpcAiVecNear(LimitSteering(FVector3d(10.0, 0.0, 0.0), FVector3d(10.0, 30.0, 40.0), 1000.0, 10.0, 100.0, 1.0),
            FVector3d(10.0, 6.0, 8.0), NPCAI_EPS * 10.0));

    // Reversal with an unlimited turn rate: pure deceleration along the line
    TestTrue(TEXT("Braking through a reversal is limited too"),
        NpcAiVecNear(LimitSteering(FVector3d(10.0, 0.0, 0.0), FVector3d(-10.0, 0.0, 0.0), 1000.0, 5.0, 1.0e6, 1.0),
            FVector3d(5.0, 0.0, 0.0), NPCAI_EPS * 10.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLimitSteeringTurnTest, "SOLTest.NpcAiMath.LimitSteeringTurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A desired direction beyond theta = maxTurn * dt is replaced by current's direction rotated by exactly theta
bool FSOLNpcAiMathLimitSteeringTurnTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d current(100.0, 0.0, 0.0);

    // 90 degree request, theta = 0.1, |d| = 100
    const FVector3d turned = LimitSteering(current, FVector3d(0.0, 100.0, 0.0), 1000.0, 1.0e9, 0.1, 1.0);
    TestTrue(TEXT("Rotated by theta toward +Y, |d| kept"),
        NpcAiVecNear(turned, FVector3d(100.0 * FMath::Cos(0.1), 100.0 * FMath::Sin(0.1), 0.0), NPCAI_EPS * 100.0));
    TestNearlyEqual(TEXT("Angle is exactly theta"), NpcAiAngle(turned, current), 0.1, NPCAI_EPS);

    // The speed clamp applies before the turn: |d| = maxSpeed = 200, theta = 0.2 * 0.5
    const FVector3d clampedTurn = LimitSteering(current, FVector3d(0.0, 0.0, 300.0), 200.0, 1.0e9, 0.2, 0.5);
    TestTrue(TEXT("Clamped then rotated toward +Z"),
        NpcAiVecNear(clampedTurn, FVector3d(200.0 * FMath::Cos(0.1), 0.0, 200.0 * FMath::Sin(0.1)),
            NPCAI_EPS * 200.0));

    // Rotation stays in the plane of the two vectors (no out-of-plane drift)
    const FVector3d planar = LimitSteering(FVector3d(50.0, 50.0, 0.0), FVector3d(-50.0, 50.0, 0.0), 1000.0, 1.0e9,
        0.3, 1.0);
    TestTrue(TEXT("Rotation in the plane of current and desired"), FMath::Abs(planar.Z) <= NPCAI_EPS * 100.0);
    TestNearlyEqual(TEXT("Planar rotation angle is theta"),
        NpcAiAngle(planar, FVector3d(50.0, 50.0, 0.0)), 0.3, NPCAI_EPS);
    TestTrue(TEXT("Planar rotation turns toward the desired side"), planar.X < 50.0 && planar.Y > 50.0);

    // Exactly opposite: any plane containing current, by exactly theta, length |d|
    const FVector3d opposite = LimitSteering(current, FVector3d(-50.0, 0.0, 0.0), 1000.0, 1.0e9, 0.5, 1.0);
    TestTrue(TEXT("Opposite: finite"), SOLTestHelpers::IsFiniteVector(opposite));
    TestNearlyEqual(TEXT("Opposite: length |d|"), opposite.Size(), 50.0, NPCAI_EPS * 50.0);
    TestNearlyEqual(TEXT("Opposite: rotated by exactly theta"), NpcAiAngle(opposite, current), 0.5, NPCAI_EPS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLimitSteeringInvalidTest, "SOLTest.NpcAiMath.LimitSteeringInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Invalid dt returns current unchanged; non-finite or negative limits count as 0
bool FSOLNpcAiMathLimitSteeringInvalidTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d current(100.0, 20.0, -5.0);
    const FVector3d desired(0.0, 100.0, 0.0);
    TestTrue(TEXT("dt 0: unchanged"), LimitSteering(current, desired, 1000.0, 1.0e6, 10.0, 0.0) == current);
    TestTrue(TEXT("dt < 0: unchanged"), LimitSteering(current, desired, 1000.0, 1.0e6, 10.0, -1.0) == current);
    TestTrue(TEXT("dt NaN: unchanged"), LimitSteering(current, desired, 1000.0, 1.0e6, 10.0, NpcAiNaN()) == current);
    TestTrue(TEXT("dt +inf: unchanged"), LimitSteering(current, desired, 1000.0, 1.0e6, 10.0, NpcAiInf()) == current);

    // Acceleration limit of 0: no change at all
    TestTrue(TEXT("maxAccel NaN counts as 0"),
        NpcAiVecNear(LimitSteering(current, desired, 1000.0, NpcAiNaN(), 10.0, 0.1), current, NPCAI_EPS));
    TestTrue(TEXT("maxAccel < 0 counts as 0"),
        NpcAiVecNear(LimitSteering(current, desired, 1000.0, -5.0, 10.0, 0.1), current, NPCAI_EPS));
    TestTrue(TEXT("maxAccel +inf counts as 0"),
        NpcAiVecNear(LimitSteering(current, desired, 1000.0, NpcAiInf(), 10.0, 0.1), current, NPCAI_EPS));

    // Speed limit of 0: the desired is clamped to zero, so the ship brakes to rest
    const FVector3d slow(10.0, 0.0, 0.0);
    TestTrue(TEXT("maxSpeed NaN counts as 0"),
        NpcAiVecNear(LimitSteering(slow, desired, NpcAiNaN(), 1.0e6, 10.0, 0.1), FVector3d::ZeroVector, NPCAI_EPS));
    TestTrue(TEXT("maxSpeed < 0 counts as 0"),
        NpcAiVecNear(LimitSteering(slow, desired, -50.0, 1.0e6, 10.0, 0.1), FVector3d::ZeroVector, NPCAI_EPS));

    // Turn limit of 0: direction held at current's, length |d|
    const FVector3d straight(100.0, 0.0, 0.0);
    TestTrue(TEXT("maxTurn NaN counts as 0"),
        NpcAiVecNear(LimitSteering(straight, FVector3d(0.0, 80.0, 0.0), 1000.0, 1.0e9, NpcAiNaN(), 0.1),
            FVector3d(80.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("maxTurn < 0 counts as 0"),
        NpcAiVecNear(LimitSteering(straight, FVector3d(0.0, 80.0, 0.0), 1000.0, 1.0e9, -1.0, 0.1),
            FVector3d(80.0, 0.0, 0.0), NPCAI_EPS * 100.0));

    // Non-finite desired counts as zero (unmentioned vector input): brake toward rest
    TestTrue(TEXT("NaN desired counts as zero"),
        NpcAiVecNear(LimitSteering(slow, NpcAiNaNVector(), 1000.0, 1.0e6, 10.0, 0.1), FVector3d::ZeroVector,
            NPCAI_EPS));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLimitSteeringPropertiesTest, "SOLTest.NpcAiMath.LimitSteeringProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Over many random inputs: accel bound, turn bound, speed bound, and exact desired when both limits allow it
bool FSOLNpcAiMathLimitSteeringPropertiesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    FRandomStream stream(NPCAI_SEED);
    bool bAccelBound = true;
    bool bTurnBound = true;
    bool bSpeedBound = true;
    bool bExactWhenReachable = true;
    bool bFinite = true;
    int32 reachableCases = 0;
    for (int32 sample = 0; sample < 5000; ++sample)
    {
        const FVector3d current = (sample % 10 == 0) ? FVector3d::ZeroVector : NpcAiRandomVector(stream, 0.0, 200.0);
        const FVector3d desired = (sample % 3 == 0)
            ? current + NpcAiRandomVector(stream, 0.0, 5.0)
            : NpcAiRandomVector(stream, 0.0, 300.0);
        const double maxSpeed = 250.0 * stream.FRand();
        const double maxAccel = 500.0 * stream.FRand();
        const double maxTurn = 4.0 * stream.FRand();
        const double dt = 0.001 + 0.1 * stream.FRand();
        const double theta = maxTurn * dt;
        const FVector3d result = LimitSteering(current, desired, maxSpeed, maxAccel, maxTurn, dt);
        bFinite = bFinite && SOLTestHelpers::IsFiniteVector(result);

        // |result - current| <= maxAccel * dt
        bAccelBound = bAccelBound && (result - current).Size() <= maxAccel * dt * (1.0 + 1.0e-9) + 1.0e-9;

        // Angle bound when both are non-zero
        if (current.Size() >= 1.0e-3 && result.Size() >= 1.0e-3)
        {
            bTurnBound = bTurnBound && NpcAiAngle(result, current) <= theta + NPCAI_EPS;
        }

        // |result| <= max(|current|, maxSpeed)
        const double speedCap = FMath::Max(current.Size(), maxSpeed);
        bSpeedBound = bSpeedBound && result.Size() <= speedCap * (1.0 + 1.0e-9) + 1.0e-9;

        // Clearly inside both limits (and the speed limit): the desired itself
        const bool bSpeedOk = desired.Size() < maxSpeed * (1.0 - 1.0e-6);
        const bool bTurnOk = current.Size() < 1.0e-3 || desired.Size() < 1.0e-3
            || NpcAiAngle(desired, current) < theta - 1.0e-6;
        const bool bAccelOk = (desired - current).Size() < maxAccel * dt * (1.0 - 1.0e-6);
        if (bSpeedOk && bTurnOk && bAccelOk)
        {
            ++reachableCases;
            bExactWhenReachable = bExactWhenReachable
                && NpcAiVecNear(result, desired, NPCAI_EPS * FMath::Max(1.0, desired.Size()));
        }
    }
    TestTrue(TEXT("Always finite"), bFinite);
    TestTrue(TEXT("|result - current| <= maxAccel * dt"), bAccelBound);
    TestTrue(TEXT("Angle to current <= maxTurn * dt"), bTurnBound);
    TestTrue(TEXT("|result| <= max(|current|, maxSpeed)"), bSpeedBound);
    TestTrue(TEXT("The sample set exercises reachable cases"), reachableCases > 50);
    TestTrue(TEXT("Exact desired whenever reachable"), bExactWhenReachable);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// PursuitVelocity
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathPursuitStandoffTest, "SOLTest.NpcAiMath.PursuitStandoff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Full speed in when far, out when too close, sideways at the standoff, blended between
bool FSOLNpcAiMathPursuitStandoffTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self = FVector3d::ZeroVector;
    const FVector3d noVelocity = FVector3d::ZeroVector;
    const FVector3d up(0.0, 0.0, 1.0);

    // t = normalize(+X x +Z) = -Y for every case below
    TestTrue(TEXT("Far: full speed in"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(10000.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0, up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("At 2x standoff: k = 1"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(2000.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0, up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("At the standoff: sideways"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(1000.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0, up),
            FVector3d(0.0, -100.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("At 1.5x standoff: k = 0.5"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(1500.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0, up),
            FVector3d(50.0, -50.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("At 0.2x standoff: k = -0.8 (backing out)"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(200.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0, up),
            FVector3d(-80.0, -20.0, 0.0), NPCAI_EPS * 100.0));

    // The strafe axis need not be unit length
    TestTrue(TEXT("Unnormalized strafe axis"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(1000.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0,
            FVector3d(0.0, 0.0, 5.0)), FVector3d(0.0, -100.0, 0.0), NPCAI_EPS * 100.0));

    // Self not at the origin: only the offset matters
    TestTrue(TEXT("Translated self"),
        NpcAiVecNear(PursuitVelocity(FVector3d(5.0e6, -3.0e6, 1.0e6), FVector3d(5.0e6 + 1500.0, -3.0e6, 1.0e6),
            noVelocity, 100.0, 1000.0, 0.0, up), FVector3d(50.0, -50.0, 0.0), 1.0e-6));

    // Non-finite or <= 0 standoff: k = 1
    TestTrue(TEXT("Standoff 0: k = 1"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(10.0, 0.0, 0.0), noVelocity, 100.0, 0.0, 0.0, up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("Standoff NaN: k = 1"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(10.0, 0.0, 0.0), noVelocity, 100.0, NpcAiNaN(), 0.0, up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("Standoff negative: k = 1"),
        NpcAiVecNear(PursuitVelocity(self, FVector3d(10.0, 0.0, 0.0), noVelocity, 100.0, -500.0, 0.0, up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathPursuitLeadTest, "SOLTest.NpcAiMath.PursuitLead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The pursuit point is the target advanced by its velocity times the lead time (0 when invalid)
bool FSOLNpcAiMathPursuitLeadTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self = FVector3d::ZeroVector;
    const FVector3d target(1000.0, 0.0, 0.0);
    const FVector3d targetVelocity(0.0, 100.0, 0.0);
    const FVector3d up(0.0, 0.0, 1.0);
    const FVector3d diagonal = FVector3d(1.0, 1.0, 0.0).GetSafeNormal() * 100.0;
    TestTrue(TEXT("Lead 10 s aims at (1000, 1000)"),
        NpcAiVecNear(PursuitVelocity(self, target, targetVelocity, 100.0, 0.0, 10.0, up), diagonal,
            NPCAI_EPS * 100.0));
    TestTrue(TEXT("Lead 0: current position"),
        NpcAiVecNear(PursuitVelocity(self, target, targetVelocity, 100.0, 0.0, 0.0, up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("Negative lead counts as 0"),
        NpcAiVecNear(PursuitVelocity(self, target, targetVelocity, 100.0, 0.0, -10.0, up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("NaN lead counts as 0"),
        NpcAiVecNear(PursuitVelocity(self, target, targetVelocity, 100.0, 0.0, NpcAiNaN(), up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("+inf lead counts as 0"),
        NpcAiVecNear(PursuitVelocity(self, target, targetVelocity, 100.0, 0.0, NpcAiInf(), up),
            FVector3d(100.0, 0.0, 0.0), NPCAI_EPS * 100.0));

    // Lead point on top of self: zero
    TestTrue(TEXT("Lead point coincides with self: zero"),
        PursuitVelocity(self, FVector3d(-1000.0, 0.0, 0.0), FVector3d(100.0, 0.0, 0.0), 100.0, 500.0, 10.0, up)
            .Size() <= NPCAI_EPS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathPursuitDegenerateTest, "SOLTest.NpcAiMath.PursuitDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero offset, invalid speed and a strafe axis parallel to the target give the contract's fallbacks; length <= maxSpeed
bool FSOLNpcAiMathPursuitDegenerateTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self(10.0, 20.0, 30.0);
    const FVector3d noVelocity = FVector3d::ZeroVector;
    const FVector3d up(0.0, 0.0, 1.0);
    const FVector3d target(1010.0, 20.0, 30.0);
    TestTrue(TEXT("Target on self: zero"),
        PursuitVelocity(self, self, noVelocity, 100.0, 1000.0, 0.0, up) == FVector3d::ZeroVector);
    TestTrue(TEXT("Target within EPS: zero"),
        PursuitVelocity(self, self + FVector3d(1.0e-7, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0, up)
            == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed 0: zero"),
        PursuitVelocity(self, target, noVelocity, 0.0, 500.0, 0.0, up) == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed < 0: zero"),
        PursuitVelocity(self, target, noVelocity, -10.0, 500.0, 0.0, up) == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed NaN: zero"),
        PursuitVelocity(self, target, noVelocity, NpcAiNaN(), 500.0, 0.0, up) == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed +inf: zero"),
        PursuitVelocity(self, target, noVelocity, NpcAiInf(), 500.0, 0.0, up) == FVector3d::ZeroVector);

    // Strafe axis parallel to the direction (or zero): t = 0
    const FVector3d alongX(1.0, 0.0, 0.0);
    TestTrue(TEXT("Parallel strafe axis at the standoff: zero"),
        NpcAiVecNear(PursuitVelocity(self, target, noVelocity, 100.0, 1000.0, 0.0, alongX), FVector3d::ZeroVector,
            NPCAI_EPS * 100.0));
    TestTrue(TEXT("Parallel strafe axis, too close: radial only"),
        NpcAiVecNear(PursuitVelocity(self, self + FVector3d(500.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0, alongX),
            FVector3d(-50.0, 0.0, 0.0), NPCAI_EPS * 100.0));
    TestTrue(TEXT("Zero strafe axis, too close: radial only"),
        NpcAiVecNear(PursuitVelocity(self, self + FVector3d(500.0, 0.0, 0.0), noVelocity, 100.0, 1000.0, 0.0,
            FVector3d::ZeroVector), FVector3d(-50.0, 0.0, 0.0), NPCAI_EPS * 100.0));

    // Length never exceeds maxSpeed
    FRandomStream stream(NPCAI_SEED + 1);
    bool bBounded = true;
    for (int32 sample = 0; sample < 2000; ++sample)
    {
        const FVector3d offset = NpcAiRandomVector(stream, 1.0, 5000.0);
        const FVector3d velocity = NpcAiRandomVector(stream, 0.0, 200.0);
        const FVector3d strafe = NpcAiRandomVector(stream, 0.0, 2.0);
        const double maxSpeed = 1.0 + 300.0 * stream.FRand();
        const double standoff = 2000.0 * stream.FRand();
        const FVector3d result = PursuitVelocity(self, self + offset, velocity, maxSpeed, standoff,
            3.0 * stream.FRand(), strafe);
        bBounded = bBounded && SOLTestHelpers::IsFiniteVector(result) && result.Size() <= maxSpeed * (1.0 + NPCAI_EPS);
    }
    TestTrue(TEXT("|result| <= maxSpeed"), bBounded);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SeparationVelocity
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathSeparationTest, "SOLTest.NpcAiMath.Separation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Neighbours strictly inside the radius push away with weight (1 - d / radius); others are skipped
bool FSOLNpcAiMathSeparationTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self = FVector3d::ZeroVector;
    const TArray<FVector3d> one = { FVector3d(-50.0, 0.0, 0.0) };
    TestTrue(TEXT("One neighbour at half radius: 0.5 * maxSpeed away"),
        NpcAiVecNear(SeparationVelocity(self, one, 100.0, 10.0), FVector3d(5.0, 0.0, 0.0), NPCAI_EPS * 10.0));

    const TArray<FVector3d> two = { FVector3d(-50.0, 0.0, 0.0), FVector3d(0.0, -75.0, 0.0) };
    TestTrue(TEXT("Two neighbours sum"),
        NpcAiVecNear(SeparationVelocity(self, two, 100.0, 10.0), FVector3d(5.0, 2.5, 0.0), NPCAI_EPS * 10.0));

    // Skipped: beyond, exactly at the radius, coincident (self), within EPS, non-finite
    const TArray<FVector3d> skipped = { FVector3d(-50.0, 0.0, 0.0), FVector3d(150.0, 0.0, 0.0),
        FVector3d(0.0, 100.0, 0.0), self, FVector3d(1.0e-7, 0.0, 0.0), NpcAiNaNVector(),
        FVector3d(NpcAiInf(), 0.0, 0.0) };
    TestTrue(TEXT("Only the valid neighbour counts"),
        NpcAiVecNear(SeparationVelocity(self, skipped, 100.0, 10.0), FVector3d(5.0, 0.0, 0.0), NPCAI_EPS * 10.0));

    // Translated self gives the same push
    const FVector3d offset(1.0e6, 2.0e6, -3.0e6);
    const TArray<FVector3d> oneTranslated = { offset + FVector3d(-50.0, 0.0, 0.0) };
    TestTrue(TEXT("Translated"),
        NpcAiVecNear(SeparationVelocity(offset, oneTranslated, 100.0, 10.0), FVector3d(5.0, 0.0, 0.0), 1.0e-6));

    // Clamped to maxSpeed: three neighbours at 1 m give s = 2.97 -> 29.7, shortened to 10
    const TArray<FVector3d> crowd = { FVector3d(-1.0, 0.0, 0.0), FVector3d(-1.0, 0.0, 0.0),
        FVector3d(-1.0, 0.0, 0.0) };
    TestTrue(TEXT("Shortened to maxSpeed"),
        NpcAiVecNear(SeparationVelocity(self, crowd, 100.0, 10.0), FVector3d(10.0, 0.0, 0.0), NPCAI_EPS * 10.0));

    // Symmetric neighbours cancel
    const TArray<FVector3d> symmetric = { FVector3d(-30.0, 0.0, 0.0), FVector3d(30.0, 0.0, 0.0) };
    TestTrue(TEXT("Symmetric neighbours cancel"),
        NpcAiVecNear(SeparationVelocity(self, symmetric, 100.0, 10.0), FVector3d::ZeroVector, NPCAI_EPS * 10.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathSeparationInvalidTest, "SOLTest.NpcAiMath.SeparationInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Invalid radius or speed, or no neighbours, give zero
bool FSOLNpcAiMathSeparationInvalidTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self = FVector3d::ZeroVector;
    const TArray<FVector3d> one = { FVector3d(-50.0, 0.0, 0.0) };
    TestTrue(TEXT("Radius 0"), SeparationVelocity(self, one, 0.0, 10.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("Radius < 0"), SeparationVelocity(self, one, -100.0, 10.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("Radius NaN"), SeparationVelocity(self, one, NpcAiNaN(), 10.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("Radius +inf"), SeparationVelocity(self, one, NpcAiInf(), 10.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed 0"), SeparationVelocity(self, one, 100.0, 0.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed < 0"), SeparationVelocity(self, one, 100.0, -10.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed NaN"), SeparationVelocity(self, one, 100.0, NpcAiNaN()) == FVector3d::ZeroVector);
    TestTrue(TEXT("maxSpeed +inf"), SeparationVelocity(self, one, 100.0, NpcAiInf()) == FVector3d::ZeroVector);
    const TArray<FVector3d> none;
    TestTrue(TEXT("No neighbours"), SeparationVelocity(self, none, 100.0, 10.0).Size() <= NPCAI_EPS);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// LeashVelocity
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLeashTest, "SOLTest.NpcAiMath.Leash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No pull inside 80% of the leash, a linear blend to a pure pull back at the leash and beyond
bool FSOLNpcAiMathLeashTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d desired(4.0, 6.0, 0.0);
    TestTrue(TEXT("Inside the soft zone: desired unchanged"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(50.0, 0.0, 0.0), 100.0, 10.0), desired, NPCAI_EPS * 10.0));
    TestTrue(TEXT("At 80% of the leash: w = 0"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(80.0, 0.0, 0.0), 100.0, 10.0), desired, 1.0e-6));
    TestTrue(TEXT("At 90%: w = 0.5"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(90.0, 0.0, 0.0), 100.0, 10.0), FVector3d(-3.0, 3.0, 0.0),
            1.0e-6));
    TestTrue(TEXT("At the leash: pure pull back"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(0.0, 100.0, 0.0), 100.0, 10.0), FVector3d(0.0, -10.0, 0.0),
            1.0e-6));
    TestTrue(TEXT("Beyond the leash: pure pull back"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(0.0, 0.0, 500.0), 100.0, 10.0), FVector3d(0.0, 0.0, -10.0),
            NPCAI_EPS * 10.0));
    TestTrue(TEXT("Beyond the leash, diagonal deviation"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(300.0, 400.0, 0.0), 100.0, 10.0), FVector3d(-6.0, -8.0, 0.0),
            NPCAI_EPS * 10.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLeashInvalidTest, "SOLTest.NpcAiMath.LeashInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero or non-finite deviation passes desired through; invalid leash pulls fully; invalid speed counts as 0
bool FSOLNpcAiMathLeashInvalidTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d desired(4.0, 6.0, 0.0);
    TestTrue(TEXT("Zero deviation: desired"),
        LeashVelocity(desired, FVector3d::ZeroVector, 100.0, 10.0) == desired);
    TestTrue(TEXT("Deviation below EPS: desired"),
        LeashVelocity(desired, FVector3d(1.0e-7, 0.0, 0.0), 100.0, 10.0) == desired);
    TestTrue(TEXT("NaN deviation: desired"), LeashVelocity(desired, NpcAiNaNVector(), 100.0, 10.0) == desired);
    TestTrue(TEXT("Infinite deviation: desired"),
        LeashVelocity(desired, FVector3d(NpcAiInf(), 0.0, 0.0), 100.0, 10.0) == desired);

    // Leash non-finite or <= 0: w = 1
    const FVector3d deviation(5.0, 0.0, 0.0);
    const FVector3d pull(-10.0, 0.0, 0.0);
    TestTrue(TEXT("Leash 0: pure pull"), NpcAiVecNear(LeashVelocity(desired, deviation, 0.0, 10.0), pull, NPCAI_EPS));
    TestTrue(TEXT("Leash < 0: pure pull"),
        NpcAiVecNear(LeashVelocity(desired, deviation, -100.0, 10.0), pull, NPCAI_EPS));
    TestTrue(TEXT("Leash NaN: pure pull"),
        NpcAiVecNear(LeashVelocity(desired, deviation, NpcAiNaN(), 10.0), pull, NPCAI_EPS));
    TestTrue(TEXT("Leash +inf: pure pull"),
        NpcAiVecNear(LeashVelocity(desired, deviation, NpcAiInf(), 10.0), pull, NPCAI_EPS));

    // maxSpeed non-finite or negative: s = 0
    TestTrue(TEXT("Speed NaN beyond the leash: zero"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(200.0, 0.0, 0.0), 100.0, NpcAiNaN()), FVector3d::ZeroVector,
            NPCAI_EPS));
    TestTrue(TEXT("Speed < 0 beyond the leash: zero"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(200.0, 0.0, 0.0), 100.0, -10.0), FVector3d::ZeroVector,
            NPCAI_EPS));
    TestTrue(TEXT("Speed NaN at w = 0.5: half the desired"),
        NpcAiVecNear(LeashVelocity(desired, FVector3d(90.0, 0.0, 0.0), 100.0, NpcAiNaN()), desired * 0.5, 1.0e-6));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// SelectTarget
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathSelectTargetNearestTest, "SOLTest.NpcAiMath.SelectTargetNearest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The nearest valid enemy inside the engage range is picked; self, friends, the dead and bad entries are skipped
bool FSOLNpcAiMathSelectTargetNearestTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self = FVector3d::ZeroVector;
    const TArray<FTargetCandidate> candidates = {
        NpcAiCandidate(1, FVector3d(500.0, 0.0, 0.0), 1),
        NpcAiCandidate(2, FVector3d(0.0, 300.0, 0.0), 1),
        NpcAiCandidate(3, FVector3d(100.0, 0.0, 0.0), 0),               // friendly
        NpcAiCandidate(4, FVector3d(50.0, 0.0, 0.0), 1, false),         // dead
        NpcAiCandidate(5, NpcAiNaNVector(), 1),                          // non-finite position
        NpcAiCandidate(6, FVector3d(NpcAiInf(), 0.0, 0.0), 1),          // non-finite position
        NpcAiCandidate(0, FVector3d(10.0, 0.0, 0.0), 1),                 // own index
        NpcAiCandidate(INDEX_NONE, FVector3d(20.0, 0.0, 0.0), 1) };      // no index
    TestEqual(TEXT("Nearest valid enemy"), SelectTarget(0, self, 0, INDEX_NONE, candidates, 1000.0, 1.2), 2);
    TestEqual(TEXT("Exactly at the engage range is outside (strict)"),
        SelectTarget(0, self, 0, INDEX_NONE, candidates, 300.0, 1.2), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Nothing in range"), SelectTarget(0, self, 0, INDEX_NONE, candidates, 250.0, 1.2),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Seen from faction 1, the friendly-to-0 ship is the enemy"),
        SelectTarget(9, self, 1, INDEX_NONE, candidates, 1000.0, 1.0), 3);

    // Candidate order does not matter
    TArray<FTargetCandidate> reversed = candidates;
    Algo::Reverse(reversed);
    TestEqual(TEXT("Reversed order"), SelectTarget(0, self, 0, INDEX_NONE, reversed, 1000.0, 1.2), 2);

    // Ties: the lower Index, in either order
    const TArray<FTargetCandidate> ties = { NpcAiCandidate(7, FVector3d(100.0, 0.0, 0.0), 1),
        NpcAiCandidate(3, FVector3d(0.0, -100.0, 0.0), 1) };
    TArray<FTargetCandidate> tiesReversed = ties;
    Algo::Reverse(tiesReversed);
    TestEqual(TEXT("Tie: lower Index"), SelectTarget(0, self, 0, INDEX_NONE, ties, 1000.0, 1.0), 3);
    TestEqual(TEXT("Tie, reversed: lower Index"), SelectTarget(0, self, 0, INDEX_NONE, tiesReversed, 1000.0, 1.0), 3);

    // Empty
    const TArray<FTargetCandidate> none;
    TestEqual(TEXT("No candidates"), SelectTarget(0, self, 0, INDEX_NONE, none, 1000.0, 1.0),
        static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathSelectTargetStickyTest, "SOLTest.NpcAiMath.SelectTargetSticky",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A still-valid current target is kept inside engageRange * keep, even when a nearer enemy exists
bool FSOLNpcAiMathSelectTargetStickyTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self = FVector3d::ZeroVector;
    const TArray<FTargetCandidate> candidates = { NpcAiCandidate(1, FVector3d(1100.0, 0.0, 0.0), 1),
        NpcAiCandidate(2, FVector3d(300.0, 0.0, 0.0), 1), NpcAiCandidate(3, FVector3d(500.0, 0.0, 0.0), 1) };
    TestEqual(TEXT("Current inside the engage range kept over a nearer one"),
        SelectTarget(0, self, 0, 3, candidates, 1000.0, 1.0), 3);
    TestEqual(TEXT("Current beyond the engage range but inside the keep range kept"),
        SelectTarget(0, self, 0, 1, candidates, 1000.0, 1.2), 1);
    TestEqual(TEXT("Current beyond the keep range dropped for the nearest"),
        SelectTarget(0, self, 0, 1, candidates, 1000.0, 1.05), 2);
    TestEqual(TEXT("keep = 1: beyond the engage range dropped"),
        SelectTarget(0, self, 0, 1, candidates, 1000.0, 1.0), 2);
    TestEqual(TEXT("keep < 1 counts as 1"), SelectTarget(0, self, 0, 3, candidates, 1000.0, 0.1), 3);
    TestEqual(TEXT("keep < 1 counts as 1 (beyond engage dropped)"),
        SelectTarget(0, self, 0, 1, candidates, 1000.0, 0.5), 2);
    TestEqual(TEXT("keep NaN counts as 1"), SelectTarget(0, self, 0, 1, candidates, 1000.0, NpcAiNaN()), 2);
    TestEqual(TEXT("keep +inf counts as 1"), SelectTarget(0, self, 0, 1, candidates, 1000.0, NpcAiInf()), 2);

    // A current target that is no longer a valid enemy is dropped
    const TArray<FTargetCandidate> deadCurrent = { NpcAiCandidate(3, FVector3d(500.0, 0.0, 0.0), 1, false),
        NpcAiCandidate(2, FVector3d(300.0, 0.0, 0.0), 1) };
    TestEqual(TEXT("Dead current dropped"), SelectTarget(0, self, 0, 3, deadCurrent, 1000.0, 1.2), 2);
    const TArray<FTargetCandidate> friendlyCurrent = { NpcAiCandidate(3, FVector3d(200.0, 0.0, 0.0), 0),
        NpcAiCandidate(2, FVector3d(300.0, 0.0, 0.0), 1) };
    TestEqual(TEXT("Friendly current dropped"), SelectTarget(0, self, 0, 3, friendlyCurrent, 1000.0, 1.2), 2);
    TestEqual(TEXT("Current absent from the candidates dropped"),
        SelectTarget(0, self, 0, 42, candidates, 1000.0, 1.2), 2);
    const TArray<FTargetCandidate> selfCurrent = { NpcAiCandidate(0, FVector3d(100.0, 0.0, 0.0), 1),
        NpcAiCandidate(2, FVector3d(300.0, 0.0, 0.0), 1) };
    TestEqual(TEXT("Current equal to self dropped"), SelectTarget(0, self, 0, 0, selfCurrent, 1000.0, 1.2), 2);
    const TArray<FTargetCandidate> lostCurrent = { NpcAiCandidate(3, FVector3d(5000.0, 0.0, 0.0), 1) };
    TestEqual(TEXT("Current out of keep range and no other enemy: none"),
        SelectTarget(0, self, 0, 3, lostCurrent, 1000.0, 1.2), static_cast<int32>(INDEX_NONE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathSelectTargetInvalidRangeTest,
    "SOLTest.NpcAiMath.SelectTargetInvalidRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A non-finite or non-positive engage range gives no target, even with a valid current target
bool FSOLNpcAiMathSelectTargetInvalidRangeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d self = FVector3d::ZeroVector;
    const TArray<FTargetCandidate> candidates = { NpcAiCandidate(2, FVector3d(300.0, 0.0, 0.0), 1) };
    TestEqual(TEXT("Range 0"), SelectTarget(0, self, 0, 2, candidates, 0.0, 1.2), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Range < 0"), SelectTarget(0, self, 0, 2, candidates, -1000.0, 1.2),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Range NaN"), SelectTarget(0, self, 0, 2, candidates, NpcAiNaN(), 1.2),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("Range +inf"), SelectTarget(0, self, 0, 2, candidates, NpcAiInf(), 1.2),
        static_cast<int32>(INDEX_NONE));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// LeadAimDirection
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLeadAimMeetsTest, "SOLTest.NpcAiMath.LeadAimMeets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A bolt fired along the lead aim meets a constant-velocity target at the smallest positive intercept time
bool FSOLNpcAiMathLeadAimMeetsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d origin = FVector3d::ZeroVector;

    // Stationary relative motion: straight at the target
    TestTrue(TEXT("Stationary target: straight at it"),
        NpcAiVecNear(LeadAimDirection(origin, origin, FVector3d(1000.0, 0.0, 0.0), origin, 500.0),
            FVector3d(1.0, 0.0, 0.0), NPCAI_EPS));
    TestTrue(TEXT("Shared velocity: straight at it"),
        NpcAiVecNear(LeadAimDirection(origin, FVector3d(0.0, 100.0, 0.0), FVector3d(1000.0, 0.0, 0.0),
            FVector3d(0.0, 100.0, 0.0), 500.0), FVector3d(1.0, 0.0, 0.0), NPCAI_EPS));

    // Crossing target: t = sqrt(1e6 / 240000)
    const double crossingT = FMath::Sqrt(1.0e6 / 240000.0);
    TestTrue(TEXT("Crossing target"),
        NpcAiVecNear(LeadAimDirection(origin, origin, FVector3d(1000.0, 0.0, 0.0), FVector3d(0.0, 100.0, 0.0), 500.0),
            FVector3d(1000.0, 100.0 * crossingT, 0.0).GetSafeNormal(), NPCAI_EPS));

    // |w| == muzzle speed (linear case): w = (-300, 400, 0), t = 1e6 / 6e5
    const double linearT = 1.0e6 / 6.0e5;
    TestTrue(TEXT("Target speed equal to muzzle speed (linear case)"),
        NpcAiVecNear(LeadAimDirection(origin, origin, FVector3d(1000.0, 0.0, 0.0), FVector3d(-300.0, 400.0, 0.0),
            500.0), FVector3d(1000.0 - 300.0 * linearT, 400.0 * linearT, 0.0).GetSafeNormal(), 1.0e-8));

    // Two positive roots: the smaller one is used
    const FVector3d fastR(1000.0, 0.0, 0.0);
    const FVector3d fastW(-600.0, 100.0, 0.0);
    double fastT = 0.0;
    TestTrue(TEXT("Reference root exists"), NpcAiSmallestPositiveRoot(fastR, fastW, 500.0, fastT));
    const FVector3d fastAim = LeadAimDirection(origin, origin, fastR, fastW, 500.0);
    TestTrue(TEXT("Approaching target: smallest positive root"),
        NpcAiVecNear(fastAim, (fastR + fastW * fastT).GetSafeNormal(), NPCAI_EPS));
    TestTrue(TEXT("Smallest root aims forward (+X), not at the later crossing"), fastAim.X > 0.0);

    // Randomized: the aim matches the reference root and the bolt meets the target
    FRandomStream stream(NPCAI_SEED + 2);
    bool bMatches = true;
    bool bMeets = true;
    bool bUnit = true;
    for (int32 sample = 0; sample < 2000; ++sample)
    {
        const FVector3d shooter = NpcAiRandomVector(stream, 0.0, 1.0e5);
        const FVector3d r = NpcAiRandomVector(stream, 100.0, 10000.0);
        const FVector3d shooterVelocity = NpcAiRandomVector(stream, 0.0, 300.0);
        const FVector3d targetVelocity = NpcAiRandomVector(stream, 0.0, 300.0);
        const double muzzle = 700.0 + 1300.0 * stream.FRand();
        const FVector3d aim = LeadAimDirection(shooter, shooterVelocity, shooter + r, targetVelocity, muzzle);
        const FVector3d w = targetVelocity - shooterVelocity;
        double t = 0.0;
        if (!NpcAiSmallestPositiveRoot(r, w, muzzle, t))
        {
            bMatches = false;
            continue;
        }
        bUnit = bUnit && FMath::Abs(aim.Size() - 1.0) <= NPCAI_EPS;
        bMatches = bMatches && NpcAiVecNear(aim, (r + w * t).GetSafeNormal(), 1.0e-8);

        // Bolt (shooter frame) at aim * muzzle * t meets the target at r + w t
        bMeets = bMeets && ((r + w * t) - aim * muzzle * t).Size() <= 1.0e-6 * r.Size();
    }
    TestTrue(TEXT("Aim matches the smallest positive root"), bMatches);
    TestTrue(TEXT("Bolt meets the target"), bMeets);
    TestTrue(TEXT("Unit length"), bUnit);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathLeadAimFallbackTest, "SOLTest.NpcAiMath.LeadAimFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No intercept or an invalid muzzle speed aims straight at the target; a coincident target gives zero
bool FSOLNpcAiMathLeadAimFallbackTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d origin = FVector3d::ZeroVector;
    const FVector3d shooter(10.0, -20.0, 5.0);
    const FVector3d target = shooter + FVector3d(0.0, 3000.0, 4000.0);
    const FVector3d straight(0.0, 0.6, 0.8);
    const FVector3d crossing(100.0, 0.0, 0.0);

    // Root-less cases
    TestTrue(TEXT("Receding faster than the bolt: straight"),
        NpcAiVecNear(LeadAimDirection(origin, origin, FVector3d(1000.0, 0.0, 0.0), FVector3d(600.0, 0.0, 0.0), 500.0),
            FVector3d(1.0, 0.0, 0.0), NPCAI_EPS));
    TestTrue(TEXT("Crossing faster than the bolt: straight"),
        NpcAiVecNear(LeadAimDirection(origin, origin, FVector3d(1000.0, 0.0, 0.0), FVector3d(0.0, 600.0, 0.0), 500.0),
            FVector3d(1.0, 0.0, 0.0), NPCAI_EPS));
    TestTrue(TEXT("Crossing at exactly muzzle speed (no linear root): straight"),
        NpcAiVecNear(LeadAimDirection(origin, origin, FVector3d(1000.0, 0.0, 0.0), FVector3d(0.0, 500.0, 0.0), 500.0),
            FVector3d(1.0, 0.0, 0.0), NPCAI_EPS));

    // Invalid muzzle speed
    TestTrue(TEXT("Muzzle 0: straight"),
        NpcAiVecNear(LeadAimDirection(shooter, origin, target, crossing, 0.0), straight, NPCAI_EPS));
    TestTrue(TEXT("Muzzle < 0: straight"),
        NpcAiVecNear(LeadAimDirection(shooter, origin, target, crossing, -500.0), straight, NPCAI_EPS));
    TestTrue(TEXT("Muzzle NaN: straight"),
        NpcAiVecNear(LeadAimDirection(shooter, origin, target, crossing, NpcAiNaN()), straight, NPCAI_EPS));
    TestTrue(TEXT("Muzzle +inf: straight"),
        NpcAiVecNear(LeadAimDirection(shooter, origin, target, crossing, NpcAiInf()), straight, NPCAI_EPS));

    // Coincident: zero
    TestTrue(TEXT("Target on the shooter: zero"),
        LeadAimDirection(shooter, origin, shooter, crossing, 500.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("Target within EPS: zero"),
        LeadAimDirection(shooter, origin, shooter + FVector3d(1.0e-7, 0.0, 0.0), crossing, 500.0)
            == FVector3d::ZeroVector);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// ShouldFire
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathShouldFireTest, "SOLTest.NpcAiMath.ShouldFire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The gun fires inside the range and the cone; a ship at rest may fire at any aim
bool FSOLNpcAiMathShouldFireTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d heading(1.0, 0.0, 0.0);

    // Returns the unit vector at the given angle from +X in the XY plane
    auto atAngle = [](const double angleRad) { return FVector3d(FMath::Cos(angleRad), FMath::Sin(angleRad), 0.0); };
    TestTrue(TEXT("Dead ahead"), ShouldFire(heading, heading, 100.0, 200.0, 0.1));
    TestTrue(TEXT("Inside the cone"), ShouldFire(heading, atAngle(0.05), 100.0, 200.0, 0.1));
    TestTrue(TEXT("On the cone edge"), ShouldFire(heading, atAngle(0.1), 100.0, 200.0, 0.1));
    TestFalse(TEXT("Outside the cone"), ShouldFire(heading, atAngle(0.15), 100.0, 200.0, 0.1));
    TestTrue(TEXT("Unnormalized heading and aim"),
        ShouldFire(heading * 50.0, atAngle(0.05) * 3.0, 100.0, 200.0, 0.1));
    TestTrue(TEXT("Cone PI: even behind"), ShouldFire(heading, -heading, 100.0, 200.0, UE_DOUBLE_PI));

    // Range
    TestTrue(TEXT("At exactly the fire range"), ShouldFire(heading, heading, 200.0, 200.0, 0.1));
    TestFalse(TEXT("Beyond the fire range"), ShouldFire(heading, heading, 200.001, 200.0, 0.1));
    TestFalse(TEXT("Distance 0"), ShouldFire(heading, heading, 0.0, 200.0, 0.1));
    TestFalse(TEXT("Distance < 0"), ShouldFire(heading, heading, -10.0, 200.0, 0.1));
    TestFalse(TEXT("Distance NaN"), ShouldFire(heading, heading, NpcAiNaN(), 200.0, 0.1));
    TestFalse(TEXT("Distance +inf"), ShouldFire(heading, heading, NpcAiInf(), 200.0, 0.1));
    TestFalse(TEXT("Range NaN"), ShouldFire(heading, heading, 100.0, NpcAiNaN(), 0.1));
    TestFalse(TEXT("Range +inf"), ShouldFire(heading, heading, 100.0, NpcAiInf(), 0.1));

    // Aim
    TestFalse(TEXT("Zero aim"), ShouldFire(heading, FVector3d::ZeroVector, 100.0, 200.0, 0.1));
    TestFalse(TEXT("Aim below EPS"), ShouldFire(heading, FVector3d(1.0e-7, 0.0, 0.0), 100.0, 200.0, 0.1));
    TestFalse(TEXT("NaN aim"), ShouldFire(heading, NpcAiNaNVector(), 100.0, 200.0, 0.1));

    // Heading at rest (zero, below EPS, or non-finite, which counts as zero): may fire any way
    TestTrue(TEXT("Zero heading: fires backwards"), ShouldFire(FVector3d::ZeroVector, -heading, 100.0, 200.0, 0.1));
    TestTrue(TEXT("Heading below EPS: fires"), ShouldFire(FVector3d(0.0, 1.0e-7, 0.0), -heading, 100.0, 200.0, 0.1));
    TestTrue(TEXT("NaN heading counts as zero: fires"), ShouldFire(NpcAiNaNVector(), -heading, 100.0, 200.0, 0.1));
    TestFalse(TEXT("Zero heading still respects the range"),
        ShouldFire(FVector3d::ZeroVector, heading, 300.0, 200.0, 0.1));

    // Cone non-finite or negative counts as 0
    TestTrue(TEXT("Cone NaN: dead ahead still fires"), ShouldFire(heading, heading, 100.0, 200.0, NpcAiNaN()));
    TestFalse(TEXT("Cone NaN: slightly off does not"), ShouldFire(heading, atAngle(0.01), 100.0, 200.0, NpcAiNaN()));
    TestTrue(TEXT("Cone < 0: dead ahead still fires"), ShouldFire(heading, heading, 100.0, 200.0, -1.0));
    TestFalse(TEXT("Cone < 0: slightly off does not"), ShouldFire(heading, atAngle(0.01), 100.0, 200.0, -1.0));
    TestFalse(TEXT("Cone +inf counts as 0"), ShouldFire(heading, atAngle(0.01), 100.0, 200.0, NpcAiInf()));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Damage, death and respawn
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathMakeAliveTest, "SOLTest.NpcAiMath.MakeAlive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MakeAlive gives full health, no timer, alive; invalid max health gives 1
bool FSOLNpcAiMathMakeAliveTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestTrue(TEXT("Max health 100"), NpcAiStatesEqual(MakeAlive(100.0), NpcAiState(100.0, 0.0, true)));
    TestTrue(TEXT("Max health 0.5"), NpcAiStatesEqual(MakeAlive(0.5), NpcAiState(0.5, 0.0, true)));
    TestTrue(TEXT("Max health 0 gives 1"), NpcAiStatesEqual(MakeAlive(0.0), NpcAiState(1.0, 0.0, true)));
    TestTrue(TEXT("Max health < 0 gives 1"), NpcAiStatesEqual(MakeAlive(-5.0), NpcAiState(1.0, 0.0, true)));
    TestTrue(TEXT("Max health NaN gives 1"), NpcAiStatesEqual(MakeAlive(NpcAiNaN()), NpcAiState(1.0, 0.0, true)));
    TestTrue(TEXT("Max health +inf gives 1"), NpcAiStatesEqual(MakeAlive(NpcAiInf()), NpcAiState(1.0, 0.0, true)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathApplyDamageTest, "SOLTest.NpcAiMath.ApplyDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Damage reduces health; reaching 0 kills and starts the respawn timer; dead ships and bad damage are ignored
bool FSOLNpcAiMathApplyDamageTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FCombatState full = NpcAiState(100.0, 0.0, true);

    // Survives
    const FDamageResult hit = ApplyDamage(full, 30.0, 5.0);
    TestTrue(TEXT("Survives with 70"), NpcAiStatesEqual(hit.State, NpcAiState(70.0, 0.0, true)));
    TestFalse(TEXT("Survives: not killed"), hit.bKilled);
    const FDamageResult keepsTimer = ApplyDamage(NpcAiState(100.0, 3.0, true), 30.0, 5.0);
    TestEqual(TEXT("Survives: RespawnS unchanged"), keepsTimer.State.RespawnS, 3.0);

    // Killed
    const FDamageResult exact = ApplyDamage(full, 100.0, 5.0);
    TestTrue(TEXT("Exact lethal damage kills"), NpcAiStatesEqual(exact.State, NpcAiState(0.0, 5.0, false)));
    TestTrue(TEXT("Exact lethal: bKilled"), exact.bKilled);
    const FDamageResult overkill = ApplyDamage(full, 250.0, 5.0);
    TestTrue(TEXT("Overkill clamps health to 0"), NpcAiStatesEqual(overkill.State, NpcAiState(0.0, 5.0, false)));
    TestTrue(TEXT("Overkill: bKilled"), overkill.bKilled);
    TestEqual(TEXT("Delay NaN counts as 0"), ApplyDamage(full, 200.0, NpcAiNaN()).State.RespawnS, 0.0);
    TestEqual(TEXT("Delay < 0 counts as 0"), ApplyDamage(full, 200.0, -4.0).State.RespawnS, 0.0);
    TestEqual(TEXT("Delay +inf counts as 0"), ApplyDamage(full, 200.0, NpcAiInf()).State.RespawnS, 0.0);

    // Ignored
    const FCombatState dead = NpcAiState(0.0, 4.0, false);
    const FDamageResult onDead = ApplyDamage(dead, 50.0, 5.0);
    TestTrue(TEXT("Dead: unchanged"), NpcAiStatesEqual(onDead.State, dead));
    TestFalse(TEXT("Dead: not killed again"), onDead.bKilled);
    for (const double damage : { 0.0, -10.0, NpcAiNaN(), NpcAiInf(), -NpcAiInf() })
    {
        const FDamageResult ignored = ApplyDamage(full, damage, 5.0);
        TestTrue(*FString::Printf(TEXT("Damage %f: unchanged"), damage), NpcAiStatesEqual(ignored.State, full));
        TestFalse(*FString::Printf(TEXT("Damage %f: not killed"), damage), ignored.bKilled);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepRespawnTest, "SOLTest.NpcAiMath.StepRespawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The respawn timer counts down and the ship comes back at MakeAlive(maxHealth) once it reaches 0
bool FSOLNpcAiMathStepRespawnTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FCombatState dead = NpcAiState(0.0, 5.0, false);

    // Count down
    const FRespawnStep waiting = StepRespawn(dead, 2.0, 100.0);
    TestTrue(TEXT("Still dead with 3 s left"), NpcAiStatesEqual(waiting.State, NpcAiState(0.0, 3.0, false)));
    TestFalse(TEXT("Not respawned yet"), waiting.bRespawned);
    const FRespawnStep back = StepRespawn(waiting.State, 3.0, 100.0);
    TestTrue(TEXT("Timer reaches exactly 0: respawned"), back.bRespawned);
    TestTrue(TEXT("Respawned at MakeAlive"), NpcAiStatesEqual(back.State, MakeAlive(100.0)));
    const FRespawnStep overshoot = StepRespawn(dead, 9.0, 100.0);
    TestTrue(TEXT("Overshoot respawns"), overshoot.bRespawned && NpcAiStatesEqual(overshoot.State, MakeAlive(100.0)));

    // dt 0
    const FRespawnStep zeroDtWaiting = StepRespawn(dead, 0.0, 100.0);
    TestFalse(TEXT("dt 0 with time left: not respawned"), zeroDtWaiting.bRespawned);
    TestTrue(TEXT("dt 0 with time left: unchanged timer"), NpcAiStatesEqual(zeroDtWaiting.State, dead));
    const FRespawnStep zeroDtDue = StepRespawn(NpcAiState(0.0, 0.0, false), 0.0, 100.0);
    TestTrue(TEXT("dt 0 with timer 0: respawned"), zeroDtDue.bRespawned);
    TestTrue(TEXT("dt 0 with timer 0: alive"), NpcAiStatesEqual(zeroDtDue.State, MakeAlive(100.0)));
    TestTrue(TEXT("dt 0 with a negative timer: respawned"),
        StepRespawn(NpcAiState(0.0, -1.0, false), 0.0, 100.0).bRespawned);

    // Invalid max health on respawn
    TestTrue(TEXT("Respawn with NaN max health: health 1"),
        NpcAiStatesEqual(StepRespawn(dead, 10.0, NpcAiNaN()).State, NpcAiState(1.0, 0.0, true)));

    // Unchanged
    const FCombatState alive = NpcAiState(40.0, 0.0, true);
    const FRespawnStep aliveStep = StepRespawn(alive, 1.0, 100.0);
    TestTrue(TEXT("Alive: unchanged"), NpcAiStatesEqual(aliveStep.State, alive));
    TestFalse(TEXT("Alive: not respawned"), aliveStep.bRespawned);
    for (const double dt : { -1.0, NpcAiNaN(), NpcAiInf(), -NpcAiInf() })
    {
        const FRespawnStep invalid = StepRespawn(dead, dt, 100.0);
        TestTrue(*FString::Printf(TEXT("dt %f: unchanged"), dt), NpcAiStatesEqual(invalid.State, dead));
        TestFalse(*FString::Printf(TEXT("dt %f: not respawned"), dt), invalid.bRespawned);
    }

    // Full cycle: damage to death, wait out the delay, come back at full health
    FCombatState state = MakeAlive(100.0);
    const FDamageResult killed = ApplyDamage(state, 150.0, 1.0);
    state = killed.State;
    int32 steps = 0;
    bool bRespawned = false;
    while (!bRespawned && steps < 100)
    {
        const FRespawnStep step = StepRespawn(state, 0.25, 100.0);
        state = step.State;
        bRespawned = step.bRespawned;
        ++steps;
    }
    TestEqual(TEXT("Cycle: respawned after 4 quarter-second steps"), steps, 4);
    TestTrue(TEXT("Cycle: full health"), NpcAiStatesEqual(state, MakeAlive(100.0)));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Statistical hit model and model selection
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathTransverseSpeedTest, "SOLTest.NpcAiMath.TransverseSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The transverse speed is the velocity's component perpendicular to the position
bool FSOLNpcAiMathTransverseSpeedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestNearlyEqual(TEXT("Mixed velocity"), TransverseSpeedMps(FVector3d(100.0, 0.0, 0.0), FVector3d(3.0, 4.0, 0.0)),
        4.0, NPCAI_EPS);
    TestNearlyEqual(TEXT("Independent of the position's length"),
        TransverseSpeedMps(FVector3d(1.0e9, 0.0, 0.0), FVector3d(3.0, 4.0, 12.0)), 4.0 * FMath::Sqrt(10.0),
        NPCAI_EPS);
    TestNearlyEqual(TEXT("Radial velocity: 0"),
        TransverseSpeedMps(FVector3d(0.0, 50.0, 0.0), FVector3d(0.0, -7.0, 0.0)), 0.0, NPCAI_EPS);
    TestNearlyEqual(TEXT("Purely transverse"),
        TransverseSpeedMps(FVector3d(3.0, 4.0, 0.0), FVector3d(-4.0, 3.0, 0.0)), 5.0, NPCAI_EPS);
    TestNearlyEqual(TEXT("Zero position: |velocity|"),
        TransverseSpeedMps(FVector3d::ZeroVector, FVector3d(3.0, 4.0, 0.0)), 5.0, NPCAI_EPS);
    TestNearlyEqual(TEXT("Position below EPS: |velocity|"),
        TransverseSpeedMps(FVector3d(1.0e-7, 0.0, 0.0), FVector3d(3.0, 4.0, 0.0)), 5.0, NPCAI_EPS);
    TestEqual(TEXT("NaN position: 0"), TransverseSpeedMps(NpcAiNaNVector(), FVector3d(3.0, 4.0, 0.0)), 0.0);
    TestEqual(TEXT("Infinite position: 0"),
        TransverseSpeedMps(FVector3d(NpcAiInf(), 0.0, 0.0), FVector3d(3.0, 4.0, 0.0)), 0.0);
    TestEqual(TEXT("NaN velocity: 0"), TransverseSpeedMps(FVector3d(100.0, 0.0, 0.0), NpcAiNaNVector()), 0.0);
    TestEqual(TEXT("Infinite velocity: 0"),
        TransverseSpeedMps(FVector3d(100.0, 0.0, 0.0), FVector3d(0.0, NpcAiInf(), 0.0)), 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathHitProbabilityFormulaTest, "SOLTest.NpcAiMath.HitProbabilityFormula",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hit probability is R^2 / (R^2 + miss^2) with the contract's miss distance
bool FSOLNpcAiMathHitProbabilityFormulaTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;

    // R = 10, spread 0.001, muzzle 1000, d = 5000, vT = 100: tf = 5, miss^2 = 25 + (0.15 * 100 * 5)^2 = 25 + 75^2
    TestNearlyEqual(TEXT("Worked example"), StatisticalHitProbability(5000.0, 100.0, 10.0, 0.001, 1000.0, 10000.0),
        100.0 / (100.0 + 25.0 + 5625.0), NPCAI_EPS);
    TestNearlyEqual(TEXT("Negative transverse speed by its absolute value"),
        StatisticalHitProbability(5000.0, -100.0, 10.0, 0.001, 1000.0, 10000.0), 100.0 / (100.0 + 25.0 + 5625.0),
        NPCAI_EPS);
    TestNearlyEqual(TEXT("Negative spread counts as 0"),
        StatisticalHitProbability(5000.0, 100.0, 10.0, -0.5, 1000.0, 10000.0), 100.0 / (100.0 + 5625.0), NPCAI_EPS);
    TestNearlyEqual(TEXT("Spread only"), StatisticalHitProbability(2000.0, 0.0, 4.0, 0.002, 1000.0, 10000.0),
        16.0 / (16.0 + 16.0), NPCAI_EPS);
    TestNearlyEqual(TEXT("Perfect aim, still target: 1"),
        StatisticalHitProbability(2000.0, 0.0, 4.0, 0.0, 1000.0, 10000.0), 1.0, NPCAI_EPS);
    TestNearlyEqual(TEXT("Distance 0: 1"), StatisticalHitProbability(0.0, 500.0, 4.0, 0.1, 1000.0, 10000.0), 1.0,
        NPCAI_EPS);
    TestNearlyEqual(TEXT("At exactly the fire range: valid"),
        StatisticalHitProbability(10000.0, 0.0, 10.0, 0.001, 1000.0, 10000.0), 100.0 / (100.0 + 100.0), NPCAI_EPS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathHitProbabilityInvalidTest, "SOLTest.NpcAiMath.HitProbabilityInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite inputs, out-of-range distances, a non-positive hit radius or muzzle speed give 0
bool FSOLNpcAiMathHitProbabilityInvalidTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestEqual(TEXT("Distance < 0"), StatisticalHitProbability(-1.0, 0.0, 10.0, 0.001, 1000.0, 10000.0), 0.0);
    TestEqual(TEXT("Distance > range"), StatisticalHitProbability(10000.5, 0.0, 10.0, 0.001, 1000.0, 10000.0), 0.0);
    TestEqual(TEXT("Hit radius 0"), StatisticalHitProbability(100.0, 0.0, 0.0, 0.001, 1000.0, 10000.0), 0.0);
    TestEqual(TEXT("Hit radius < 0"), StatisticalHitProbability(100.0, 0.0, -10.0, 0.001, 1000.0, 10000.0), 0.0);
    TestEqual(TEXT("Muzzle 0"), StatisticalHitProbability(100.0, 0.0, 10.0, 0.001, 0.0, 10000.0), 0.0);
    TestEqual(TEXT("Muzzle < 0"), StatisticalHitProbability(100.0, 0.0, 10.0, 0.001, -1000.0, 10000.0), 0.0);

    // Every input non-finite in turn
    const double good[6] = { 100.0, 10.0, 10.0, 0.001, 1000.0, 10000.0 };
    for (int32 slot = 0; slot < 6; ++slot)
    {
        for (const double bad : { NpcAiNaN(), NpcAiInf(), -NpcAiInf() })
        {
            double args[6] = { good[0], good[1], good[2], good[3], good[4], good[5] };
            args[slot] = bad;
            TestEqual(*FString::Printf(TEXT("Input %d = %f gives 0"), slot, bad),
                StatisticalHitProbability(args[0], args[1], args[2], args[3], args[4], args[5]), 0.0);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathHitProbabilityMonotonicTest, "SOLTest.NpcAiMath.HitProbabilityMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hit probability lies in [0, 1] and never rises with distance, spread or transverse speed
bool FSOLNpcAiMathHitProbabilityMonotonicTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    bool bInRange = true;
    bool bDistance = true;
    bool bSpread = true;
    bool bTransverse = true;
    const double range = 20000.0;
    for (const double spread : { 0.0, 0.0005, 0.005, 0.05 })
    {
        for (const double transverse : { 0.0, 10.0, 300.0, 3000.0 })
        {
            double previous = 2.0;
            for (int32 step = 0; step <= 200; ++step)
            {
                const double distance = range * step / 200.0;
                const double p = StatisticalHitProbability(distance, transverse, 8.0, spread, 1500.0, range);
                bInRange = bInRange && p >= 0.0 && p <= 1.0;
                bDistance = bDistance && p <= previous + NPCAI_EPS;
                previous = p;

                // More spread and more transverse speed never help
                bSpread = bSpread
                    && StatisticalHitProbability(distance, transverse, 8.0, spread * 2.0 + 0.001, 1500.0, range)
                        <= p + NPCAI_EPS;
                bTransverse = bTransverse
                    && StatisticalHitProbability(distance, transverse * 2.0 + 1.0, 8.0, spread, 1500.0, range)
                        <= p + NPCAI_EPS;
            }
        }
    }
    TestTrue(TEXT("In [0, 1]"), bInRange);
    TestTrue(TEXT("Non-increasing in distance"), bDistance);
    TestTrue(TEXT("Non-increasing in spread"), bSpread);
    TestTrue(TEXT("Non-increasing in transverse speed"), bTransverse);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathRollHitTest, "SOLTest.NpcAiMath.RollHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RollHit is unit < clamp(probability, 0, 1), with NaN probability as 0 and NaN unit false
bool FSOLNpcAiMathRollHitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestTrue(TEXT("0.3 < 0.5"), RollHit(0.3, 0.5));
    TestFalse(TEXT("0.5 is not < 0.5"), RollHit(0.5, 0.5));
    TestFalse(TEXT("0.7 >= 0.5"), RollHit(0.7, 0.5));
    TestTrue(TEXT("Probability 2 clamps to 1"), RollHit(0.999, 2.0));
    TestFalse(TEXT("Probability -1 clamps to 0"), RollHit(0.0, -1.0));
    TestFalse(TEXT("Probability 0: never at unit 0"), RollHit(0.0, 0.0));
    TestTrue(TEXT("Probability 1: hits at unit 0"), RollHit(0.0, 1.0));
    TestFalse(TEXT("NaN probability counts as 0"), RollHit(0.0, NpcAiNaN()));
    TestTrue(TEXT("+inf probability clamps to 1"), RollHit(0.5, NpcAiInf()));
    TestFalse(TEXT("NaN unit: false"), RollHit(NpcAiNaN(), 1.0));
    TestFalse(TEXT("NaN unit and NaN probability: false"), RollHit(NpcAiNaN(), NpcAiNaN()));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathCombatModelFromIntTest, "SOLTest.NpcAiMath.CombatModelFromInt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// 0, 1 and 2 map to Bolts, Statistical and Hybrid; anything else is Bolts
bool FSOLNpcAiMathCombatModelFromIntTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestTrue(TEXT("0 is Bolts"), CombatModelFromInt(0) == ECombatModel::Bolts);
    TestTrue(TEXT("1 is Statistical"), CombatModelFromInt(1) == ECombatModel::Statistical);
    TestTrue(TEXT("2 is Hybrid"), CombatModelFromInt(2) == ECombatModel::Hybrid);
    TestTrue(TEXT("-1 is Bolts"), CombatModelFromInt(-1) == ECombatModel::Bolts);
    TestTrue(TEXT("3 is Bolts"), CombatModelFromInt(3) == ECombatModel::Bolts);
    TestTrue(TEXT("255 is Bolts"), CombatModelFromInt(255) == ECombatModel::Bolts);
    TestTrue(TEXT("258 (2 mod 256) is Bolts"), CombatModelFromInt(258) == ECombatModel::Bolts);
    TestTrue(TEXT("INT32_MAX is Bolts"), CombatModelFromInt(MAX_int32) == ECombatModel::Bolts);
    TestTrue(TEXT("INT32_MIN is Bolts"), CombatModelFromInt(MIN_int32) == ECombatModel::Bolts);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathUsesRealBoltsTest, "SOLTest.NpcAiMath.UsesRealBolts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Bolts always, Statistical never, Hybrid only strictly inside a valid visual radius; invalid enums never
bool FSOLNpcAiMathUsesRealBoltsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestTrue(TEXT("Bolts: near"), UsesRealBolts(ECombatModel::Bolts, 10.0, 1000.0));
    TestTrue(TEXT("Bolts: far"), UsesRealBolts(ECombatModel::Bolts, 1.0e9, 1000.0));
    TestTrue(TEXT("Bolts: NaN distance and radius"), UsesRealBolts(ECombatModel::Bolts, NpcAiNaN(), NpcAiNaN()));
    TestFalse(TEXT("Statistical: near"), UsesRealBolts(ECombatModel::Statistical, 10.0, 1000.0));
    TestFalse(TEXT("Statistical: far"), UsesRealBolts(ECombatModel::Statistical, 1.0e9, 1000.0));

    // Hybrid
    TestTrue(TEXT("Hybrid: inside"), UsesRealBolts(ECombatModel::Hybrid, 100.0, 1000.0));
    TestFalse(TEXT("Hybrid: exactly at the radius (strict)"), UsesRealBolts(ECombatModel::Hybrid, 1000.0, 1000.0));
    TestFalse(TEXT("Hybrid: outside"), UsesRealBolts(ECombatModel::Hybrid, 2000.0, 1000.0));
    TestTrue(TEXT("Hybrid: distance 0"), UsesRealBolts(ECombatModel::Hybrid, 0.0, 1000.0));
    TestTrue(TEXT("Hybrid: negative distance counts as 0"), UsesRealBolts(ECombatModel::Hybrid, -5.0, 1000.0));
    TestFalse(TEXT("Hybrid: NaN distance"), UsesRealBolts(ECombatModel::Hybrid, NpcAiNaN(), 1000.0));
    TestFalse(TEXT("Hybrid: +inf distance"), UsesRealBolts(ECombatModel::Hybrid, NpcAiInf(), 1000.0));
    TestFalse(TEXT("Hybrid: -inf distance"), UsesRealBolts(ECombatModel::Hybrid, -NpcAiInf(), 1000.0));
    TestFalse(TEXT("Hybrid: radius 0"), UsesRealBolts(ECombatModel::Hybrid, 0.0, 0.0));
    TestFalse(TEXT("Hybrid: radius < 0"), UsesRealBolts(ECombatModel::Hybrid, 0.0, -1000.0));
    TestFalse(TEXT("Hybrid: radius NaN"), UsesRealBolts(ECombatModel::Hybrid, 10.0, NpcAiNaN()));
    TestFalse(TEXT("Hybrid: radius +inf"), UsesRealBolts(ECombatModel::Hybrid, 10.0, NpcAiInf()));

    // Invalid enum values
    TestFalse(TEXT("Enum 3: false"), UsesRealBolts(static_cast<ECombatModel>(3), 10.0, 1000.0));
    TestFalse(TEXT("Enum 7: false"), UsesRealBolts(static_cast<ECombatModel>(7), 10.0, 1000.0));
    TestFalse(TEXT("Enum 255: false"), UsesRealBolts(static_cast<ECombatModel>(255), 10.0, 1000.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMidLeashTest, "SOLTest.NpcAiMath.MidLeashCoversClasses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Mid tier's leash is at least every class's leash, so a Full ship demoted to Mid is never pulled in at once
bool FSOLNpcAiMidLeashTest::RunTest(const FString& /*parameters*/)
{
    for (int32 classIndex = 0; classIndex < SOLNpcAi::ClassCount(); ++classIndex)
    {
        TestTrue(*FString::Printf(TEXT("Class %d leash within the Mid leash"), classIndex),
            SOLNpcAi::GetClass(classIndex).LeashM <= SOL::NPC_MID_DEVIATION_LEASH_M);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
