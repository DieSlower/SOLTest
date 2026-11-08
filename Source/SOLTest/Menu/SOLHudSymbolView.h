/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLGlossary.h"
#include "SOLConstants.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHudSymbols.h"

#include "Components/Widget.h"
#include "CoreMinimal.h"

#include "SOLHudSymbolView.generated.h"

class SSOLHudShapeLayer;
class UMaterialInterface;
struct FSOLHudShapeBatch;

// The scratch memory a symbol view builds its batch through (SDD 33 D8): a symbol's primitives and a card / frame
// output, about 11 KB. The Help page owns one and lends it to each view's UpdateBatch (the views rebuild one at a time
// on the game thread), so no view carries its own
struct FSOLHudSymbolScratch
{
    FSOLHudSymbolPrims Prims;
    FSOLHoloFrameOutput Frame;
};

/**
 * One HUD glossary row's picture on the Help page (SDD 33 section 8.3, part 33d-ii): a UMG wrapper of a small
 * SSOLHudShapeLayer in retain mode drawn with the one shared M_SOLHudShape material of USOLSolidMenuSubsystem. It shows
 * either a row's HUD symbols (several share a near-square grid of equal cells, each shrunk to fit its cell, never
 * enlarged; SOLGlossary::CellRect / SymbolFitScale) or its text sample (the '|' parts as glow-text lines in the HUD's
 * Medium font, centred, wrapped at the HUD's size before it shrinks; SOLGlossary::LayoutSample), over one dark HUD panel backing filling the view.
 * Symbols are at the HUD's scale for a 1080 px canvas in menu units (D5: GLOSSARY_SYMBOL_UI_SCALE x pixels per unit).
 * The batch is built by UpdateBatch (the page calls it from its tick for the shown tab) only when the view is first
 * laid out and again when its pixel size or the effects strength changed (SOLGlossary::ViewNeedsRebuild), never per
 * frame. Memory: the layer holds no shape buffer until the first build sizes the batch to the symbols (a dry build,
 * plus GLOSSARY_SHAPE_HEADROOM, capped at GLOSSARY_MAX_SHAPES), and every view's layer shares one paint vertex buffer.
 * Painting builds no batch and grows no buffer of its own, but it is not allocation-free: Slate's
 * FSlateDrawElement::MakeCustomVerts copies the vertex and index arrays into the draw element, so each painted view
 * allocates twice per frame (TECH_DEBT). The layer maps the batch through its own geometry every paint, so a scroll or
 * a move repaints correctly without a rebuild (SDD R6). The view clips to its bounds, so glow never spills out, and
 * takes no hit test.
 */
UCLASS()
class SOLTEST_API USOLHudSymbolView : public UWidget
{
    GENERATED_BODY()

public:

    // Clips to the view's bounds and takes no hit test
    USOLHudSymbolView();

    // Shows a row's symbols (count clamped to GLOSSARY_MAX_SYMBOLS_PER_ENTRY); a change forces the next rebuild
    void SetSymbols(const ESOLHudSymbol* symbols, int32 count);

    // Shows a text sample (its '|' parts as lines) of a GLOSSARY_ENTRIES row (the row picks the lines' colours,
    // SOLGlossary::SampleLineRole); a change forces the next rebuild
    void SetSample(const FString& sample, int32 entryIndex = INDEX_NONE);

    // (33d-iii) Returns a text sample's row box size in menu units (D44, SOLGlossary::SampleBoxSize): measured in the
    // HUD's Medium font as the layer draws it, the larger of the 1080 and 720 font scales; the square symbol box when
    // Slate is not up
    static FVector2f SampleBoxSize(const FString& sample);

    // (33d-iii) Sets the view's size in units before its first layout (its box less the recess padding), the size a
    // newly shown tab builds it at, and after a resize until the layout catches up; GLOSSARY_VIEW_SIZE square by
    // default
    void SetNominalSize(const FVector2f& sizeUnits);

    // Rebuilds the batch through the scratch when the view's pixel size or the strength changed since the last build
    // (or it was never built); returns whether it rebuilt. The pixel size is the view's laid-out size in units (before
    // its first layout: the nominal size) at layoutScale, the owning page's current layout scale (0: the view's own
    // last one). The page ticks within its own paint, before its views are arranged in that frame, so their cached
    // geometry is a frame old (zero on a newly shown tab, the old scale after a resize, never updated while a row is
    // scrolled out of view): built at the page's scale, every row of a newly shown tab paints on its first frame
    bool UpdateBatch(float strength, FSOLHudSymbolScratch& scratch, float layoutScale = 0.0f);

