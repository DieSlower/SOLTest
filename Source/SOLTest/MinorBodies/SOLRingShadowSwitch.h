/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Developer A/B switch for every analytic ring and planet shadow (SDD 16): the console variable sol.RingShadows. Kept
// out of the pure SOLRingShadowMath module, which holds no global state
namespace SOLRingShadow
{
    // Returns false while sol.RingShadows is 0: the gas giant decks, the far ring discs, the ring rocks and the ship's
    // sunlight then all draw unshadowed (GPU profiling of the shadow cost)
    SOLTEST_API bool AreShadowsEnabled();
}
