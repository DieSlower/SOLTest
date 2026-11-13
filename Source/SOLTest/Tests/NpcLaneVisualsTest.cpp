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
    TestEqual(TEXT("tunnel texture: NpcTunnelTex (SDD 49 Q5, case 17c)"),
        FString(SOL::NpcLaneMaterialParams::TUNNEL_TEXTURE), FString(TEXT("NpcTunnelTex")));

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
// lines; M_SOLNpcPoints exposes the crossfade input NpcParams4 and the group texture. Since issue #49 (SDD 49 case
// 17c, Q5) both also read the tunnel fade from the texture NpcTunnelTex. A renamed parameter fails here instead of
// silently drawing its default
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
        { TUNNEL_TEXTURE, ENpcLaneVisualsParam::Texture },
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
        TestTrue(TEXT("points: texture NpcTunnelTex (the lane ships' crossfade with the tunnel, Q5)"),
            NpcLaneVisualsHasParameter(*points, ENpcLaneVisualsParam::Texture, TUNNEL_TEXTURE));
    }
    return true;
}

// Issue #49 Part 49b (SDD 49 sections 4.7 and 4.10, case 17c), test first: the tunnel renderer's names, its developer
// switch and M_SOLNpcTunnel. SOLTest.NpcLaneVisuals.TunnelMaterial fails until the material is built with -SOLRebuild

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneVisualsTunnelNamesTest, "SOLTest.NpcLaneVisuals.TunnelNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The tunnel's names in one place (style guide section 7): the cvar, the material path, the two smoke flags, and the
// parameter names of SOL::NpcTunnelMaterialParams: the shared tunnel texture (the same name the lane and point
// materials read), the glow scale, and the per-kind name lists the material test walks, every name non-empty and
// unique
bool FSOLNpcLaneVisualsTunnelNamesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::NpcTunnelMaterialParams;
    TestEqual(TEXT("cvar sol.NpcTunnel"), FString(SOL::NPC_TUNNEL_CVAR), FString(TEXT("sol.NpcTunnel")));
    TestEqual(TEXT("tunnel material path"), FString(SOL::Paths::NPC_TUNNEL_MATERIAL),
        FString(TEXT("/Game/SOL/Npc/M_SOLNpcTunnel.M_SOLNpcTunnel")));
    TestEqual(TEXT("smoke flag"), FString(SOL::CommandLine::SMOKE_NPC_TUNNEL), FString(TEXT("SOLSmokeNpcTunnel")));
    TestEqual(TEXT("profile smoke flag"), FString(SOL::CommandLine::SMOKE_NPC_TUNNEL_PROFILE),
        FString(TEXT("SOLSmokeNpcTunnelProfile")));
    TestEqual(TEXT("tunnel material: NpcTunnelTex"), FString(TUNNEL_TEXTURE), FString(TEXT("NpcTunnelTex")));
    TestEqual(TEXT("one texture name for the tunnel, lane and point materials"), FString(TUNNEL_TEXTURE),
        FString(SOL::NpcLaneMaterialParams::TUNNEL_TEXTURE));
    TestEqual(TEXT("tunnel material: GlowScale"), FString(GLOW_SCALE), FString(TEXT("GlowScale")));

    // The per-kind lists: the texture and glow scale are listed, every name is non-empty and appears once
    TSet<FString> seen;
    bool bAllUnique = true;
    bool bAllNamed = true;
    for (const TConstArrayView<const TCHAR* const> names : { TConstArrayView<const TCHAR* const>(SCALAR_NAMES),
        TConstArrayView<const TCHAR* const>(VECTOR_NAMES), TConstArrayView<const TCHAR* const>(TEXTURE_NAMES) })
    {
        for (const TCHAR* name : names)
        {
            const FString text(name);
            bAllNamed &= !text.IsEmpty();
            bool bAlreadySeen = false;
            seen.Add(text, &bAlreadySeen);
            bAllUnique &= !bAlreadySeen;
        }
    }
    TestTrue(TEXT("every listed name non-empty"), bAllNamed);
    TestTrue(TEXT("every listed name unique"), bAllUnique);
    TestTrue(TEXT("the texture list has NpcTunnelTex"),
        TConstArrayView<const TCHAR* const>(TEXTURE_NAMES).ContainsByPredicate([](const TCHAR* name)
        {
            return FString(name) == TUNNEL_TEXTURE;
        }));
    TestTrue(TEXT("the scalar list has GlowScale"),
        TConstArrayView<const TCHAR* const>(SCALAR_NAMES).ContainsByPredicate([](const TCHAR* name)
        {
            return FString(name) == GLOW_SCALE;
        }));
    TestTrue(TEXT("the vector list is not empty (the once-set style and colours)"),
        TConstArrayView<const TCHAR* const>(VECTOR_NAMES).Num() > 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneVisualsTunnelCvarTest, "SOLTest.NpcLaneVisuals.TunnelCvar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 section 4.2: the A/B profiling switch sol.NpcTunnel is registered with the module, an integer, and on (1) by
// default
bool FSOLNpcLaneVisualsTunnelCvarTest::RunTest(const FString& /*parameters*/)
{
    IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(SOL::NPC_TUNNEL_CVAR);
    if (!TestNotNull(TEXT("sol.NpcTunnel is registered"), cvar))
    {
        return false;
    }
    TestTrue(TEXT("an integer switch"), cvar->IsVariableInt());
    TestEqual(TEXT("on by default"), cvar->GetInt(), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneVisualsTunnelMaterialTest, "SOLTest.NpcLaneVisuals.TunnelMaterial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 section 4.7 (case 17c): M_SOLNpcTunnel loads from SOL::Paths::NPC_TUNNEL_MATERIAL, exposes every name of
// SOL::NpcTunnelMaterialParams with its kind, and is additive, unlit, two-sided, drawn with the depth test off and
// after motion blur like the lanes and orbit lines (issue #25)
bool FSOLNpcLaneVisualsTunnelMaterialTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::NpcTunnelMaterialParams;
    const UMaterialInterface* tunnel = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::NPC_TUNNEL_MATERIAL);
    if (!TestNotNull(TEXT("M_SOLNpcTunnel loads"), tunnel))
    {
        return false;
    }

    // Every listed name with the kind of its list
    struct FNamedList
    {
        TConstArrayView<const TCHAR* const> Names;
        ENpcLaneVisualsParam Kind;
        const TCHAR* KindName;
    };
    const FNamedList lists[] = {
        { SCALAR_NAMES, ENpcLaneVisualsParam::Scalar, TEXT("scalar") },
        { VECTOR_NAMES, ENpcLaneVisualsParam::Vector, TEXT("vector") },
        { TEXTURE_NAMES, ENpcLaneVisualsParam::Texture, TEXT("texture") },
    };
    for (const FNamedList& list : lists)
    {
        for (const TCHAR* name : list.Names)
        {
            TestTrue(FString::Printf(TEXT("tunnel: %s parameter %s"), list.KindName, name),
                NpcLaneVisualsHasParameter(*tunnel, list.Kind, name));
        }
    }
    TestTrue(TEXT("tunnel: texture NpcTunnelTex"),
        NpcLaneVisualsHasParameter(*tunnel, ENpcLaneVisualsParam::Texture, TUNNEL_TEXTURE));
    TestTrue(TEXT("tunnel: scalar GlowScale"),
        NpcLaneVisualsHasParameter(*tunnel, ENpcLaneVisualsParam::Scalar, GLOW_SCALE));

    // Blend, shading and pass
    TestTrue(TEXT("tunnel: additive"), tunnel->GetBlendMode() == BLEND_Additive);
    TestTrue(TEXT("tunnel: unlit"), tunnel->GetShadingModels().IsUnlit());
    TestTrue(TEXT("tunnel: two-sided"), tunnel->IsTwoSided());
    const UMaterial* base = tunnel->GetMaterial();
    if (TestNotNull(TEXT("tunnel: base material"), base))
    {
        TestTrue(TEXT("tunnel: depth test off"), base->bDisableDepthTest != 0);
        TestTrue(TEXT("tunnel: drawn after motion blur (issue #25, like the lanes)"),
            base->TranslucencyPass == MTP_AfterMotionBlur);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
