/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHologramCursorSubsystem.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLOverlayIcons.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

//////////////////////////////////////////////////////////////////////////
// Builds the arrow's shapes once: glow lines around the widget centre (the hot spot)
void SSOLHologramCursor::Construct(const FArguments& /*args*/)
{
    SetVisibility(EVisibility::HitTestInvisible);
    mBatch.Initialize(SOL::Overlay::CURSOR_SEGMENTS);
    FVector2f starts[SOL::Overlay::CURSOR_SEGMENTS];
    FVector2f ends[SOL::Overlay::CURSOR_SEGMENTS];
    SOLHologramCursor::ArrowSegments(1.0f, starts, ends);
    const FVector2f center(0.5f * SOLHologramCursor::WidgetSidePx(1.0f, SOL::Hologram::HUD_GLOW_PAD_PX));
    for (int32 segment = 0; segment < SOL::Overlay::CURSOR_SEGMENTS; ++segment)
    {
        mBatch.AddLine(center + starts[segment], center + ends[segment], SOL::Hologram::HUD_LINE_HALF_WIDTH_PX,
            SOL::HudPalette::PRIMARY, SOL::Overlay::CURSOR_INTENSITY, SOL::Hologram::HUD_GLOW_PAD_PX);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the material and the settings the effects strength is read from
void SSOLHologramCursor::SetMaterial(UMaterialInstanceDynamic* material, USOLSettingsSubsystem* settings)
{
    mMaterial = material;
    mSettings = settings;
    mBrush = FSlateBrush();
    mBrush.SetResourceObject(material);
    mBrush.ImageSize = FVector2D(1.0, 1.0);
    mResourceHandle = FSlateResourceHandle();
    mPushedStrength = -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// The square whose centre is the hot spot
FVector2D SSOLHologramCursor::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D(SOLHologramCursor::WidgetSidePx(1.0f, SOL::Hologram::HUD_GLOW_PAD_PX));
}

//////////////////////////////////////////////////////////////////////////
// Draws the arrow as one custom-vertex element (local pixels -> window space)
int32 SSOLHologramCursor::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& /*widgetStyle*/, const bool /*bParentEnabled*/) const
{
    UMaterialInstanceDynamic* material = mMaterial.Get();
    if (material == nullptr || !FSlateApplication::IsInitialized())
    {
        return layerId;
    }

    // The hologram menu effects slider (row jitter, decision D19) through the HUD material's strength parameter; pushed only when it changes
    const USOLSettingsSubsystem* settings = mSettings.Get();
    const float strength = settings != nullptr ? settings->GetLiveValues().HoloMenuEffects
        : SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;
    if (strength != mPushedStrength)
    {
        mPushedStrength = strength;
        material->SetScalarParameterValue(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH, strength);
    }
    if (!mResourceHandle.IsValid())
    {
        mResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(mBrush);
    }
    if (!mResourceHandle.IsValid())
    {
        return layerId;
    }
    const FSlateRenderTransform& renderTransform = allottedGeometry.GetAccumulatedRenderTransform();
    mPaintVertices.Reset();
    mPaintVertices.Append(mBatch.GetVertices());
    for (FSlateVertex& vertex : mPaintVertices)
    {
        vertex.Position = renderTransform.TransformPoint(vertex.Position);
    }
    FSlateDrawElement::MakeCustomVerts(outDrawElements, layerId, mResourceHandle, mPaintVertices, mBatch.GetIndices(),
        nullptr, 0, 0);
    ++mPaintCount;
    return layerId + 1;
}

//////////////////////////////////////////////////////////////////////////
// Game and PIE worlds only
bool USOLHologramCursorSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Creates the material instance and the cursor widget and hands them to the game viewport for every cursor type
void USOLHologramCursorSubsystem::OnWorldBeginPlay(UWorld& world)
{
    Super::OnWorldBeginPlay(world);
    UGameViewportClient* viewport = world.GetGameViewport();
    UMaterialInterface* material = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::HUD_SHAPE_MATERIAL);
    if (viewport == nullptr || material == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("HologramCursor: no %s; the system cursor stays"),
            viewport == nullptr ? TEXT("game viewport") : SOL::Paths::HUD_SHAPE_MATERIAL);
        return;
    }
    CursorMaterial = UMaterialInstanceDynamic::Create(material, this);
    mCursor = SNew(SSOLHologramCursor);
    mCursor->SetMaterial(CursorMaterial, USOLSettingsSubsystem::Get(&world));
    for (int32 type = 0; type < EMouseCursor::TotalCursorCount; ++type)
    {
        if (type != EMouseCursor::None && type != EMouseCursor::Custom)
        {
            viewport->SetSoftwareCursorWidget(static_cast<EMouseCursor::Type>(type), mCursor);
        }
    }
    viewport->SetUseSoftwareCursorWidgets(true);
    mViewport = viewport;
    UE_LOG(LogSOL, Log, TEXT("HologramCursor: hologram pointer installed as the software cursor"));
}

//////////////////////////////////////////////////////////////////////////
// Takes the cursor widget back from the game viewport (the system arrow returns)
void USOLHologramCursorSubsystem::Deinitialize()
{
    if (UGameViewportClient* viewport = mViewport.Get())
    {
        for (int32 type = 0; type < EMouseCursor::TotalCursorCount; ++type)
        {
            if (viewport->GetSoftwareCursorWidget(static_cast<EMouseCursor::Type>(type)) == mCursor && mCursor)
            {
                viewport->SetSoftwareCursorWidget(static_cast<EMouseCursor::Type>(type), TSharedPtr<SWidget>());
            }
        }
    }
    mViewport.Reset();
    mCursor.Reset();
    CursorMaterial = nullptr;
    Super::Deinitialize();
}
