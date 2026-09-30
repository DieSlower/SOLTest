/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLViewpointResolve.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Integer-valued, exactly representable universe points: an override viewpoint and a distinct observer position
    const FVector3d VIEWPOINT_OVERRIDE_M(300000000000.0, -45000000000.0, 7000000000.0);
    const FVector3d VIEWPOINT_OBSERVER_M(149597870700.0, 20000000000.0, -3000000000.0);

    //////////////////////////////////////////////////////////////////////////
    // Checks one Resolve call's result against the expected position (exact) and snap flag
    void CheckViewpointResolve(FAutomationTestBase& test, const TCHAR* caseName,
        const FSOLViewpointResolveResult& actual, const FVector3d& expectedPositionM, const bool bExpectedSnap)
    {
        test.TestTrue(FString::Printf(TEXT("%s: EffectivePositionM exact (got %s, expected %s)"), caseName,
            *actual.EffectivePositionM.ToString(), *expectedPositionM.ToString()),
            actual.EffectivePositionM == expectedPositionM);
        test.TestEqual(FString::Printf(TEXT("%s: bForceSnap"), caseName), actual.bForceSnap, bExpectedSnap);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLViewpointResolveOverrideHeldTest, "SOLTest.ViewpointResolve.OverrideHeldUsesOverrideNoSnap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An override active last frame and this frame renders from the override without forcing a snap
bool FSOLViewpointResolveOverrideHeldTest::RunTest(const FString& /*parameters*/)
{
    const FSOLViewpointResolveResult result =
        SOLViewpoint::Resolve(true, true, VIEWPOINT_OVERRIDE_M, VIEWPOINT_OBSERVER_M);
    CheckViewpointResolve(*this, TEXT("held override"), result, VIEWPOINT_OVERRIDE_M, false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLViewpointResolveNoOverrideTest, "SOLTest.ViewpointResolve.NoOverrideUsesObserverNoSnap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With no override last frame or this frame, rendering uses the observer and no snap is forced
bool FSOLViewpointResolveNoOverrideTest::RunTest(const FString& /*parameters*/)
{
    const FSOLViewpointResolveResult result =
        SOLViewpoint::Resolve(false, false, VIEWPOINT_OVERRIDE_M, VIEWPOINT_OBSERVER_M);
    CheckViewpointResolve(*this, TEXT("no override"), result, VIEWPOINT_OBSERVER_M, false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLViewpointResolveTurnOnTest, "SOLTest.ViewpointResolve.OverrideTurnedOnUsesOverrideAndSnaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The frame an override turns on, rendering switches to the override and the origin is forced to snap
bool FSOLViewpointResolveTurnOnTest::RunTest(const FString& /*parameters*/)
{
    const FSOLViewpointResolveResult result =
        SOLViewpoint::Resolve(false, true, VIEWPOINT_OVERRIDE_M, VIEWPOINT_OBSERVER_M);
    CheckViewpointResolve(*this, TEXT("override on"), result, VIEWPOINT_OVERRIDE_M, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLViewpointResolveTurnOffTest, "SOLTest.ViewpointResolve.OverrideTurnedOffUsesObserverAndSnaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The frame an override turns off, rendering returns to the observer and the origin is forced to snap
bool FSOLViewpointResolveTurnOffTest::RunTest(const FString& /*parameters*/)
{
    const FSOLViewpointResolveResult result =
        SOLViewpoint::Resolve(true, false, VIEWPOINT_OVERRIDE_M, VIEWPOINT_OBSERVER_M);
    CheckViewpointResolve(*this, TEXT("override off"), result, VIEWPOINT_OBSERVER_M, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLViewpointResolveLargeMagnitudeTest, "SOLTest.ViewpointResolve.LargeMagnitudePassesThroughExactly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Solar-system-scale positions (1e11 to 1e13 m, with sub-meter fractions) are selected bit-exactly, never rounded
bool FSOLViewpointResolveLargeMagnitudeTest::RunTest(const FString& /*parameters*/)
{
    // Around Neptune's distance and beyond, with fractional parts that float truncation would destroy
    const FVector3d overrideM(4.4951e12 + 0.125, -9.87654321e12 - 0.5, 1.23e11 + 0.25);
    const FVector3d observerM(-1.0e13 + 0.375, 3.0 * SOLTestHelpers::AU_M + 0.0625, -2.5e11 - 0.75);

    // Every flag combination must return one of the two inputs exactly
    CheckViewpointResolve(*this, TEXT("large held"),
        SOLViewpoint::Resolve(true, true, overrideM, observerM), overrideM, false);
    CheckViewpointResolve(*this, TEXT("large none"),
        SOLViewpoint::Resolve(false, false, overrideM, observerM), observerM, false);
    CheckViewpointResolve(*this, TEXT("large on"),
        SOLViewpoint::Resolve(false, true, overrideM, observerM), overrideM, true);
    CheckViewpointResolve(*this, TEXT("large off"),
        SOLViewpoint::Resolve(true, false, overrideM, observerM), observerM, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLViewpointResolveSnapIndependentOfPositionsTest,
    "SOLTest.ViewpointResolve.SnapDependsOnTransitionNotPositions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// bForceSnap follows only the on/off transition: coinciding positions still snap on a transition, differing ones don't
// snap without one
bool FSOLViewpointResolveSnapIndependentOfPositionsTest::RunTest(const FString& /*parameters*/)
{
    // Override and observer equal in value: a transition still forces a snap, no transition still doesn't
    const FVector3d sameM = VIEWPOINT_OBSERVER_M;
    CheckViewpointResolve(*this, TEXT("equal positions, on"),
        SOLViewpoint::Resolve(false, true, sameM, sameM), sameM, true);
    CheckViewpointResolve(*this, TEXT("equal positions, off"),
        SOLViewpoint::Resolve(true, false, sameM, sameM), sameM, true);
    CheckViewpointResolve(*this, TEXT("equal positions, held"),
        SOLViewpoint::Resolve(true, true, sameM, sameM), sameM, false);
    CheckViewpointResolve(*this, TEXT("equal positions, none"),
        SOLViewpoint::Resolve(false, false, sameM, sameM), sameM, false);

    // Widely differing positions without a transition never force a snap (drift is FSOLRenderOrigin's job)
    const FVector3d farOverrideM = VIEWPOINT_OBSERVER_M + FVector3d(5.0e12, -5.0e12, 1.0e11);
    CheckViewpointResolve(*this, TEXT("differing positions, held"),
        SOLViewpoint::Resolve(true, true, farOverrideM, VIEWPOINT_OBSERVER_M), farOverrideM, false);
    CheckViewpointResolve(*this, TEXT("differing positions, none"),
        SOLViewpoint::Resolve(false, false, farOverrideM, VIEWPOINT_OBSERVER_M), VIEWPOINT_OBSERVER_M, false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
