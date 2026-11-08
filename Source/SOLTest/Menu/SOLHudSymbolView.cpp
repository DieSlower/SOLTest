/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLHudSymbolView.h"

#include "Menu/SOLSolidMenuSubsystem.h"
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
    // Whether a view already logged that the shared HUD shape material is missing (logged once per run)
    bool GHudSymbolViewMaterialWarned = false;

    //////////////////////////////////////////////////////////////////////////
    // Returns the engine font of a HUD font slot (the fonts the flight HUD draws its text with)
    const UFont* HudSymbolViewFont(const ESOLHudFont font)
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

    //////////////////////////////////////////////////////////////////////////
    // Returns the batch the views dry-build their symbols into to size their own batches (GLOSSARY_MAX_SHAPES,
    // reserved at its first use; the views rebuild one at a time on the game thread)
    FSOLHudShapeBatch& HudSymbolViewDryBatch()
    {
        static FSOLHudShapeBatch batch;
        if (batch.GetMaxShapes() == 0)
        {
            batch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
        }
        return batch;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the paint vertex buffer every view's layer shares (Slate paints on the game thread only, one layer at a
    // time; reserved for GLOSSARY_MAX_SHAPES at the first build, so a paint never grows it)
    TArray<FSlateVertex>& HudSymbolViewPaintScratch()
    {
        static TArray<FSlateVertex> vertices;
        return vertices;
    }
}

