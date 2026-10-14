/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingShadowSwitch.h"

#include "HAL/IConsoleManager.h"

namespace
{
    // Developer A/B switch for the analytic ring and planet shadows (SDD 16 16b-16d GPU measurements)
    TAutoConsoleVariable<int32> CVarSOLRingShadows(TEXT("sol.RingShadows"), 1,
        TEXT("1: analytic ring and planet shadows (gas giants, ring discs, ring rocks, ship sunlight; SDD 16); "
            "0: all off (A/B profiling)"));
}

//////////////////////////////////////////////////////////////////////////
// Returns false while sol.RingShadows is 0
bool SOLRingShadow::AreShadowsEnabled()
{
    return CVarSOLRingShadows.GetValueOnGameThread() != 0;
}
