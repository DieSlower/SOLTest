/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcAiMath.h"
#include "Flight/SOLNewtonianFlight.h"
#include "SOLConstants.h"

#include "Tests/SOLTestHelpers.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #11 (Part 10d) pure NPC AI logic, written against the 10d contract in
// Docs/Plans/11-scale-and-performance-demo-plan.md before any production code existed. Re-validated for issue #18
// Part 18c (Newtonian NPC motion, "## 18c contract" in Docs/Plans/18-newtonian-flight-core-plan.md): LimitSteering
// and the Mid leash constant are gone, the class table gains thrust and attitude fields, and the Mid / Full tier
// motion functions are new.

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
        return a.MaxSpeedMps == b.MaxSpeedMps && a.PilotKind == b.PilotKind && a.ForwardG == b.ForwardG
            && a.ReverseG == b.ReverseG && a.LateralG == b.LateralG && a.VerticalG == b.VerticalG
            && a.ClassGCapG == b.ClassGCapG && a.MaxAngularAccelRadPerS2 == b.MaxAngularAccelRadPerS2
            && a.MaxAngularRateRadPerS == b.MaxAngularRateRadPerS && a.LeashM == b.LeashM
            && a.HitRadiusM == b.HitRadiusM && a.MaxHealth == b.MaxHealth && a.FireRatePerS == b.FireRatePerS
            && a.MuzzleSpeedMps == b.MuzzleSpeedMps
            && a.BoltDamage == b.BoltDamage && a.BoltLifetimeS == b.BoltLifetimeS && a.EngageRangeM == b.EngageRangeM
            && a.FireRangeM == b.FireRangeM && a.FireConeRad == b.FireConeRad && a.StandoffM == b.StandoffM
            && a.SeparationRadiusM == b.SeparationRadiusM && a.AimSpreadRad == b.AimSpreadRad
            && a.RespawnDelayS == b.RespawnDelayS && a.GunGimbalRad == b.GunGimbalRad;
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

    // Standard gravity, the unit of the class table's G fields
    constexpr double NPCAI_G0 = SOLNewtonian::STANDARD_GRAVITY_MPS2;

    //////////////////////////////////////////////////////////////////////////
    // Returns the minimum rest-to-rest time over a distance at an acceleration with a cruise-speed cap (the bang-bang
    // time 2 sqrt(d / a) when the cap is not reached; independent of the code under test)
    double NpcAiIdealRestToRestS(const double distanceM, const double accelMps2, const double capMps)
    {
        const double peakMps = FMath::Sqrt(distanceM * accelMps2);
        if (FMath::IsFinite(capMps) && capMps > 0.0 && peakMps > capMps)
        {
            return distanceM / capMps + capMps / accelMps2;
        }
        return 2.0 * FMath::Sqrt(distanceM / accelMps2);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rest-to-rest bang-bang rotation time with a rate limit (independent of the code under test)
    double NpcAiIdealRotationS(const double angleRad, const double rateRadPerS, const double alphaRadPerS2)
    {
        if (angleRad >= rateRadPerS * rateRadPerS / alphaRadPerS2)
        {
            return angleRad / rateRadPerS + rateRadPerS / alphaRadPerS2;
        }
        return 2.0 * FMath::Sqrt(angleRad / alphaRadPerS2);
    }

    // Outcome of a simulated Mid-tier return to the rail
    struct FNpcAiMidRun
    {
        bool bAtRest = false;
        bool bFinite = true;
        double TimeS = 0.0;             // Time at which the step first reported rest
        double MinAlongM = 0.0;         // Smallest deviation component along the initial deviation direction
    };

    //////////////////////////////////////////////////////////////////////////
    // Runs StepMidReturn from a deviation and velocity until it reports rest or maxTimeS passes
    FNpcAiMidRun NpcAiRunMidReturn(const FVector3d& deviationM, const FVector3d& velocityMps,
        const FSOLGuidanceParams& params, const double dt, const double maxTimeS)
    {
        FNpcAiMidRun run;
        const FVector3d along = deviationM.GetSafeNormal();
        FVector3d position = deviationM;
        FVector3d velocity = velocityMps;
        run.MinAlongM = FVector3d::DotProduct(position, along);
        const int32 maxSteps = FMath::CeilToInt32(maxTimeS / dt);
        for (int32 step = 1; step <= maxSteps; ++step)
        {
            // One step; track finiteness and the overshoot past the rail along the initial direction
            const SOLNpcAi::FMidReturnStep next = SOLNpcAi::StepMidReturn(position, velocity, params, dt);
            run.bFinite = run.bFinite && SOLTestHelpers::IsFiniteVector(next.DeviationM)
                && SOLTestHelpers::IsFiniteVector(next.DeviationVelocityMps);
            position = next.DeviationM;
            velocity = next.DeviationVelocityMps;
            run.MinAlongM = FMath::Min(run.MinAlongM, FVector3d::DotProduct(position, along));
            if (next.bAtRest)
            {
                run.bAtRest = true;
                run.TimeS = step * dt;
                break;
            }
        }
        return run;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a random unit orientation
    FQuat4d NpcAiRandomOrientation(FRandomStream& stream)
    {
        return FQuat4d(FVector3d(stream.VRand()).GetSafeNormal(), UE_DOUBLE_TWO_PI * stream.FRand());
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
    TestEqual(TEXT("FULL_HOLD_TIME_CONSTANT_S"), FULL_HOLD_TIME_CONSTANT_S, 0.5);
    TestEqual(TEXT("NOSE_ON_TARGET_RANGE_SCALE"), NOSE_ON_TARGET_RANGE_SCALE, 1.5);
    TestEqual(TEXT("HULL_HEADING_MIN_SPEED_MPS"), HULL_HEADING_MIN_SPEED_MPS, 1.0);
    TestEqual(TEXT("MID_ARRIVAL_RADIUS_M"), MID_ARRIVAL_RADIUS_M, 1.0);
    TestEqual(TEXT("MID_ARRIVAL_SPEED_MPS"), MID_ARRIVAL_SPEED_MPS, 0.5);
    TestEqual(TEXT("CLASS_SPIN_RATE_SCALE"), CLASS_SPIN_RATE_SCALE, 2.0);
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
        const double fields[] = { params.MaxSpeedMps, params.ForwardG, params.ReverseG, params.LateralG,
            params.VerticalG, params.ClassGCapG, params.MaxAngularAccelRadPerS2.X, params.MaxAngularAccelRadPerS2.Y,
            params.MaxAngularAccelRadPerS2.Z, params.MaxAngularRateRadPerS.X, params.MaxAngularRateRadPerS.Y,
            params.MaxAngularRateRadPerS.Z, params.LeashM, params.HitRadiusM, params.MaxHealth, params.FireRatePerS,
            params.MuzzleSpeedMps, params.BoltDamage, params.BoltLifetimeS, params.EngageRangeM, params.FireRangeM,
            params.FireConeRad, params.StandoffM, params.SeparationRadiusM, params.RespawnDelayS };
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
        TestTrue(*FString::Printf(TEXT("Class %d: GunGimbal finite and in [0, PI]"), classIndex),
            FMath::IsFinite(params.GunGimbalRad) && params.GunGimbalRad >= 0.0 && params.GunGimbalRad <= UE_DOUBLE_PI);
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

// ---------------------------------------------------------------------------------------------------------------------
// Issue #18 Part 18c: class thrust table and envelope
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathClassTableTest, "SOLTest.NpcAiMath.ClassTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The three classes carry the SDD 18 section 3.7 pilot kinds, G values, caps, angular accelerations and rates
bool FSOLNpcAiMathClassTableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TestEqual(TEXT("Three classes"), ClassCount(), 3);

    // One expected row of the class table
    struct FRow
    {
        const TCHAR* Name;
        ESOLPilotKind Kind;
        double ForwardG;
        double ReverseG;
        double LateralG;
        double VerticalG;
        double CapG;
        FVector3d Alpha;
        FVector3d Rate;
    };
    const FRow rows[] = {
        { TEXT("Fighter"), ESOLPilotKind::Human, 8.0, 4.0, 4.0, 4.0, 6.0, FVector3d(6.0, 4.0, 4.0),
            FVector3d(3.0, 1.5, 1.5) },
        { TEXT("Gunship"), ESOLPilotKind::Human, 4.0, 2.0, 2.0, 2.0, 4.0, FVector3d(2.0, 1.5, 1.5),
            FVector3d(1.2, 0.8, 0.8) },
        { TEXT("Interceptor"), ESOLPilotKind::AI, 15.0, 8.0, 8.0, 8.0, 20.0, FVector3d(8.0, 6.0, 6.0),
            FVector3d(4.0, 2.5, 2.5) } };
    for (int32 classIndex = 0; classIndex < 3; ++classIndex)
    {
        const FClassParams& params = GetClass(classIndex);
        const FRow& row = rows[classIndex];
        TestTrue(*FString::Printf(TEXT("%s: pilot kind"), row.Name), params.PilotKind == row.Kind);
        TestEqual(*FString::Printf(TEXT("%s: ForwardG"), row.Name), params.ForwardG, row.ForwardG);
        TestEqual(*FString::Printf(TEXT("%s: ReverseG"), row.Name), params.ReverseG, row.ReverseG);
        TestEqual(*FString::Printf(TEXT("%s: LateralG"), row.Name), params.LateralG, row.LateralG);
        TestEqual(*FString::Printf(TEXT("%s: VerticalG"), row.Name), params.VerticalG, row.VerticalG);
        TestEqual(*FString::Printf(TEXT("%s: ClassGCapG"), row.Name), params.ClassGCapG, row.CapG);
        TestTrue(*FString::Printf(TEXT("%s: angular acceleration (roll, pitch, yaw)"), row.Name),
            params.MaxAngularAccelRadPerS2 == row.Alpha);
        TestTrue(*FString::Printf(TEXT("%s: angular rate (roll, pitch, yaw)"), row.Name),
            params.MaxAngularRateRadPerS == row.Rate);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathClassEnvelopeTest, "SOLTest.NpcAiMath.ClassEnvelope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ClassEnvelope turns the G table into accelerations, no boost, the effective total cap, and the class spin clamp
bool FSOLNpcAiMathClassEnvelopeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const double expectedCapG[] = { 6.0, 4.0, 20.0 };
    const double expectedSpin[] = { 6.0, 2.4, 8.0 };
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        const FClassParams& params = GetClass(classIndex);
        const FSOLThrustEnvelope env = ClassEnvelope(params);
        const FString name = FString::Printf(TEXT("Class %d"), classIndex);
        TestNearlyEqual(*(name + TEXT(": forward")), env.ForwardAccelMps2, params.ForwardG * NPCAI_G0, NPCAI_EPS);
        TestNearlyEqual(*(name + TEXT(": reverse")), env.ReverseAccelMps2, params.ReverseG * NPCAI_G0, NPCAI_EPS);
        TestNearlyEqual(*(name + TEXT(": lateral")), env.LateralAccelMps2, params.LateralG * NPCAI_G0, NPCAI_EPS);
        TestNearlyEqual(*(name + TEXT(": vertical")), env.VerticalAccelMps2, params.VerticalG * NPCAI_G0, NPCAI_EPS);
        TestEqual(*(name + TEXT(": no boost")), env.BoostMultiplier, 1.0);
        TestTrue(*(name + TEXT(": angular acceleration copied")),
            env.MaxAngularAccelRadPerS2 == params.MaxAngularAccelRadPerS2);
        TestTrue(*(name + TEXT(": angular rate copied")), env.MaxAngularRateRadPerS == params.MaxAngularRateRadPerS);
        TestNearlyEqual(*(name + TEXT(": spin clamp = 2 * max rate")), env.MaxSpinRateRadPerS,
            CLASS_SPIN_RATE_SCALE * params.MaxAngularRateRadPerS.GetMax(), NPCAI_EPS);
        TestEqual(*(name + TEXT(": max speed is the global cap")), env.MaxSpeedMps, SOL::MAX_SPEED_CAP_MPS);
        TestNearlyEqual(*(name + TEXT(": total cap = effective G cap")), env.MaxTotalAccelMps2,
            SOLNewtonian::EffectiveGCapG(params.PilotKind, params.ClassGCapG) * NPCAI_G0, NPCAI_EPS);
        if (classIndex < 3)
        {
            TestNearlyEqual(*(name + TEXT(": pinned total cap")), env.MaxTotalAccelMps2,
                expectedCapG[classIndex] * NPCAI_G0, NPCAI_EPS);
            TestNearlyEqual(*(name + TEXT(": pinned spin clamp")), env.MaxSpinRateRadPerS, expectedSpin[classIndex],
                NPCAI_EPS);
        }
        if (params.PilotKind == ESOLPilotKind::Human)
        {
            TestTrue(*(name + TEXT(": a human class never exceeds HUMAN_MAX_G")),
                env.MaxTotalAccelMps2 <= SOLNewtonian::HUMAN_MAX_G * NPCAI_G0 * (1.0 + NPCAI_EPS));
        }
    }

    // Synthetic classes with a 12 G structural cap: a human pilot is held to HUMAN_MAX_G, an AI is not
    FClassParams human = GetClass(0);
    human.PilotKind = ESOLPilotKind::Human;
    human.ClassGCapG = 12.0;
    TestNearlyEqual(TEXT("Human with a 12 G cap: clamped to HUMAN_MAX_G"), ClassEnvelope(human).MaxTotalAccelMps2,
        SOLNewtonian::HUMAN_MAX_G * NPCAI_G0, NPCAI_EPS);
    FClassParams ai = GetClass(0);
    ai.PilotKind = ESOLPilotKind::AI;
    ai.ClassGCapG = 12.0;
    TestNearlyEqual(TEXT("AI with a 12 G cap keeps 12 G"), ClassEnvelope(ai).MaxTotalAccelMps2, 12.0 * NPCAI_G0,
        NPCAI_EPS);
    FClassParams humanInterceptor = GetClass(2);
    humanInterceptor.PilotKind = ESOLPilotKind::Human;
    TestNearlyEqual(TEXT("The interceptor flown by a human: clamped to HUMAN_MAX_G"),
        ClassEnvelope(humanInterceptor).MaxTotalAccelMps2, SOLNewtonian::HUMAN_MAX_G * NPCAI_G0, NPCAI_EPS);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Issue #18 Part 18c: Mid tier (point-mass return to the rail)
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathMidReturnAccelTest, "SOLTest.NpcAiMath.MidReturnAccel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Mid return acceleration is min(LateralG, effective G cap) * g0; a bad LateralG gives 0
bool FSOLNpcAiMathMidReturnAccelTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const double expectedG[] = { 4.0, 2.0, 8.0 };
    for (int32 classIndex = 0; classIndex < 3; ++classIndex)
    {
        TestNearlyEqual(*FString::Printf(TEXT("Class %d"), classIndex), MidReturnAccelMps2(GetClass(classIndex)),
            expectedG[classIndex] * NPCAI_G0, NPCAI_EPS);
    }

    // Non-finite or <= 0 LateralG gives 0
    for (const double lateralG : { NpcAiNaN(), NpcAiInf(), -NpcAiInf(), 0.0, -2.0 })
    {
        FClassParams params = GetClass(0);
        params.LateralG = lateralG;
        TestEqual(*FString::Printf(TEXT("LateralG %f gives 0"), lateralG), MidReturnAccelMps2(params), 0.0);
    }

    // A cap below the lateral thrust wins
    FClassParams lowCap = GetClass(0);
    lowCap.ClassGCapG = 3.0;
    TestNearlyEqual(TEXT("Cap 3 G below lateral 4 G"), MidReturnAccelMps2(lowCap), 3.0 * NPCAI_G0, NPCAI_EPS);

    // A human's effective cap is at most HUMAN_MAX_G; an AI's is the structural cap
    FClassParams strongHuman = GetClass(0);
    strongHuman.PilotKind = ESOLPilotKind::Human;
    strongHuman.LateralG = 10.0;
    strongHuman.ClassGCapG = 12.0;
    TestNearlyEqual(TEXT("Human, lateral 10 G, cap 12 G: HUMAN_MAX_G"), MidReturnAccelMps2(strongHuman),
        SOLNewtonian::HUMAN_MAX_G * NPCAI_G0, NPCAI_EPS);
    FClassParams strongAi = strongHuman;
    strongAi.PilotKind = ESOLPilotKind::AI;
    TestNearlyEqual(TEXT("AI, lateral 10 G, cap 12 G: lateral"), MidReturnAccelMps2(strongAi), 10.0 * NPCAI_G0,
        NPCAI_EPS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathMidReturnGuidanceParamsTest, "SOLTest.NpcAiMath.MidReturnGuidanceParams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Mid guidance budgets are the margin times the return acceleration, no flip, the class cruise cap, Mid arrival
bool FSOLNpcAiMathMidReturnGuidanceParamsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FSOLGuidanceParams defaults;
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        const FClassParams& params = GetClass(classIndex);
        const FSOLGuidanceParams guidance = MidReturnGuidanceParams(params);
        const FString name = FString::Printf(TEXT("Class %d"), classIndex);
        const double expectedPlan = SOLNewtonian::GUIDANCE_ACCEL_MARGIN * MidReturnAccelMps2(params);
        TestNearlyEqual(*(name + TEXT(": plan")), guidance.PlanAccelMps2, expectedPlan, NPCAI_EPS);
        TestNearlyEqual(*(name + TEXT(": brake = plan")), guidance.BrakeAccelMps2, expectedPlan, NPCAI_EPS);
        TestEqual(*(name + TEXT(": no flip")), guidance.FlipTimeS, 0.0);
        TestEqual(*(name + TEXT(": cruise cap = class max speed")), guidance.MaxCruiseSpeedMps, params.MaxSpeedMps);
        TestEqual(*(name + TEXT(": arrival radius")), guidance.ArrivalRadiusM, MID_ARRIVAL_RADIUS_M);
        TestEqual(*(name + TEXT(": arrival speed")), guidance.ArrivalSpeedMps, MID_ARRIVAL_SPEED_MPS);
        TestEqual(*(name + TEXT(": default velocity gain")), guidance.VelocityGainPerS, defaults.VelocityGainPerS);
    }
    TestNearlyEqual(TEXT("Fighter plan = 0.9 * 4 G"), MidReturnGuidanceParams(GetClass(0)).PlanAccelMps2,
        0.9 * 4.0 * NPCAI_G0, NPCAI_EPS);

    // A capped synthetic class follows its cap
    FClassParams lowCap = GetClass(0);
    lowCap.ClassGCapG = 3.0;
    TestNearlyEqual(TEXT("Cap 3 G: plan 0.9 * 3 G"), MidReturnGuidanceParams(lowCap).PlanAccelMps2,
        0.9 * 3.0 * NPCAI_G0, NPCAI_EPS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepMidReturnInvalidTest, "SOLTest.NpcAiMath.StepMidReturnInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite inputs and a zero state come to rest at zero; a bad dt leaves the state unchanged and not at rest
bool FSOLNpcAiMathStepMidReturnInvalidTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FSOLGuidanceParams params = MidReturnGuidanceParams(GetClass(0));
    const double dt = 1.0 / 60.0;

    // Returns true when the step is the zero rest state
    const auto isZeroRest = [](const FMidReturnStep& step)
    {
        return step.bAtRest && step.DeviationM == FVector3d::ZeroVector
            && step.DeviationVelocityMps == FVector3d::ZeroVector;
    };
    const FVector3d bads[] = { NpcAiNaNVector(), FVector3d(NpcAiInf(), 0.0, 0.0), FVector3d(0.0, -NpcAiInf(), 0.0),
        FVector3d(1.0, 2.0, NpcAiNaN()) };
    for (const FVector3d& bad : bads)
    {
        TestTrue(TEXT("Non-finite deviation: zero, at rest"),
            isZeroRest(StepMidReturn(bad, FVector3d(1.0, 2.0, 3.0), params, dt)));
        TestTrue(TEXT("Non-finite velocity: zero, at rest"),
            isZeroRest(StepMidReturn(FVector3d(100.0, 0.0, 0.0), bad, params, dt)));
    }

    // A ship exactly on its rail at rest (listed before the dt check, so it holds for a bad dt too)
    TestTrue(TEXT("Both zero: zero, at rest"),
        isZeroRest(StepMidReturn(FVector3d::ZeroVector, FVector3d::ZeroVector, params, dt)));
    TestTrue(TEXT("Both zero with dt NaN: zero, at rest"),
        isZeroRest(StepMidReturn(FVector3d::ZeroVector, FVector3d::ZeroVector, params, NpcAiNaN())));

    // Bad dt: unchanged, not at rest
    const FVector3d deviation(100.0, -20.0, 5.0);
    const FVector3d velocity(3.0, 4.0, 0.0);
    for (const double badDt : { 0.0, -1.0, NpcAiNaN(), NpcAiInf(), -NpcAiInf() })
    {
        const FMidReturnStep step = StepMidReturn(deviation, velocity, params, badDt);
        TestTrue(*FString::Printf(TEXT("dt %f: unchanged"), badDt),
            step.DeviationM == deviation && step.DeviationVelocityMps == velocity);
        TestFalse(*FString::Printf(TEXT("dt %f: not at rest"), badDt), step.bAtRest);
    }

    // Arrived (inside the arrival radius and speed, both inclusive): snapped to zero, at rest
    TestTrue(TEXT("Inside the arrival ball: at rest"),
        isZeroRest(StepMidReturn(FVector3d(0.5, 0.0, 0.0), FVector3d(0.0, 0.1, 0.0), params, dt)));
    TestTrue(TEXT("On the arrival radius and speed: at rest"),
        isZeroRest(StepMidReturn(FVector3d(MID_ARRIVAL_RADIUS_M, 0.0, 0.0),
            FVector3d(0.0, MID_ARRIVAL_SPEED_MPS, 0.0), params, dt)));
    TestFalse(TEXT("Just outside the arrival radius: not at rest"),
        StepMidReturn(FVector3d(1.5, 0.0, 0.0), FVector3d::ZeroVector, params, dt).bAtRest);
    TestFalse(TEXT("Inside the radius but too fast: not at rest"),
        StepMidReturn(FVector3d(0.5, 0.0, 0.0), FVector3d(2.0, 0.0, 0.0), params, dt).bAtRest);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepMidReturnFormulaTest, "SOLTest.NpcAiMath.StepMidReturnFormula",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A step is the rendezvous guidance toward the rail (relPos = -deviation), then semi-implicit Euler
bool FSOLNpcAiMathStepMidReturnFormulaTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    FRandomStream stream(NPCAI_SEED + 10);
    bool bMatches = true;
    bool bNotAtRest = true;
    for (int32 sample = 0; sample < 500; ++sample)
    {
        const FSOLGuidanceParams params = MidReturnGuidanceParams(GetClass(sample % ClassCount()));
        const FVector3d deviation = NpcAiRandomVector(stream, 5.0, 5000.0);
        const FVector3d velocity = NpcAiRandomVector(stream, 0.0, 300.0);
        const double dt = 0.001 + 0.2 * stream.FRand();
        const FSOLGuidanceOutput guidance = SOLNewtonian::ComputeRendezvousGuidance(-deviation, velocity,
            FVector3d::ZeroVector, FVector3d::ZeroVector, params, dt);
        const FMidReturnStep step = StepMidReturn(deviation, velocity, params, dt);
        if (guidance.Phase == ESOLGuidancePhase::Arrived)
        {
            continue;
        }

        // Same operations as the contract, so only rounding-level differences are allowed
        const FVector3d expectedVelocity = velocity + guidance.DesiredNetAccelMps2 * dt;
        const FVector3d expectedDeviation = deviation + expectedVelocity * dt;
        bNotAtRest = bNotAtRest && !step.bAtRest;
        bMatches = bMatches && NpcAiVecNear(step.DeviationVelocityMps, expectedVelocity, 1.0e-12 * 1000.0)
            && NpcAiVecNear(step.DeviationM, expectedDeviation, 1.0e-12 * 10000.0);
    }
    TestTrue(TEXT("v' = v + net * dt and x' = x + v' * dt"), bMatches);
    TestTrue(TEXT("Not arrived: not at rest"), bNotAtRest);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepMidReturnBoundsTest, "SOLTest.NpcAiMath.StepMidReturnBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Over many random states: |v' - v| <= Plan * dt and |v'| <= max(|v|, cruise cap) + Plan * dt; rest means zero
bool FSOLNpcAiMathStepMidReturnBoundsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    FRandomStream stream(NPCAI_SEED + 11);
    bool bFinite = true;
    bool bAccelBound = true;
    bool bSpeedBound = true;
    bool bRestIsZero = true;
    int32 movingSteps = 0;
    for (int32 sample = 0; sample < 4000; ++sample)
    {
        const FClassParams& classParams = GetClass(sample % ClassCount());
        const FSOLGuidanceParams params = MidReturnGuidanceParams(classParams);
        const FVector3d deviation = (sample % 7 == 0) ? NpcAiRandomVector(stream, 0.0, 3.0)
            : NpcAiRandomVector(stream, 0.0, 5000.0);
        const FVector3d velocity = NpcAiRandomVector(stream, 0.0, 1.5 * classParams.MaxSpeedMps);
        const double dt = 0.001 + 0.499 * stream.FRand();
        const FMidReturnStep step = StepMidReturn(deviation, velocity, params, dt);
        bFinite = bFinite && SOLTestHelpers::IsFiniteVector(step.DeviationM)
            && SOLTestHelpers::IsFiniteVector(step.DeviationVelocityMps);
        if (step.bAtRest)
        {
            bRestIsZero = bRestIsZero && step.DeviationM == FVector3d::ZeroVector
                && step.DeviationVelocityMps == FVector3d::ZeroVector;
            continue;
        }

        // The bounds hold to rounding (relative 1e-9 plus a tiny absolute slack)
        ++movingSteps;
        const double budget = params.PlanAccelMps2 * dt;
        bAccelBound = bAccelBound
            && (step.DeviationVelocityMps - velocity).Size() <= budget * (1.0 + 1.0e-9) + 1.0e-9;
        bSpeedBound = bSpeedBound && step.DeviationVelocityMps.Size()
            <= FMath::Max(velocity.Size(), params.MaxCruiseSpeedMps) + budget * (1.0 + 1.0e-9) + 1.0e-9;
    }
    TestTrue(TEXT("Always finite"), bFinite);
    TestTrue(TEXT("The samples exercise moving steps"), movingSteps > 1000);
    TestTrue(TEXT("|v' - v| <= Plan * dt"), bAccelBound);
    TestTrue(TEXT("|v'| <= max(|v|, cruise cap) + Plan * dt"), bSpeedBound);
    TestTrue(TEXT("At rest means zero deviation and velocity"), bRestIsZero);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepMidReturnConvergenceTest,
    "SOLTest.NpcAiMath.StepMidReturnConvergence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From 3 km at rest each class returns to rest on its rail within 1.5x the ideal time + 2 s, never crossing far past
bool FSOLNpcAiMathStepMidReturnConvergenceTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const double startM = 3000.0;
    const FVector3d directions[] = { FVector3d(1.0, 0.0, 0.0), FVector3d(1.0, -2.0, 2.0).GetSafeNormal() };
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        const FSOLGuidanceParams params = MidReturnGuidanceParams(GetClass(classIndex));

        // The ideal is the bang-bang time 2 sqrt(d / Plan), lengthened only if the class cruise cap is reached
        const double idealS = NpcAiIdealRestToRestS(startM, params.PlanAccelMps2, params.MaxCruiseSpeedMps);
        const double boundS = 1.5 * (idealS + 2.0);
        for (const double dt : { 1.0 / 60.0, 1.0 / 10.0 })
        {
            for (const FVector3d& direction : directions)
            {
                const FNpcAiMidRun run = NpcAiRunMidReturn(direction * startM, FVector3d::ZeroVector, params, dt,
                    boundS);
                const FString name = FString::Printf(TEXT("Class %d dt %.4f dir (%.2f, %.2f, %.2f)"), classIndex, dt,
                    direction.X, direction.Y, direction.Z);
                AddInfo(FString::Printf(TEXT("%s: rest after %.2f s (bound %.2f s), min along %.3f m"), *name,
                    run.TimeS, boundS, run.MinAlongM));
                TestTrue(*(name + TEXT(": finite")), run.bFinite);
                TestTrue(*(name + TEXT(": at rest within the bound")), run.bAtRest);
                TestTrue(*(name + TEXT(": never past the rail by more than the arrival radius")),
                    run.MinAlongM >= -MID_ARRIVAL_RADIUS_M - NPCAI_EPS);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepMidReturnRecedingTest, "SOLTest.NpcAiMath.StepMidReturnReceding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship receding from its rail at 100 m/s from 500 m turns around and still returns to rest
bool FSOLNpcAiMathStepMidReturnRecedingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        const FSOLGuidanceParams params = MidReturnGuidanceParams(GetClass(classIndex));
        const double plan = params.PlanAccelMps2;

        // Ideal: stop the recession (100 / Plan), then rest-to-rest over the extended distance
        const double stopS = 100.0 / plan;
        const double extendedM = 500.0 + 100.0 * 100.0 / (2.0 * plan);
        const double boundS = 1.5 * (stopS + NpcAiIdealRestToRestS(extendedM, plan, params.MaxCruiseSpeedMps) + 2.0);
        const FNpcAiMidRun run = NpcAiRunMidReturn(FVector3d(500.0, 0.0, 0.0), FVector3d(100.0, 0.0, 0.0), params,
            1.0 / 60.0, boundS);
        AddInfo(FString::Printf(TEXT("Class %d: rest after %.2f s (bound %.2f s)"), classIndex, run.TimeS, boundS));
        TestTrue(*FString::Printf(TEXT("Class %d: finite"), classIndex), run.bFinite);
        TestTrue(*FString::Printf(TEXT("Class %d: at rest within the bound"), classIndex), run.bAtRest);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepMidReturnLongStepTest, "SOLTest.NpcAiMath.StepMidReturnLongStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Long steps (up to 0.5 s, a slow Mid update) still bring every class to rest without diverging
bool FSOLNpcAiMathStepMidReturnLongStepTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        const FSOLGuidanceParams params = MidReturnGuidanceParams(GetClass(classIndex));
        const double idealS = NpcAiIdealRestToRestS(3000.0, params.PlanAccelMps2, params.MaxCruiseSpeedMps);

        // No tolerance is given for long steps; 3x the ideal plus 2 s per side only rules out a stall or divergence
        const double boundS = 3.0 * (idealS + 2.0);
        for (const double dt : { 0.25, 0.5 })
        {
            const FNpcAiMidRun run = NpcAiRunMidReturn(FVector3d(0.0, 3000.0, 0.0), FVector3d::ZeroVector, params,
                dt, boundS);
            AddInfo(FString::Printf(TEXT("Class %d dt %.2f: rest after %.2f s, min along %.3f m"), classIndex, dt,
                run.TimeS, run.MinAlongM));
            TestTrue(*FString::Printf(TEXT("Class %d dt %.2f: finite"), classIndex, dt), run.bFinite);
            TestTrue(*FString::Printf(TEXT("Class %d dt %.2f: at rest within the bound"), classIndex, dt),
                run.bAtRest);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Issue #18 Part 18c: hull heading and hand-off
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathHullForwardTest, "SOLTest.NpcAiMath.HullForward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hull points along a velocity faster than 1 m/s, else perpendicular to the up hint, else +X
bool FSOLNpcAiMathHullForwardTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d up(0.0, 0.0, 1.0);

    // Velocity branch
    TestTrue(TEXT("Fast velocity: its direction"),
        NpcAiVecNear(HullForward(FVector3d(30.0, 40.0, 0.0), up), FVector3d(0.6, 0.8, 0.0), NPCAI_EPS));
    TestTrue(TEXT("Fast velocity, invalid up: its direction"),
        NpcAiVecNear(HullForward(FVector3d(0.0, 0.0, -50.0), NpcAiNaNVector()), FVector3d(0.0, 0.0, -1.0),
            NPCAI_EPS));
    TestTrue(TEXT("Just above 1 m/s: its direction"),
        NpcAiVecNear(HullForward(FVector3d(1.0 + 1.0e-6, 0.0, 0.0), up), FVector3d(1.0, 0.0, 0.0), NPCAI_EPS));

    // Exactly 1 m/s is not strictly greater: the up fallback. u = +Z, |u.X| < 0.9: Cross(+Z, +X) = +Y
    TestTrue(TEXT("Exactly 1 m/s: up fallback"),
        NpcAiVecNear(HullForward(FVector3d(1.0, 0.0, 0.0), up), FVector3d(0.0, 1.0, 0.0), NPCAI_EPS));
    TestTrue(TEXT("Slow velocity: up fallback"),
        NpcAiVecNear(HullForward(FVector3d(0.3, 0.2, 0.0), up), FVector3d(0.0, 1.0, 0.0), NPCAI_EPS));
    TestTrue(TEXT("Zero velocity, unnormalized up: up fallback"),
        NpcAiVecNear(HullForward(FVector3d::ZeroVector, FVector3d(0.0, 0.0, 7.0)), FVector3d(0.0, 1.0, 0.0),
            NPCAI_EPS));
    TestTrue(TEXT("NaN velocity: up fallback"),
        NpcAiVecNear(HullForward(NpcAiNaNVector(), up), FVector3d(0.0, 1.0, 0.0), NPCAI_EPS));
    TestTrue(TEXT("Infinite velocity: up fallback"),
        NpcAiVecNear(HullForward(FVector3d(NpcAiInf(), 0.0, 0.0), up), FVector3d(0.0, 1.0, 0.0), NPCAI_EPS));

    // |u.X| >= 0.9 crosses with +Y instead: Cross(+X, +Y) = +Z, Cross(-X, +Y) = -Z
    TestTrue(TEXT("Up along +X: Cross(+X, +Y) = +Z"),
        NpcAiVecNear(HullForward(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0)), FVector3d(0.0, 0.0, 1.0),
            NPCAI_EPS));
    TestTrue(TEXT("Up along -X: Cross(-X, +Y) = -Z"),
        NpcAiVecNear(HullForward(FVector3d::ZeroVector, FVector3d(-3.0, 0.0, 0.0)), FVector3d(0.0, 0.0, -1.0),
            NPCAI_EPS));
    TestTrue(TEXT("Up along +Y: Cross(+Y, +X) = -Z"),
        NpcAiVecNear(HullForward(FVector3d::ZeroVector, FVector3d(0.0, 1.0, 0.0)), FVector3d(0.0, 0.0, -1.0),
            NPCAI_EPS));

    // Degenerate up: +X
    TestTrue(TEXT("Slow velocity, zero up: +X"),
        HullForward(FVector3d(0.5, 0.0, 0.0), FVector3d::ZeroVector) == FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("Slow velocity, up below STEER_EPSILON: +X"),
        HullForward(FVector3d::ZeroVector, FVector3d(0.0, 0.0, 1.0e-7)) == FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("Slow velocity, up just above STEER_EPSILON: up fallback"),
        NpcAiVecNear(HullForward(FVector3d::ZeroVector, FVector3d(0.0, 0.0, 2.0e-6)), FVector3d(0.0, 1.0, 0.0),
            NPCAI_EPS));
    TestTrue(TEXT("Slow velocity, NaN up: +X"),
        HullForward(FVector3d::ZeroVector, NpcAiNaNVector()) == FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("NaN velocity and NaN up: +X"),
        HullForward(NpcAiNaNVector(), NpcAiNaNVector()) == FVector3d(1.0, 0.0, 0.0));

    // The up fallback is always unit length and perpendicular to the up
    FRandomStream stream(NPCAI_SEED + 20);
    bool bUnit = true;
    bool bPerpendicular = true;
    for (int32 sample = 0; sample < 1000; ++sample)
    {
        const FVector3d hint = NpcAiRandomVector(stream, 1.0e-3, 1.0e3);
        const FVector3d forward = HullForward(NpcAiRandomVector(stream, 0.0, 0.99), hint);
        bUnit = bUnit && FMath::Abs(forward.Size() - 1.0) <= NPCAI_EPS;
        bPerpendicular = bPerpendicular
            && FMath::Abs(FVector3d::DotProduct(forward, hint.GetSafeNormal())) <= NPCAI_EPS;
    }
    TestTrue(TEXT("Fallback is unit length"), bUnit);
    TestTrue(TEXT("Fallback is perpendicular to the up"), bPerpendicular);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathHandoffOrientationTest, "SOLTest.NpcAiMath.HandoffOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hand-off orientation is normalized, noses along HullForward, and rolls its up toward the hint
bool FSOLNpcAiMathHandoffOrientationTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    FRandomStream stream(NPCAI_SEED + 21);
    bool bNormalized = true;
    bool bForward = true;
    bool bUpPerpendicular = true;
    bool bUpTowardHint = true;
    for (int32 sample = 0; sample < 1000; ++sample)
    {
        const FVector3d velocity = (sample % 3 == 0) ? NpcAiRandomVector(stream, 0.0, 0.99)
            : NpcAiRandomVector(stream, 2.0, 500.0);
        const FVector3d hint = NpcAiRandomVector(stream, 1.0e-2, 10.0);
        const FQuat4d orientation = HandoffOrientation(velocity, hint);
        const FVector3d forward = HullForward(velocity, hint);
        const FVector3d localUp = orientation.GetAxisZ();
        bNormalized = bNormalized && FMath::Abs(orientation.Size() - 1.0) <= NPCAI_EPS;
        bForward = bForward && NpcAiVecNear(orientation.GetAxisX(), forward, 1.0e-9);
        bUpPerpendicular = bUpPerpendicular && FMath::Abs(FVector3d::DotProduct(localUp, forward)) <= 1.0e-9;

        // Only a hint clearly off the forward axis defines an up direction
        const double hintAngle = NpcAiAngle(forward, hint);
        if (hintAngle > 1.0e-3 && hintAngle < UE_DOUBLE_PI - 1.0e-3)
        {
            bUpTowardHint = bUpTowardHint && FVector3d::DotProduct(localUp, hint) > 0.0;
        }
    }
    TestTrue(TEXT("Normalized"), bNormalized);
    TestTrue(TEXT("Forward equals HullForward"), bForward);
    TestTrue(TEXT("Local up perpendicular to the forward"), bUpPerpendicular);
    TestTrue(TEXT("Local up toward a non-parallel hint"), bUpTowardHint);

    // Flying along +X with +Z up is the identity frame
    const FQuat4d level = HandoffOrientation(FVector3d(100.0, 0.0, 0.0), FVector3d(0.0, 0.0, 1.0));
    TestTrue(TEXT("Level flight: nose +X"), NpcAiVecNear(level.GetAxisX(), FVector3d(1.0, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Level flight: up +Z"), NpcAiVecNear(level.GetAxisZ(), FVector3d(0.0, 0.0, 1.0), 1.0e-9));

    // Invalid hints still give a normalized orientation along HullForward
    const FQuat4d noHint = HandoffOrientation(FVector3d(0.0, 50.0, 0.0), NpcAiNaNVector());
    TestTrue(TEXT("NaN hint: normalized"), FMath::Abs(noHint.Size() - 1.0) <= NPCAI_EPS);
    TestTrue(TEXT("NaN hint: nose along the velocity"),
        NpcAiVecNear(noHint.GetAxisX(), FVector3d(0.0, 1.0, 0.0), 1.0e-9));
    const FQuat4d nothing = HandoffOrientation(NpcAiNaNVector(), NpcAiNaNVector());
    TestTrue(TEXT("Nothing valid: normalized"), FMath::Abs(nothing.Size() - 1.0) <= NPCAI_EPS);
    TestTrue(TEXT("Nothing valid: nose +X"), NpcAiVecNear(nothing.GetAxisX(), FVector3d(1.0, 0.0, 0.0), 1.0e-9));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Issue #18 Part 18c: Full tier (Newtonian ship)
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathFullShipDesiredForwardTest, "SOLTest.NpcAiMath.FullShipDesiredForward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The nose goes on the aim near the fire range, else along the desired velocity, the velocity, the current nose, +X
bool FSOLNpcAiMathFullShipDesiredForwardTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d aim(0.0, 5.0, 0.0);
    const FVector3d desired(0.0, 0.0, 10.0);
    const FVector3d velocity(-20.0, 0.0, 0.0);
    const FVector3d current(0.0, 3.0, 4.0);
    const FVector3d aimDir(0.0, 1.0, 0.0);
    const FVector3d desiredDir(0.0, 0.0, 1.0);
    const FVector3d velocityDir(-1.0, 0.0, 0.0);
    const FVector3d currentDir(0.0, 0.6, 0.8);

    // Guns on target: aim wins inside 1.5x the fire range, the boundary inclusive
    TestTrue(TEXT("Inside the fire range: aim"),
        NpcAiVecNear(FullShipDesiredForward(aim, 100.0, 200.0, desired, velocity, current), aimDir, NPCAI_EPS));
    TestTrue(TEXT("At exactly 1.5x the fire range: aim"),
        NpcAiVecNear(FullShipDesiredForward(aim, 300.0, 200.0, desired, velocity, current), aimDir, NPCAI_EPS));
    TestTrue(TEXT("Beyond 1.5x the fire range: desired velocity"),
        NpcAiVecNear(FullShipDesiredForward(aim, 300.001, 200.0, desired, velocity, current), desiredDir,
            NPCAI_EPS));

    // Invalid aim or distances fall through to the desired velocity
    const FVector3d badAims[] = { FVector3d::ZeroVector, FVector3d(1.0e-7, 0.0, 0.0), NpcAiNaNVector(),
        FVector3d(NpcAiInf(), 0.0, 0.0) };
    for (const FVector3d& badAim : badAims)
    {
        TestTrue(TEXT("Invalid aim falls through"),
            NpcAiVecNear(FullShipDesiredForward(badAim, 100.0, 200.0, desired, velocity, current), desiredDir,
                NPCAI_EPS));
    }
    for (const double bad : { 0.0, -10.0, NpcAiNaN(), NpcAiInf() })
    {
        TestTrue(*FString::Printf(TEXT("Distance %f falls through"), bad),
            NpcAiVecNear(FullShipDesiredForward(aim, bad, 200.0, desired, velocity, current), desiredDir,
                NPCAI_EPS));
        TestTrue(*FString::Printf(TEXT("Fire range %f falls through"), bad),
            NpcAiVecNear(FullShipDesiredForward(aim, 100.0, bad, desired, velocity, current), desiredDir,
                NPCAI_EPS));
    }

    // Desired velocity strictly above 1 m/s, else the velocity
    TestTrue(TEXT("Desired at exactly 1 m/s falls to the velocity"),
        NpcAiVecNear(FullShipDesiredForward(FVector3d::ZeroVector, 0.0, 0.0, FVector3d(0.0, 0.0, 1.0), velocity,
            current), velocityDir, NPCAI_EPS));
    TestTrue(TEXT("NaN desired falls to the velocity"),
        NpcAiVecNear(FullShipDesiredForward(FVector3d::ZeroVector, 0.0, 0.0, NpcAiNaNVector(), velocity, current),
            velocityDir, NPCAI_EPS));
    TestTrue(TEXT("Desired just above 1 m/s: desired"),
        NpcAiVecNear(FullShipDesiredForward(FVector3d::ZeroVector, 0.0, 0.0, FVector3d(0.0, 0.0, 1.0 + 1.0e-6),
            velocity, current), desiredDir, NPCAI_EPS));

    // Velocity strictly above 1 m/s, else the current forward
    TestTrue(TEXT("Velocity at exactly 1 m/s falls to the current forward"),
        NpcAiVecNear(FullShipDesiredForward(FVector3d::ZeroVector, 0.0, 0.0, FVector3d::ZeroVector,
            FVector3d(-1.0, 0.0, 0.0), current), currentDir, NPCAI_EPS));
    TestTrue(TEXT("Infinite velocity falls to the current forward"),
        NpcAiVecNear(FullShipDesiredForward(FVector3d::ZeroVector, 0.0, 0.0, FVector3d::ZeroVector,
            FVector3d(0.0, NpcAiInf(), 0.0), current), currentDir, NPCAI_EPS));

    // Current forward, else +X
    TestTrue(TEXT("Zero current forward: +X"),
        FullShipDesiredForward(FVector3d::ZeroVector, 0.0, 0.0, FVector3d::ZeroVector, FVector3d::ZeroVector,
            FVector3d::ZeroVector) == FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("NaN current forward: +X"),
        FullShipDesiredForward(NpcAiNaNVector(), NpcAiNaN(), NpcAiNaN(), NpcAiNaNVector(), NpcAiNaNVector(),
            NpcAiNaNVector()) == FVector3d(1.0, 0.0, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepFullShipInvalidTest, "SOLTest.NpcAiMath.StepFullShipInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A non-finite or non-positive dt leaves the state unchanged
bool FSOLNpcAiMathStepFullShipInvalidTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FSOLThrustEnvelope env = ClassEnvelope(GetClass(0));
    FSOLShipState state;
    state.PositionM = FVector3d(1.0e5, -2.0e4, 300.0);
    state.VelocityMps = FVector3d(50.0, 10.0, -5.0);
    state.Orientation = FQuat4d(FVector3d(0.0, 0.0, 1.0), 0.7);
    state.AngularVelocityRadPerS = FVector3d(0.1, -0.2, 0.3);
    for (const double dt : { 0.0, -0.1, NpcAiNaN(), NpcAiInf(), -NpcAiInf() })
    {
        const FSOLShipState next = StepFullShip(state, FVector3d(200.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0),
            FVector3d(0.0, 0.0, 1.0), env, dt);
        TestTrue(*FString::Printf(TEXT("dt %f: unchanged"), dt), next.PositionM == state.PositionM
            && next.VelocityMps == state.VelocityMps && next.Orientation == state.Orientation
            && next.AngularVelocityRadPerS == state.AngularVelocityRadPerS);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepFullShipBoundsTest, "SOLTest.NpcAiMath.StepFullShipBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Over random states and every class: thrust within the total cap and the per-axis box (start-of-step frame), along
// the velocity error, spin within the clamp, and x' = x + v' * dt
bool FSOLNpcAiMathStepFullShipBoundsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    TArray<FSOLThrustEnvelope> envelopes;
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        envelopes.Add(ClassEnvelope(GetClass(classIndex)));
    }

    // Rounding of (v' - v) / dt: |v| <= 600 m/s and dt >= 1 ms leave ~1e-10 m/s^2, so 1e-6 m/s^2 is ample
    constexpr double accelSlack = 1.0e-6;
    constexpr double relSlack = 1.0 + 1.0e-9;
    FRandomStream stream(NPCAI_SEED + 30);
    bool bFinite = true;
    bool bTotal = true;
    bool bBox = true;
    bool bDirection = true;
    bool bHoldRate = true;
    bool bSpin = true;
    bool bPosition = true;
    bool bNormalized = true;
    for (int32 sample = 0; sample < 3000; ++sample)
    {
        const FSOLThrustEnvelope& env = envelopes[sample % envelopes.Num()];
        const double spin = env.MaxSpinRateRadPerS;
        FSOLShipState state;
        state.PositionM = NpcAiRandomVector(stream, 0.0, 1.0e7);
        state.VelocityMps = NpcAiRandomVector(stream, 0.0, 600.0);
        state.Orientation = NpcAiRandomOrientation(stream);
        state.AngularVelocityRadPerS = FVector3d((2.0 * stream.FRand() - 1.0) * spin,
            (2.0 * stream.FRand() - 1.0) * spin, (2.0 * stream.FRand() - 1.0) * spin);
        const FVector3d desiredVelocity = (sample % 5 == 0)
            ? state.VelocityMps + NpcAiRandomVector(stream, 0.0, 5.0) : NpcAiRandomVector(stream, 0.0, 600.0);
        const FVector3d desiredForward = (sample % 11 == 0) ? FVector3d::ZeroVector
            : NpcAiRandomVector(stream, 0.1, 10.0);
        const FVector3d desiredUp = (sample % 4 == 0) ? FVector3d::ZeroVector : NpcAiRandomVector(stream, 0.1, 10.0);
        const double dt = 0.001 + 0.199 * stream.FRand();
        const FSOLShipState next = StepFullShip(state, desiredVelocity, desiredForward, desiredUp, env, dt);
        bFinite = bFinite && SOLTestHelpers::IsFiniteVector(next.PositionM)
            && SOLTestHelpers::IsFiniteVector(next.VelocityMps)
            && SOLTestHelpers::IsFiniteVector(next.AngularVelocityRadPerS);

        // Achieved acceleration in the world and in the start-of-step local frame
        const FVector3d accel = (next.VelocityMps - state.VelocityMps) / dt;
        const FVector3d local = state.Orientation.UnrotateVector(accel);
        bTotal = bTotal && accel.Size() <= env.MaxTotalAccelMps2 * relSlack + accelSlack;
        bBox = bBox && local.X <= env.ForwardAccelMps2 * relSlack + accelSlack
            && -local.X <= env.ReverseAccelMps2 * relSlack + accelSlack
            && FMath::Abs(local.Y) <= env.LateralAccelMps2 * relSlack + accelSlack
            && FMath::Abs(local.Z) <= env.VerticalAccelMps2 * relSlack + accelSlack;

        // The allocation only scales the hold request (desired - v) / max(tau, dt) down, keeping its direction
        const FVector3d velocityError = desiredVelocity - state.VelocityMps;
        bHoldRate = bHoldRate && accel.Size()
            <= velocityError.Size() / FMath::Max(FULL_HOLD_TIME_CONSTANT_S, dt) * relSlack + accelSlack;
        if (accel.Size() > 1.0e-3)
        {
            bDirection = bDirection && NpcAiAngle(accel, velocityError) <= 1.0e-6;
        }

        // Spin clamp, position update and orientation normalization
        bSpin = bSpin && FMath::Abs(next.AngularVelocityRadPerS.X) <= spin * relSlack
            && FMath::Abs(next.AngularVelocityRadPerS.Y) <= spin * relSlack
            && FMath::Abs(next.AngularVelocityRadPerS.Z) <= spin * relSlack;
        bPosition = bPosition && NpcAiVecNear(next.PositionM, state.PositionM + next.VelocityMps * dt,
            1.0e-12 * FMath::Max(1.0, state.PositionM.Size()) + 1.0e-9);
        bNormalized = bNormalized && FMath::Abs(next.Orientation.Size() - 1.0) <= NPCAI_EPS;
    }
    TestTrue(TEXT("Always finite"), bFinite);
    TestTrue(TEXT("|a| <= MaxTotalAccelMps2"), bTotal);
    TestTrue(TEXT("Local acceleration inside the per-axis box"), bBox);
    TestTrue(TEXT("|a| <= |desired - v| / max(tau, dt)"), bHoldRate);
    TestTrue(TEXT("Acceleration along the velocity error"), bDirection);
    TestTrue(TEXT("Spin within the clamp"), bSpin);
    TestTrue(TEXT("x' = x + v' * dt"), bPosition);
    TestTrue(TEXT("Orientation normalized"), bNormalized);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepFullShipSpeedTest, "SOLTest.NpcAiMath.StepFullShipSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From rest, a desired 200 m/s along the nose is reached in a bounded time and the nose stays put
bool FSOLNpcAiMathStepFullShipSpeedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d desiredVelocity(200.0, 0.0, 0.0);
    const double dt = 1.0 / 60.0;
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        const FSOLThrustEnvelope env = ClassEnvelope(GetClass(classIndex));

        // Saturated burn to within accel * tau, then the 0.5 s exponential to 0.05 m/s (a few seconds): +6 s is ample
        const double accel = FMath::Min(env.ForwardAccelMps2, env.MaxTotalAccelMps2);
        const double boundS = 200.0 / accel + 6.0;
        FSOLShipState state;
        double reachedS = -1.0;
        const int32 steps = FMath::CeilToInt32(boundS / dt);
        for (int32 step = 1; step <= steps && reachedS < 0.0; ++step)
        {
            state = StepFullShip(state, desiredVelocity, FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 0.0, 1.0), env, dt);
            if ((state.VelocityMps - desiredVelocity).Size() <= 0.05)
            {
                reachedS = step * dt;
            }
        }
        AddInfo(FString::Printf(TEXT("Class %d: 200 m/s within 0.05 after %.2f s (bound %.2f s)"), classIndex,
            reachedS, boundS));
        TestTrue(*FString::Printf(TEXT("Class %d: reaches 200 m/s"), classIndex), reachedS > 0.0);
        TestTrue(*FString::Printf(TEXT("Class %d: nose still +X"), classIndex),
            NpcAiAngle(state.Orientation.GetAxisX(), FVector3d(1.0, 0.0, 0.0)) <= 1.0e-6);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepFullShipAlignTest, "SOLTest.NpcAiMath.StepFullShipAlign",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A constant desired forward 90 degrees off the nose is reached within 1 degree in a bounded time and held
bool FSOLNpcAiMathStepFullShipAlignTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d desiredForward(0.0, 1.0, 0.0);
    const double dt = 1.0 / 60.0;
    const double oneDegree = FMath::DegreesToRadians(1.0);
    for (int32 classIndex = 0; classIndex < ClassCount(); ++classIndex)
    {
        const FSOLThrustEnvelope env = ClassEnvelope(GetClass(classIndex));

        // A pure 90 degree yaw: 3x the ideal rest-to-rest time (the controller brakes at 80% alpha and settles on a
        // 0.25 s exponential) plus 5 s; for the fighter about 9.3 s
        const double idealS = NpcAiIdealRotationS(UE_DOUBLE_HALF_PI, env.MaxAngularRateRadPerS.Z,
            env.MaxAngularAccelRadPerS2.Z);
        const double boundS = 3.0 * idealS + 5.0;
        FSOLShipState state;
        double alignedS = -1.0;
        const int32 steps = FMath::CeilToInt32(boundS / dt);
        for (int32 step = 1; step <= steps; ++step)
        {
            state = StepFullShip(state, FVector3d::ZeroVector, desiredForward, FVector3d(0.0, 0.0, 1.0), env, dt);
            if (alignedS < 0.0 && NpcAiAngle(state.Orientation.GetAxisX(), desiredForward) <= oneDegree)
            {
                alignedS = step * dt;
            }
        }
        const double finalAngle = NpcAiAngle(state.Orientation.GetAxisX(), desiredForward);
        AddInfo(FString::Printf(TEXT("Class %d: aligned after %.2f s (bound %.2f s), final error %.4f deg"),
            classIndex, alignedS, boundS, FMath::RadiansToDegrees(finalAngle)));
        TestTrue(*FString::Printf(TEXT("Class %d: aligns within 1 degree"), classIndex), alignedS > 0.0);
        TestTrue(*FString::Printf(TEXT("Class %d: still aligned at the end"), classIndex), finalAngle <= oneDegree);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathStepFullShipBallisticTest, "SOLTest.NpcAiMath.StepFullShipBallistic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the desired velocity equal to the velocity and the nose and up already on target, the ship coasts (no gravity)
bool FSOLNpcAiMathStepFullShipBallisticTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FSOLThrustEnvelope env = ClassEnvelope(GetClass(1));
    const double dt = 0.1;
    FRandomStream stream(NPCAI_SEED + 31);
    for (int32 trial = 0; trial < 2; ++trial)
    {
        FSOLShipState start;
        start.PositionM = FVector3d(1.0e6, -2.0e5, 3.0e4);
        start.VelocityMps = FVector3d(120.0, -35.0, 8.0);
        start.Orientation = (trial == 0) ? FQuat4d::Identity : NpcAiRandomOrientation(stream);
        const FVector3d forward = start.Orientation.GetAxisX();
        const FVector3d up = start.Orientation.GetAxisZ();

        // 600 steps (60 s): a 1 G pull would move the ship by ~17.6 km, so a 1e-3 m drift rules any gravity out
        FSOLShipState state = start;
        for (int32 step = 0; step < 600; ++step)
        {
            state = StepFullShip(state, state.VelocityMps, forward, up, env, dt);
        }
        const FString name = (trial == 0) ? TEXT("Identity") : TEXT("Random orientation");
        TestTrue(*(name + TEXT(": velocity unchanged")),
            NpcAiVecNear(state.VelocityMps, start.VelocityMps, 1.0e-9 * start.VelocityMps.Size()));
        TestTrue(*(name + TEXT(": straight-line position")),
            NpcAiVecNear(state.PositionM, start.PositionM + start.VelocityMps * (600.0 * dt), 1.0e-3));
        TestTrue(*(name + TEXT(": no rotation")),
            NpcAiAngle(state.Orientation.GetAxisX(), forward) <= 1.0e-6
                && NpcAiAngle(state.Orientation.GetAxisZ(), up) <= 1.0e-6);
        TestTrue(*(name + TEXT(": no spin")), state.AngularVelocityRadPerS.Size() <= 1.0e-6);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Issue #18 Part 18c: Mid-to-Far demotion gate
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathAllowFarDemotionTest, "SOLTest.NpcAiMath.AllowFarDemotion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A budget-forced demotion is always allowed; otherwise only when the rail snap is invisible (at most half a pixel)
bool FSOLNpcAiMathAllowFarDemotionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;

    // Pixel 2^-10 rad at 1024 m: half a pixel is exactly 0.5 m
    const double pixel = 1.0 / 1024.0;
    TestTrue(TEXT("Forced: allowed even with a huge offset"), AllowFarDemotion(1.0e9, 10.0, pixel, true));
    TestTrue(TEXT("Forced: allowed with NaN inputs"), AllowFarDemotion(NpcAiNaN(), NpcAiNaN(), NpcAiNaN(), true));
    TestTrue(TEXT("Zero offset: allowed"), AllowFarDemotion(0.0, 1024.0, pixel, false));
    TestTrue(TEXT("Exactly half a pixel: allowed"), AllowFarDemotion(0.5, 1024.0, pixel, false));
    TestFalse(TEXT("Just over half a pixel: not allowed"), AllowFarDemotion(0.5000001, 1024.0, pixel, false));
    TestFalse(TEXT("NaN offset: not allowed"), AllowFarDemotion(NpcAiNaN(), 1024.0, pixel, false));
    TestFalse(TEXT("Negative offset: not allowed"), AllowFarDemotion(-0.1, 1024.0, pixel, false));

    // Unforced, it equals IsHandoffSnapInvisible over a grid that includes the boundary and invalid inputs
    bool bMatches = true;
    for (const double offset : { 0.0, 0.25, 0.5, 0.5000001, 1.0, -1.0, NpcAiNaN(), NpcAiInf() })
    {
        for (const double distance : { 1024.0, 1.0e6, 0.0, -5.0, NpcAiNaN(), NpcAiInf() })
        {
            for (const double pixelAngle : { pixel, 1.0e-4, 0.0, -1.0, NpcAiNaN(), NpcAiInf() })
            {
                bMatches = bMatches && AllowFarDemotion(offset, distance, pixelAngle, false)
                    == SOLNewtonian::IsHandoffSnapInvisible(offset, distance, pixelAngle);
                bMatches = bMatches && AllowFarDemotion(offset, distance, pixelAngle, true);
            }
        }
    }
    TestTrue(TEXT("Unforced equals IsHandoffSnapInvisible; forced is always true"), bMatches);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Issue #18 Part 18c: gun gimbal
// ---------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcAiMathGunDirectionTest, "SOLTest.NpcAiMath.GunDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shot leaves along the aim inside the gimbal, else at the gimbal angle from the nose toward the aim; bad inputs
// give zero (aim) or the aim (nose); a bad gimbal counts as 0
bool FSOLNpcAiMathGunDirectionTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcAi;
    const FVector3d plusX(1.0, 0.0, 0.0);
    const FVector3d plusY(0.0, 1.0, 0.0);
    const FVector3d plusZ(0.0, 0.0, 1.0);
    const double tight = 1.0e-12;

    // Invalid aim: zero, whatever the nose and gimbal
    TestTrue(TEXT("Zero aim: zero"), GunDirection(plusX, FVector3d::ZeroVector, 1.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("Aim below EPS: zero"),
        GunDirection(plusX, FVector3d(1.0e-7, 0.0, 0.0), 1.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("NaN aim: zero"), GunDirection(plusX, NpcAiNaNVector(), 1.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("Infinite aim: zero"),
        GunDirection(plusX, FVector3d(NpcAiInf(), 0.0, 0.0), 1.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("NaN aim, NaN nose: zero"),
        GunDirection(NpcAiNaNVector(), NpcAiNaNVector(), 1.0) == FVector3d::ZeroVector);
    TestTrue(TEXT("Zero aim, zero nose: zero"),
        GunDirection(FVector3d::ZeroVector, FVector3d::ZeroVector, 1.0) == FVector3d::ZeroVector);

    // Invalid nose: the normalized aim, whatever the gimbal
    const FVector3d aim345(3.0, 4.0, 0.0);
    const FVector3d aim345Unit(0.6, 0.8, 0.0);
    TestTrue(TEXT("Zero nose: normalized aim"),
        NpcAiVecNear(GunDirection(FVector3d::ZeroVector, aim345, 0.1), aim345Unit, tight));
    TestTrue(TEXT("Nose below EPS: normalized aim"),
        NpcAiVecNear(GunDirection(FVector3d(1.0e-7, 0.0, 0.0), aim345, 0.1), aim345Unit, tight));
    TestTrue(TEXT("NaN nose: normalized aim"), NpcAiVecNear(GunDirection(NpcAiNaNVector(), aim345, 0.1), aim345Unit,
        tight));
    TestTrue(TEXT("Infinite nose: normalized aim"),
        NpcAiVecNear(GunDirection(FVector3d(NpcAiInf(), 0.0, 0.0), aim345, 0.1), aim345Unit, tight));
    TestTrue(TEXT("Zero nose, NaN gimbal: normalized aim"),
        NpcAiVecNear(GunDirection(FVector3d::ZeroVector, aim345, NpcAiNaN()), aim345Unit, tight));

    // Inside the gimbal: exactly the normalized aim (non-unit inputs too)
    TestTrue(TEXT("Aim on the nose, gimbal 0: aim"), GunDirection(plusX, plusX, 0.0) == plusX);
    TestTrue(TEXT("Unit aim inside the gimbal: exactly the aim"), GunDirection(plusX, plusY, 2.0) == plusY);
    TestTrue(TEXT("Non-unit aim inside the gimbal: normalized aim"),
        NpcAiVecNear(GunDirection(FVector3d(0.0, 0.0, 7.0), FVector3d(0.0, 5.0, 5.0), 1.0),
            FVector3d(0.0, 1.0, 1.0).GetSafeNormal(), tight));
    TestTrue(TEXT("Small offset inside a small gimbal: aim"),
        NpcAiVecNear(GunDirection(plusX, FVector3d(1.0, 0.01, 0.0), 0.02), FVector3d(1.0, 0.01, 0.0).GetSafeNormal(),
            tight));

    // The boundary theta == g is inside (and the rotated answer would equal the aim there anyway)
    TestTrue(TEXT("Boundary 90 degrees: aim"), NpcAiVecNear(GunDirection(plusX, plusY, UE_DOUBLE_HALF_PI), plusY,
        tight));
    TestTrue(TEXT("Boundary 45 degrees: aim"),
        NpcAiVecNear(GunDirection(plusX, FVector3d(2.0, 2.0, 0.0), UE_DOUBLE_PI / 4.0),
            FVector3d(1.0, 1.0, 0.0).GetSafeNormal(), tight));

    // Outside the gimbal: at the gimbal angle from the nose toward the aim, in their common plane
    TestTrue(TEXT("+X nose, +Y aim, gimbal 0.3: (cos, sin, 0)"),
        NpcAiVecNear(GunDirection(plusX, plusY, 0.3), FVector3d(FMath::Cos(0.3), FMath::Sin(0.3), 0.0), NPCAI_EPS));
    TestTrue(TEXT("Non-unit +Z nose, (0, -3, 1) aim, gimbal 0.2"),
        NpcAiVecNear(GunDirection(FVector3d(0.0, 0.0, 4.0), FVector3d(0.0, -3.0, 1.0), 0.2),
            FVector3d(0.0, -FMath::Sin(0.2), FMath::Cos(0.2)), NPCAI_EPS));
    {
        // A general off-axis case checked against the geometric properties
        const FVector3d nose = FVector3d(1.0, 2.0, -0.5);
        const FVector3d aim = FVector3d(-2.0, 0.5, 3.0);
        const double gimbal = 0.4;
        const FVector3d result = GunDirection(nose, aim, gimbal);
        const FVector3d noseUnit = nose.GetSafeNormal();
        const FVector3d aimUnit = aim.GetSafeNormal();
        const FVector3d normal = FVector3d::CrossProduct(noseUnit, aimUnit).GetSafeNormal();
        const FVector3d toward = (aimUnit - noseUnit * FVector3d::DotProduct(noseUnit, aimUnit)).GetSafeNormal();
        TestTrue(TEXT("Off-axis: unit"), SOLTestHelpers::IsFiniteVector(result)
            && FMath::Abs(result.Size() - 1.0) <= NPCAI_EPS);
        TestTrue(TEXT("Off-axis: angle to nose == gimbal"), FMath::Abs(NpcAiAngle(result, noseUnit) - gimbal)
            <= NPCAI_EPS);
        TestTrue(TEXT("Off-axis: coplanar with nose and aim"),
            FMath::Abs(FVector3d::DotProduct(result, normal)) <= NPCAI_EPS);
        TestTrue(TEXT("Off-axis: on the aim's side"),
            FMath::Abs(FVector3d::DotProduct(result, toward) - FMath::Sin(gimbal)) <= NPCAI_EPS);
        TestTrue(TEXT("Off-axis: closer to the aim than the nose is"),
            NpcAiAngle(result, aimUnit) < NpcAiAngle(noseUnit, aimUnit));
    }

    // Antiparallel aim: the documented perpendicular normalize(Cross(n, |n.X| < 0.9 ? +X : +Y))
    TestTrue(TEXT("Antiparallel, +X nose (uses +Y, p = +Z)"),
        NpcAiVecNear(GunDirection(plusX, -plusX, 0.5), FVector3d(FMath::Cos(0.5), 0.0, FMath::Sin(0.5)), NPCAI_EPS));
    TestTrue(TEXT("Antiparallel, +Z nose (uses +X, p = +Y)"),
        NpcAiVecNear(GunDirection(plusZ, -plusZ, 0.5), FVector3d(0.0, FMath::Sin(0.5), FMath::Cos(0.5)), NPCAI_EPS));
    TestTrue(TEXT("Antiparallel, non-unit -Y nose (uses +X, p = Cross(-Y, +X) = +Z)"),
        NpcAiVecNear(GunDirection(FVector3d(0.0, -3.0, 0.0), FVector3d(0.0, 2.0, 0.0), 1.0),
            FVector3d(0.0, -FMath::Cos(1.0), FMath::Sin(1.0)), NPCAI_EPS));
    TestTrue(TEXT("Antiparallel, gimbal 0: the nose"), NpcAiVecNear(GunDirection(plusX, -plusX, 0.0), plusX,
        NPCAI_EPS));
    TestTrue(TEXT("Antiparallel, gimbal PI: the aim"), NpcAiVecNear(GunDirection(plusX, -plusX, UE_DOUBLE_PI), -plusX,
        tight));

    // A NaN or negative gimbal counts as 0: an off-nose aim gives the nose itself
    TestTrue(TEXT("NaN gimbal: the nose"), NpcAiVecNear(GunDirection(plusX, plusY, NpcAiNaN()), plusX, NPCAI_EPS));
    TestTrue(TEXT("Negative gimbal: the nose"), NpcAiVecNear(GunDirection(plusX, plusY, -0.5), plusX, NPCAI_EPS));
    TestTrue(TEXT("-inf gimbal: the nose"), NpcAiVecNear(GunDirection(plusX, plusY, -NpcAiInf()), plusX, NPCAI_EPS));
    TestTrue(TEXT("Gimbal 0: the nose (non-unit nose)"),
        NpcAiVecNear(GunDirection(FVector3d(0.0, 0.0, 9.0), FVector3d(1.0, 1.0, 1.0), 0.0), plusZ, NPCAI_EPS));
    TestTrue(TEXT("NaN gimbal, aim on the nose: the aim"),
        NpcAiVecNear(GunDirection(plusX, FVector3d(2.0, 0.0, 0.0), NpcAiNaN()), plusX, tight));

    // A gimbal of PI or more: always the aim
    TestTrue(TEXT("Gimbal PI: aim"), NpcAiVecNear(GunDirection(plusX, FVector3d(-1.0, 0.1, 0.0), UE_DOUBLE_PI),
        FVector3d(-1.0, 0.1, 0.0).GetSafeNormal(), tight));
    TestTrue(TEXT("Gimbal 10: aim"), NpcAiVecNear(GunDirection(plusX, FVector3d(-1.0, -0.2, 0.3), 10.0),
        FVector3d(-1.0, -0.2, 0.3).GetSafeNormal(), tight));
    TestTrue(TEXT("Gimbal +inf: aim"), NpcAiVecNear(GunDirection(plusX, FVector3d(-1.0, -0.2, 0.3), NpcAiInf()),
        FVector3d(-1.0, -0.2, 0.3).GetSafeNormal(), tight));

    // Randomized properties: unit, within the gimbal of the nose, the aim when inside, else on the gimbal cone
    FRandomStream stream(NPCAI_SEED + 18);
    bool bUnit = true;
    bool bWithinGimbal = true;
    bool bAimInside = true;
    bool bOnCone = true;
    bool bCoplanar = true;
    bool bAimSide = true;
    for (int32 sample = 0; sample < 4000; ++sample)
    {
        // Random nose, aim and gimbal (a fifth of the samples use a gimbal near the aim angle)
        const FVector3d nose = NpcAiRandomVector(stream, 0.01, 100.0);
        const FVector3d aim = NpcAiRandomVector(stream, 0.01, 100.0);
        const FVector3d noseUnit = nose.GetSafeNormal();
        const FVector3d aimUnit = aim.GetSafeNormal();
        const double theta = NpcAiAngle(noseUnit, aimUnit);
        const double gimbal = (sample % 5 == 0)
            ? FMath::Clamp(theta + 0.02 * (stream.FRand() - 0.5), 0.0, UE_DOUBLE_PI)
            : UE_DOUBLE_PI * stream.FRand();
        const FVector3d result = GunDirection(nose, aim, gimbal);
        bUnit = bUnit && SOLTestHelpers::IsFiniteVector(result) && FMath::Abs(result.Size() - 1.0) <= NPCAI_EPS;
        const double toNose = NpcAiAngle(result, noseUnit);
        bWithinGimbal = bWithinGimbal && toNose <= gimbal + NPCAI_EPS;
        if (theta <= gimbal)
        {
            bAimInside = bAimInside && NpcAiVecNear(result, aimUnit, NPCAI_EPS);
            continue;
        }

        // Outside: on the cone, in the nose-aim plane, toward the aim (skip the plane checks when nearly parallel)
        bOnCone = bOnCone && FMath::Abs(toNose - gimbal) <= NPCAI_EPS;
        const FVector3d cross = FVector3d::CrossProduct(noseUnit, aimUnit);
        if (cross.Size() > 1.0e-3)
        {
            const FVector3d toward = (aimUnit - noseUnit * FVector3d::DotProduct(noseUnit, aimUnit)).GetSafeNormal();
            bCoplanar = bCoplanar && FMath::Abs(FVector3d::DotProduct(result, cross.GetSafeNormal())) <= NPCAI_EPS;
            bAimSide = bAimSide && FMath::Abs(FVector3d::DotProduct(result, toward) - FMath::Sin(gimbal)) <= NPCAI_EPS;
        }
    }
    TestTrue(TEXT("Random: unit"), bUnit);
    TestTrue(TEXT("Random: angle to nose <= gimbal"), bWithinGimbal);
    TestTrue(TEXT("Random: the aim when inside the gimbal"), bAimInside);
    TestTrue(TEXT("Random: on the gimbal cone when outside"), bOnCone);
    TestTrue(TEXT("Random: coplanar with nose and aim when outside"), bCoplanar);
    TestTrue(TEXT("Random: on the aim's side when outside"), bAimSide);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