//////////////////////////////////////////////////////////////////////////
// Clips to the view's bounds (the glow quads reach past the art), takes no hit test and starts with no symbol
USOLHudSymbolView::USOLHudSymbolView()
{
    SetClipping(EWidgetClipping::ClipToBounds);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    for (ESOLHudSymbol& symbol : mSymbols)
    {
        symbol = ESOLHudSymbol::Count;
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows a row's symbols; a change forces the next rebuild
void USOLHudSymbolView::SetSymbols(const ESOLHudSymbol* symbols, const int32 count)
{
    const int32 clamped = symbols != nullptr ? FMath::Clamp(count, 0, SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY) : 0;
    bool bChanged = mIsSample || clamped != mSymbolCount;
    for (int32 index = 0; index < clamped; ++index)
    {
        bChanged |= mSymbols[index] != symbols[index];
        mSymbols[index] = symbols[index];
    }
    mSymbolCount = clamped;
    mIsSample = false;
    if (bChanged)
    {
        mStamp.bBuilt = false;
        mIsBatchSized = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows a text sample of a glossary row; a change forces the next rebuild
void USOLHudSymbolView::SetSample(const FString& sample, const int32 entryIndex)
{
    if (mIsSample && mSample == sample && mSampleEntry == entryIndex)
    {
        return;
    }
    mSample = sample;
    mSampleEntry = entryIndex;
    mIsSample = true;
    mSymbolCount = 0;
    mStamp.bBuilt = false;
    mIsBatchSized = false;
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the batch when the pixel size (the laid-out size, or GLOSSARY_VIEW_SIZE before the first layout, at the
// page's layout scale) or the strength changed (or it was never built)
bool USOLHudSymbolView::UpdateBatch(const float strength, FSOLHudSymbolScratch& scratch, const float layoutScale)
{
    if (!mLayer.IsValid())
    {
        return false;
    }
    const FGeometry& geometry = GetCachedGeometry();
    const FVector2f laidOut(geometry.GetLocalSize());
    const FVector2f localSize = laidOut.X > 0.0f && laidOut.Y > 0.0f ? laidOut
        : FVector2f(SOL::Help::GLOSSARY_VIEW_SIZE, SOL::Help::GLOSSARY_VIEW_SIZE);
    const float scale = layoutScale > 0.0f ? layoutScale : geometry.Scale;
    const FIntPoint sizePx = SOLGlossary::ViewPixelSize(localSize, scale);
    if (!SOLGlossary::ViewNeedsRebuild(mStamp, sizePx, strength))
    {
        return false;
    }
    Rebuild(sizePx, scale, FSOLSolidMenuParamCache::SanitizeStrength(strength), scratch);
    SOLGlossary::MarkViewBuilt(mStamp, sizePx, strength);
    ++mBuildCount;
    mLayer->MarkBatchChanged();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Refills the layer: one dark HUD panel backing over the whole view, then the symbols or the sample in the art area
// (the batch sized to the content first when the content changed)
void USOLHudSymbolView::Rebuild(const FIntPoint& sizePx, const float pxPerUnit, const float strength,
    FSOLHudSymbolScratch& scratch)
{
    using namespace SOL::Help;
    const FBox2f viewRect(FVector2f::ZeroVector, FVector2f(sizePx));
    mArtArea = viewRect.ExpandBy(-GLOSSARY_SYMBOL_PADDING * pxPerUnit);

    // The HUD palette at the live strength (the cards' hologram look follows it)
    FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    palette.Strength = strength;
    HudSymbolViewPaintScratch().Reserve(GLOSSARY_MAX_SHAPES * SOL::Hologram::HUD_VERTICES_PER_SHAPE);
    if (!mIsBatchSized)
    {
        SizeBatch(pxPerUnit, palette, scratch);
    }
    mLayer->ResetFrame();
    mShapeCount = 0;
    mDroppedCount = 0;
    mFitScale = 0.0f;
    mLayer->AddBacking(viewRect, static_cast<float>(SOL::Hologram::HUD_PANEL_CORNER_PX) * pxPerUnit,
        static_cast<float>(SOL::Hologram::HUD_PANEL_FEATHER_PX) * pxPerUnit, 1.0f);
    FSOLHudShapeBatch& batch = mLayer->GetBatch();
    if (mIsSample)
    {
        BuildSample(pxPerUnit, palette);
    }
    else
    {
        BuildSymbols(pxPerUnit, palette, scratch, batch);

        // A size that makes more shapes than the headroom allows: the batch grows to the cap once and is refilled
        if (batch.GetDroppedCount() > 0 && batch.GetMaxShapes() < GLOSSARY_MAX_SHAPES)
        {
            batch.Initialize(GLOSSARY_MAX_SHAPES);
            mDroppedCount = 0;
            mFitScale = 0.0f;
            BuildSymbols(pxPerUnit, palette, scratch, batch);
        }
    }
    mShapeCount = batch.GetShapeCount();
    mDroppedCount += mLayer->GetBatch().GetDroppedCount() + mLayer->GetTexts().GetDroppedCount();
}

//////////////////////////////////////////////////////////////////////////
// Sizes the layer's batch to the shapes the symbols make at this build's size (a dry build into the shared batch) plus
// GLOSSARY_SHAPE_HEADROOM, capped at GLOSSARY_MAX_SHAPES; a sample draws no shape and holds no batch
void USOLHudSymbolView::SizeBatch(const float pxPerUnit, const FSOLHudSymbolPalette& palette,
    FSOLHudSymbolScratch& scratch)
{
    using namespace SOL::Help;
    int32 needed = 0;
    if (!mIsSample && mSymbolCount > 0)
    {
        FSOLHudShapeBatch& dry = HudSymbolViewDryBatch();
        dry.Reset();
        BuildSymbols(pxPerUnit, palette, scratch, dry);
        needed = FMath::Min(dry.GetShapeCount() + dry.GetDroppedCount() + GLOSSARY_SHAPE_HEADROOM,
            GLOSSARY_MAX_SHAPES);
        dry.Reset();
    }
    FSOLHudShapeBatch& batch = mLayer->GetBatch();
    if (batch.GetMaxShapes() != needed)
    {
        batch.Initialize(needed);
    }
    mIsBatchSized = true;
}

//////////////////////////////////////////////////////////////////////////
// Adds the symbols to a batch: each shrunk to fit its grid cell of the art area (never enlarged past the HUD's 1080 px
// scale) and centred in it, with the HUD's glow pad in pixels (M_SOLHudShape fades its halo over a fixed pixel pad)
void USOLHudSymbolView::BuildSymbols(const float pxPerUnit, const FSOLHudSymbolPalette& palette,
    FSOLHudSymbolScratch& scratch, FSOLHudShapeBatch& batch)
{
    using namespace SOL::Help;
    const float maxScale = GLOSSARY_SYMBOL_UI_SCALE * pxPerUnit;
    const float padPx = SOL::Hologram::HUD_GLOW_PAD_PX;
    for (int32 index = 0; index < mSymbolCount; ++index)
    {
        // Fit at the largest scale, then measure again at the fitted one to centre it
        const ESOLHudSymbol symbol = mSymbols[index];
        const FBox2f cell = SOLGlossary::CellRect(index, mSymbolCount, mArtArea);
        const FBox2f boundsAtMax = SOLHudSymbols::Bounds(symbol, maxScale, scratch.Prims);
        const float scale = SOLGlossary::SymbolFitScale(boundsAtMax, cell.GetSize(), maxScale);
        if (scale <= 0.0f)
        {
            continue;
        }
        const FVector2f centre = SOLGlossary::SymbolCentre(SOLHudSymbols::Bounds(symbol, scale, scratch.Prims), cell);
        SOLHudSymbols::Build(symbol, centre, scale, scratch.Prims);
        mDroppedCount += scratch.Prims.DroppedCount;
        SOLHudSymbols::AddToBatch(scratch.Prims, batch, palette, padPx, scratch.Frame);
        mFitScale = mFitScale > 0.0f ? FMath::Min(mFitScale, scale) : scale;
    }
}

//////////////////////////////////////////////////////////////////////////
// Adds the sample's lines in the HUD's Medium font, laid out in the art area (shrunk to fit down to the sample's
// smallest scale, then wrapped)
void USOLHudSymbolView::BuildSample(const float pxPerUnit, const FSOLHudSymbolPalette& palette)
{
    using namespace SOL::Help;
    mSampleTexts.Reset();
    const UFont* font = HudSymbolViewFont(ESOLHudFont::Medium);
    if (font == nullptr || !mArtArea.bIsValid)
    {
        return;
    }

    // Measured as the layer draws it: Slate's measure of the font's Slate info with the glow outline, at the scale the
    // lines are drawn at (Slate rounds a scaled font to whole pixel sizes, so a small line is wider than its unit width
    // scaled, and the legacy UFont::GetStringSize reads narrower still: a wrapped line ran past the view's clip). The
    // scale is the layout's own result, so it is laid out at unit scale, then again measured at the scale found, until
    // the scale holds (at most GLOSSARY_SAMPLE_MEASURE_PASSES); the lines are made here, never in paint
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }
    FSlateFontInfo fontInfo = font->GetLegacySlateFontInfo();
    fontInfo.OutlineSettings.OutlineSize = SOL::Hologram::HUD_TEXT_GLOW_OUTLINE;
    const TSharedRef<FSlateFontMeasure> measureService = FSlateApplication::Get().GetRenderer()
        ->GetFontMeasureService();
    float measureScale = 1.0f;
    for (int32 pass = 0; pass < GLOSSARY_SAMPLE_MEASURE_PASSES; ++pass)
    {
        // A text's width per unit of scale, as drawn at measureScale
        const auto measure = [&measureService, &fontInfo, measureScale](const FStringView text)
        {
            return static_cast<float>(measureService->Measure(text, fontInfo, measureScale).X) / measureScale;
        };
        const float lineHeight = static_cast<float>(measureService->GetMaxCharacterHeight(fontInfo, measureScale))
            / measureScale;
        SOLGlossary::LayoutSample(mSample, mArtArea.GetSize(), GLOSSARY_SYMBOL_UI_SCALE * pxPerUnit,
            GLOSSARY_SAMPLE_MIN_UI_SCALE * pxPerUnit, lineHeight, measure, mSampleLayout);
        if (!(mSampleLayout.Scale > 0.0f) || mSampleLayout.Scale == measureScale)
        {
            break;
        }
        measureScale = mSampleLayout.Scale;
    }
    mFitScale = mSampleLayout.Scale;
    FSOLHudTextList& texts = mLayer->GetTexts();
    for (const FSOLGlossarySampleLine& line : mSampleLayout.Lines)
    {
        const FText& text = mSampleTexts.Add_GetRef(FText::FromString(line.Text));
        const ESOLHudPaletteRole role = SOLGlossary::SampleLineRole(mSampleEntry, line.Text);
        texts.Add(text, mArtArea.Min + line.Position, mSampleLayout.Scale,
            palette.Colors[static_cast<int32>(role)], ESOLHudFont::Medium);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the shapes the last paint drew
int32 USOLHudSymbolView::GetPaintedShapeCount() const
{
    return mLayer.IsValid() ? mLayer->GetLastPaintedShapeCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the shape capacity the batch holds (0 before the first build and for a sample)
int32 USOLHudSymbolView::GetShapeCapacity() const
{
    return mLayer.IsValid() ? mLayer->GetBatch().GetMaxShapes() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the window-space point the last paint mapped the batch's origin to
FVector2f USOLHudSymbolView::GetLastPaintOrigin() const
{
    return mLayer.IsValid() ? mLayer->GetLastPaintOrigin() : FVector2f::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Returns the text lines of the last build
int32 USOLHudSymbolView::GetTextCount() const
{
    return mLayer.IsValid() ? mLayer->GetTexts().GetItems().Num() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the text lines the last paint drew
int32 USOLHudSymbolView::GetPaintedTextCount() const
{
    return mLayer.IsValid() ? mLayer->GetLastPaintedTextCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the backings the last paint drew
int32 USOLHudSymbolView::GetPaintedBackingCount() const
{
    return mLayer.IsValid() ? mLayer->GetLastPaintedBackingCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the material the layer draws with, or null
UObject* USOLHudSymbolView::GetMaterial() const
{
    return mLayer.IsValid() ? mLayer->GetMaterial() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Drops the layer and forgets the build
void USOLHudSymbolView::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mLayer.Reset();
    mStamp.bBuilt = false;
    mIsBatchSized = false;
}

//////////////////////////////////////////////////////////////////////////
// Creates the retained shape layer (the view's capacities, not the HUD's: no shapes until the first build sizes the
// batch, the shared paint buffer) with the shared material, the HUD's engine fonts and the glow text
TSharedRef<SWidget> USOLHudSymbolView::RebuildWidget()
{
    using namespace SOL::Help;
    mLayer = SNew(SSOLHudShapeLayer).MaxShapes(0).MaxTexts(GLOSSARY_MAX_SAMPLE_LINES).MaxBackings(1)
        .PaintScratch(&HudSymbolViewPaintScratch());
    mLayer->SetRetainBatch(true);
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(this);
    UMaterialInstanceDynamic* material = solid != nullptr ? solid->GetHudShapeMaterial() : nullptr;
    if (material == nullptr && !GHudSymbolViewMaterialWarned)
    {
        GHudSymbolViewMaterialWarned = true;
        UE_LOG(LogSOL, Warning, TEXT("HudSymbolView %s: no shared HUD shape material; the glossary draws its backings "
            "and text only"), *GetName());
    }
    mLayer->SetMaterial(material);

    // The text lines use the engine fonts the flight HUD draws with (GEngine's fonts stay loaded)
    for (int32 index = 0; index < static_cast<int32>(ESOLHudFont::Count); ++index)
    {
        const ESOLHudFont font = static_cast<ESOLHudFont>(index);
        if (const UFont* engineFont = HudSymbolViewFont(font))
        {
            mLayer->SetFont(font, engineFont->GetLegacySlateFontInfo());
        }
    }
    mLayer->SetTextGlow(true);
    mStamp.bBuilt = false;
    mIsBatchSized = false;
    return mLayer.ToSharedRef();
}
