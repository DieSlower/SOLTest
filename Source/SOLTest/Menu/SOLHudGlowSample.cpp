/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLHudGlowSample.h"

#include "Menu/SOLGlossary.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSolidMenuMath.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudShapeLayer.h"
#include "UI/SOLHudTextList.h"

#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SlateRenderer.h"

namespace
{
    // The picture's layout in fractions of the view's height: the ring in the left square, the text line and the
    // line to its right
    constexpr float GLOWSAMPLE_RING_RADIUS = 0.3f;          // Ring radius
    constexpr float GLOWSAMPLE_CONTENT_LEFT = 1.0f;         // Where the text and the line start (x)
    constexpr float GLOWSAMPLE_RIGHT_MARGIN = 0.15f;        // Room right of the text and the line
    constexpr float GLOWSAMPLE_TEXT_TOP = 0.08f;            // The text line's top
    constexpr float GLOWSAMPLE_LINE_Y = 0.78f;              // The line's height
    constexpr float GLOWSAMPLE_INTENSITY = 1.0f;            // Glow intensity of the ring and the line
    constexpr int32 GLOWSAMPLE_FIT_PASSES = 2;              // Text measures (Slate rounds a scaled font's pixel size)

    // Whether a sample already logged that the HUD shape material is missing (logged once per run)
    bool GHudGlowSampleMaterialWarned = false;

