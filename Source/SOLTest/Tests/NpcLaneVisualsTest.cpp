/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #38 Part 38b lane renderer's names, developer switch and materials (SDD 38 sections 4.7, 4.8,
// D7, D16, D20), written test-first against the SDD: the constants and parameter names the renderer and
// Tools/Npc/create_npc_assets.py share, the sol.NpcLanes cvar, and M_SOLNpcLanes / M_SOLNpcPoints exposing the
// parameters ASOLNpcVisuals sets. SOLTest.NpcLaneVisuals.Materials fails until the materials are rebuilt with
// -SOLRebuild. The look itself (ribbon placement, crossfade, occlusion, colours after tone mapping) is smoke-only.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Material parameter kinds the helper below looks up
    enum class ENpcLaneVisualsParam : uint8
    {
        Scalar,
        Vector,
        Texture,
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a material exposes a parameter of a kind with a name
    bool NpcLaneVisualsHasParameter(const UMaterialInterface& material, const ENpcLaneVisualsParam kind,
        const TCHAR* name)
    {
        TArray<FMaterialParameterInfo> infos;
        TArray<FGuid> ids;
        switch (kind)
        {
        case ENpcLaneVisualsParam::Scalar:
            material.GetAllScalarParameterInfo(infos, ids);
            break;
        case ENpcLaneVisualsParam::Vector:
            material.GetAllVectorParameterInfo(infos, ids);
            break;
        case ENpcLaneVisualsParam::Texture:
            material.GetAllTextureParameterInfo(infos, ids);
            break;
        }
        const FName wanted(name);
        return infos.ContainsByPredicate([&wanted](const FMaterialParameterInfo& info)
        {
            return info.Name == wanted;
        });
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneVisualsNamesTest, "SOLTest.NpcLaneVisuals.Names",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The renderer's names in one place (style guide section 7): the cvar, the lane material path, the material parameter
// names of SDD 38 section 4.8, and the tuned emissive scale of the plan
bool FSOLNpcLaneVisualsNamesTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("cvar sol.NpcLanes (D20)"), FString(SOL::NPC_LANE_CVAR), FString(TEXT("sol.NpcLanes")));
    TestEqual(TEXT("lane material path"), FString(SOL::Paths::NPC_LANES_MATERIAL),
        FString(TEXT("/Game/SOL/Npc/M_SOLNpcLanes.M_SOLNpcLanes")));
    TestEqual(TEXT("lane material: NpcLaneParams0"), FString(SOL::NpcLaneMaterialParams::LANE_PARAMS0),
        FString(TEXT("NpcLaneParams0")));
    TestEqual(TEXT("point material: NpcParams4"), FString(SOL::NpcLaneMaterialParams::POINT_LANE_PARAMS),
        FString(TEXT("NpcParams4")));
    TestEqual(TEXT("lane material: GlowScale"), FString(SOL::NpcLaneMaterialParams::GLOW_SCALE),
        FString(TEXT("GlowScale")));
    TestEqual(TEXT("group texture: NpcGroupTex"), FString(SOL::NpcLaneMaterialParams::GROUP_TEXTURE),
        FString(TEXT("NpcGroupTex")));

    // Emissive: tuned on the screenshots (D16), positive and finite
    const double emissive = SOL::NPC_LANE_EMISSIVE;
    TestTrue(TEXT("emissive positive and finite"), FMath::IsFinite(emissive) && emissive > 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneVisualsCvarTest, "SOLTest.NpcLaneVisuals.Cvar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D20: the developer switch sol.NpcLanes is registered with the module, an integer, and on (1) by default
bool FSOLNpcLaneVisualsCvarTest::RunTest(const FString& /*parameters*/)
{
    IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(SOL::NPC_LANE_CVAR);
    if (!TestNotNull(TEXT("sol.NpcLanes is registered"), cvar))
    {
        return false;
    }
    TestTrue(TEXT("an integer switch"), cvar->IsVariableInt());
    TestEqual(TEXT("on by default"), cvar->GetInt(), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneVisualsMaterialsTest, "SOLTest.NpcLaneVisuals.Materials",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Section 4.8 and 4.12: M_SOLNpcLanes loads, exposes every parameter of SOL::NpcLaneMaterialParams that
// ASOLNpcVisuals sets on it (the per-frame vectors, GlowScale, the group texture and the once-set style, glow and
// colour vectors) and is additive, unlit, two-sided, drawn with the depth test off and after motion blur like the orbit
// lines; M_SOLNpcPoints exposes the crossfade input NpcParams4 and the group texture. A renamed parameter fails here
// instead of silently drawing its default
bool FSOLNpcLaneVisualsMaterialsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::NpcLaneMaterialParams;

    // Every lane material parameter name with its kind (POINT_LANE_PARAMS belongs to the point material)
    struct FNamedParam
    {
        const TCHAR* Name;
        ENpcLaneVisualsParam Kind;
    };
    const FNamedParam laneParams[] = {
        { LANE_PARAMS0, ENpcLaneVisualsParam::Vector },
        { FRAME, ENpcLaneVisualsParam::Vector },
        { GLOW_SCALE, ENpcLaneVisualsParam::Scalar },
        { GROUP_TEXTURE, ENpcLaneVisualsParam::Texture },
        { STYLE0, ENpcLaneVisualsParam::Vector },
        { STYLE1, ENpcLaneVisualsParam::Vector },
        { STYLE2, ENpcLaneVisualsParam::Vector },
        { GLOW0, ENpcLaneVisualsParam::Vector },
        { GLOW1, ENpcLaneVisualsParam::Vector },
        { OUTBOUND_COLOR, ENpcLaneVisualsParam::Vector },
        { INBOUND_COLOR, ENpcLaneVisualsParam::Vector },
    };
    const UMaterialInterface* lanes = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::NPC_LANES_MATERIAL);
    if (TestNotNull(TEXT("M_SOLNpcLanes loads"), lanes))
    {
        for (const FNamedParam& param : laneParams)
        {
            TestTrue(FString::Printf(TEXT("lanes: parameter %s"), param.Name),
                NpcLaneVisualsHasParameter(*lanes, param.Kind, param.Name));
        }
        TestTrue(TEXT("lanes: additive"), lanes->GetBlendMode() == BLEND_Additive);
        TestTrue(TEXT("lanes: unlit"), lanes->GetShadingModels().IsUnlit());
        TestTrue(TEXT("lanes: two-sided"), lanes->IsTwoSided());
        const UMaterial* base = lanes->GetMaterial();
        if (TestNotNull(TEXT("lanes: base material"), base))
        {
            TestTrue(TEXT("lanes: depth test off"), base->bDisableDepthTest != 0);
            TestTrue(TEXT("lanes: drawn after motion blur (issue #25, like the orbit lines)"),
                base->TranslucencyPass == MTP_AfterMotionBlur);
        }
    }

    const UMaterialInterface* points = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::NPC_POINTS_MATERIAL);
    if (TestNotNull(TEXT("M_SOLNpcPoints loads"), points))
    {
        TestTrue(TEXT("points: vector NpcParams4"),
            NpcLaneVisualsHasParameter(*points, ENpcLaneVisualsParam::Vector, POINT_LANE_PARAMS));
        TestTrue(TEXT("points: texture NpcGroupTex"),
            NpcLaneVisualsHasParameter(*points, ENpcLaneVisualsParam::Texture, GROUP_TEXTURE));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