    // Returns the shapes of the last build (verification)
    int32 GetShapeCount() const { return mShapeCount; }

    // Returns the shapes the last paint drew (verification)
    int32 GetPaintedShapeCount() const;

    // Returns the shape capacity the batch holds (verification: 0 before the first build and for a sample)
    int32 GetShapeCapacity() const;

    // Returns the window-space point the last paint mapped the batch's origin to (verification: the view's place)
    FVector2f GetLastPaintOrigin() const;

    // Returns the text lines of the last build (verification)
    int32 GetTextCount() const;

    // Returns the text lines the last paint drew (verification)
    int32 GetPaintedTextCount() const;

    // Returns the backings the last paint drew (verification)
    int32 GetPaintedBackingCount() const;

    // Returns the shapes and primitives the last build dropped for want of room (verification: 0)
    int32 GetDroppedShapeCount() const { return mDroppedCount; }

    // Returns how many times the batch was built (verification: once, then only on a size or strength change)
    int32 GetBuildCount() const { return mBuildCount; }

    // Returns the pixel size of the last build ((0, 0) before the first)
    FIntPoint GetBuiltSizePx() const { return mStamp.SizePx; }

    // Returns the art area of the last build in the view's pixels (the view inset by GLOSSARY_SYMBOL_PADDING)
    FBox2f GetArtAreaPx() const { return mArtArea; }

    // Returns the bounds of the last build's sample lines in the view's pixels, as measured for the layout (an invalid
    // box for symbols or no line; verification: inside the art area, nothing clipped)
    FBox2f GetSampleBoundsPx() const { return mSampleBounds; }

    // Returns the smallest symbol scale of the last build, or the sample's scale (0 before a build or with nothing
    // drawn)
    float GetFitScale() const { return mFitScale; }

    // Returns the material the layer draws with, or null (verification: the subsystem's shared HUD shape material)
    UObject* GetMaterial() const;

    // Returns the number of symbols shown (0 for a sample)
    int32 GetSymbolCount() const { return mSymbolCount; }

    // Returns whether the view shows a text sample
    bool IsSample() const { return mIsSample; }

    // Drops the layer and forgets the build, so a rebuilt widget builds again
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:

    // Creates the retained shape layer with the shared material, the HUD fonts and the glow text
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:

    // Refills the layer: the backing, then the symbols' shapes or the sample's lines, at pxPerUnit pixels per unit
    void Rebuild(const FIntPoint& sizePx, float pxPerUnit, float strength, FSOLHudSymbolScratch& scratch);

    // Sizes the layer's batch to the symbols' shapes (a dry build into the shared batch) plus headroom, capped
    void SizeBatch(float pxPerUnit, const FSOLHudSymbolPalette& palette, FSOLHudSymbolScratch& scratch);

    // Adds the symbols to a batch, each fitted and centred in its grid cell of the art area
    void BuildSymbols(float pxPerUnit, const FSOLHudSymbolPalette& palette, FSOLHudSymbolScratch& scratch,
        FSOLHudShapeBatch& batch);

    // Adds the sample's lines, laid out in the art area
    void BuildSample(float pxPerUnit, const FSOLHudSymbolPalette& palette);

    TSharedPtr<SSOLHudShapeLayer> mLayer;
    ESOLHudSymbol mSymbols[SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY];
    int32 mSymbolCount = 0;
    FString mSample;
    int32 mSampleEntry = INDEX_NONE;                // The GLOSSARY_ENTRIES row the sample is of
    bool mIsSample = false;
    bool mIsBatchSized = false;                     // The batch was sized for the current symbols
    FSOLGlossaryViewStamp mStamp;
    FSOLGlossarySampleLayout mSampleLayout;         // The last build's lines (capacity kept)
    TArray<FText> mSampleTexts;                     // The lines' texts, made at build, never in paint
    FBox2f mArtArea = FBox2f(ForceInit);
    FBox2f mSampleBounds = FBox2f(ForceInit);       // The last build's sample lines as measured (view pixels)
    FVector2f mNominalSize = FVector2f(SOL::Help::GLOSSARY_VIEW_SIZE);  // Units, the size before the first layout
    bool mIsNominalPending = false;                 // A new nominal size the layout has not caught up with yet
    int32 mBuildCount = 0;
    int32 mShapeCount = 0;
    int32 mDroppedCount = 0;
    float mFitScale = 0.0f;
};