    //////////////////////////////////////////////////////////////////////////
    // Returns the engine font of a HUD font slot (the fonts the flight HUD draws its text with)
    const UFont* HudGlowSampleFont(const ESOLHudFont font)
    {
        if (GEngine == nullptr)
        {
            return nullptr;
        }
        switch (font)
        {
        case ESOLHudFont::Large:
            return GEngine->GetLargeFont();
        case ESOLHudFont::Medium:
            return GEngine->GetMediumFont();
        default:
            return GEngine->GetSmallFont();
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Clips to the view's bounds (the glow quads reach past the art) and takes no hit test
USOLHudGlowSample::USOLHudGlowSample()
{
    SetClipping(EWidgetClipping::ClipToBounds);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    mText = FText::FromString(SOLMenuText::SETTINGS_GLOW_SAMPLE);
}

//////////////////////////////////////////////////////////////////////////
// Pushes the glow scale, its text alpha and the effects strength on change, and rebuilds on a pixel size change
bool USOLHudGlowSample::Update(const float hudGlowSetting, const float effectsStrength, const float layoutScale)
{
    if (!mLayer.IsValid())
    {
        return false;
    }
    bool bChanged = false;
    if (mGlowFeed.Update(hudGlowSetting))
    {
        if (Material != nullptr)
        {
            Material->SetScalarParameterValue(SOL::HudShapeMaterialParams::GLOW_SCALE, mGlowFeed.GetScale());
        }
        mLayer->SetTextGlowAlpha(mGlowFeed.GetTextAlpha());
        bChanged = true;
    }
    const float strength = SOLSolidMenu::SanitizeStrength(effectsStrength);
    if (strength != mStrength)
    {
        mStrength = strength;
        if (Material != nullptr)
        {
            Material->SetScalarParameterValue(SOL::HudShapeMaterialParams::EFFECTS_STRENGTH, strength);
        }
        bChanged = true;
    }

    // The picture at the view's pixel size, once it has been laid out
    const FGeometry& geometry = GetCachedGeometry();
    const FVector2f localSize(geometry.GetLocalSize());
    const float scale = layoutScale > 0.0f ? layoutScale : geometry.Scale;
    if (!(localSize.X > 0.0f && localSize.Y > 0.0f && scale > 0.0f))
    {
        return bChanged;
    }
    const FIntPoint sizePx = SOLGlossary::ViewPixelSize(localSize, scale);
    if (sizePx == mSizePx || sizePx.X <= 0 || sizePx.Y <= 0)
    {
        return bChanged;
    }
    Rebuild(sizePx, scale);
    mSizePx = sizePx;
    ++mBuildCount;
    if (Material != nullptr)
    {
        Material->SetScalarParameterValue(SOL::HudShapeMaterialParams::VIEW_WIDTH, static_cast<float>(sizePx.X));
        Material->SetScalarParameterValue(SOL::HudShapeMaterialParams::VIEW_HEIGHT, static_cast<float>(sizePx.Y));
    }
    mLayer->MarkBatchChanged();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Refills the layer: one HUD panel backing over the view, the ring, the line and the text line in the HUD's Medium
// font at the glossary's HUD scale, shrunk until it fits beside the ring and above the line
void USOLHudGlowSample::Rebuild(const FIntPoint& sizePx, const float pxPerUnit)
{
    using namespace SOL::Hologram;
    const FVector2f size(sizePx);
    const float height = size.Y;
    const float left = GLOWSAMPLE_CONTENT_LEFT * height;
    const float right = size.X - GLOWSAMPLE_RIGHT_MARGIN * height;
    const float lineY = GLOWSAMPLE_LINE_Y * height;
    mLayer->ResetFrame();
    mLayer->AddBacking(FBox2f(FVector2f::ZeroVector, size), static_cast<float>(HUD_PANEL_CORNER_PX) * pxPerUnit,
        static_cast<float>(HUD_PANEL_FEATHER_PX) * pxPerUnit, 1.0f);
    FSOLHudShapeBatch& batch = mLayer->GetBatch();
    batch.AddRing(FVector2f(0.5f * height), GLOWSAMPLE_RING_RADIUS * height, HUD_LINE_HALF_WIDTH_PX,
        SOL::HudPalette::PRIMARY, GLOWSAMPLE_INTENSITY, HUD_GLOW_PAD_PX);
    if (right > left)
    {
        batch.AddLine(FVector2f(left, lineY), FVector2f(right, lineY), HUD_LINE_HALF_WIDTH_PX,
            SOL::HudPalette::PRIMARY, GLOWSAMPLE_INTENSITY, HUD_GLOW_PAD_PX);
    }

    // The text line, measured as the layer draws it (the Slate info of the font with the glow outline)
    const UFont* font = HudGlowSampleFont(ESOLHudFont::Medium);
    if (font == nullptr || !FSlateApplication::IsInitialized() || right <= left)
    {
        return;
    }
    FSlateFontInfo fontInfo = font->GetLegacySlateFontInfo();
    fontInfo.OutlineSettings.OutlineSize = HUD_TEXT_GLOW_OUTLINE;
    const TSharedRef<FSlateFontMeasure> measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    const FVector2f room(right - left, lineY - 2.0f * GLOWSAMPLE_TEXT_TOP * height);
    float scale = SOL::Help::GLOSSARY_SYMBOL_UI_SCALE * pxPerUnit;
    for (int32 pass = 0; pass < GLOWSAMPLE_FIT_PASSES; ++pass)
    {
        const FVector2f drawn(measure->Measure(mText.ToString(), fontInfo, scale));
        const float fit = FMath::Min(drawn.X > room.X ? room.X / drawn.X : 1.0f,
            drawn.Y > room.Y ? room.Y / drawn.Y : 1.0f);
        if (fit >= 1.0f)
        {
            break;
        }
        scale *= fit;
    }
    mLayer->GetTexts().Add(mText, FVector2f(left, GLOWSAMPLE_TEXT_TOP * height), scale, SOL::HudPalette::PRIMARY,
        ESOLHudFont::Medium);
}

//////////////////////////////////////////////////////////////////////////
// Returns the layer's glow-text outline alpha, -1 without a layer
float USOLHudGlowSample::GetTextGlowAlpha() const
{
    return mLayer.IsValid() ? mLayer->GetTextGlowAlpha() : -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the shapes the last paint drew
int32 USOLHudGlowSample::GetPaintedShapeCount() const
{
    return mLayer.IsValid() ? mLayer->GetLastPaintedShapeCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the text lines the last paint drew
int32 USOLHudGlowSample::GetPaintedTextCount() const
{
    return mLayer.IsValid() ? mLayer->GetLastPaintedTextCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the backings the last paint drew
int32 USOLHudGlowSample::GetPaintedBackingCount() const
{
    return mLayer.IsValid() ? mLayer->GetLastPaintedBackingCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the shapes of the last build
int32 USOLHudGlowSample::GetShapeCount() const
{
    return mLayer.IsValid() ? mLayer->GetBatch().GetShapeCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the material the layer draws with, or null
UObject* USOLHudGlowSample::GetMaterial() const
{
    return mLayer.IsValid() ? mLayer->GetMaterial() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Drops the layer and forgets the build and the pushed values
void USOLHudGlowSample::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mLayer.Reset();
    mSizePx = FIntPoint::ZeroValue;
    mGlowFeed.Reset();
    mStrength = -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Creates the retained shape layer (a few shapes, one text line, one backing) with the sample's own HUD shape material
// instance (grain and scanlines on, like the glossary's), the HUD's engine fonts and the glow text
TSharedRef<SWidget> USOLHudGlowSample::RebuildWidget()
{
    mLayer = SNew(SSOLHudShapeLayer).MaxShapes(SOL::SolidMenu::SETTINGS_GLOW_SAMPLE_MAX_SHAPES).MaxTexts(1)
        .MaxBackings(1);
    mLayer->SetRetainBatch(true);
    if (Material == nullptr)
    {
        UMaterialInterface* parent = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::HUD_SHAPE_MATERIAL);
        Material = parent != nullptr ? UMaterialInstanceDynamic::Create(parent, this) : nullptr;
        if (Material != nullptr)
        {
            Material->SetScalarParameterValue(SOL::HudShapeMaterialParams::COMPOSITE, 1.0f);
        }
        else if (!GHudGlowSampleMaterialWarned)
        {
            GHudGlowSampleMaterialWarned = true;
            UE_LOG(LogSOL, Warning, TEXT("HudGlowSample %s: no %s; the sample draws its backing and text only"),
                *GetName(), SOL::Paths::HUD_SHAPE_MATERIAL);
        }
    }
    mLayer->SetMaterial(Material);

    // The text line uses the engine fonts the flight HUD draws with (GEngine's fonts stay loaded)
    for (int32 index = 0; index < static_cast<int32>(ESOLHudFont::Count); ++index)
    {
        const ESOLHudFont font = static_cast<ESOLHudFont>(index);
        if (const UFont* engineFont = HudGlowSampleFont(font))
        {
            mLayer->SetFont(font, engineFont->GetLegacySlateFontInfo());
        }
    }
    mLayer->SetTextGlow(true);
    mSizePx = FIntPoint::ZeroValue;
    mGlowFeed.Reset();
    mStrength = -1.0f;
    return mLayer.ToSharedRef();
}
