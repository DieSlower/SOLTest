/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLKeyCaps.h"
#include "Menu/SOLKeyDiagram.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLSolidMenuMath.h"

#include "Components/Widget.h"
#include "CoreMinimal.h"
#include "Fonts/ShapedTextFwd.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include "SOLKeyboardDiagram.generated.h"

class UMaterialInstanceDynamic;

class FActiveTimerHandle;
class FSlateFontMeasure;

// The fonts the keyboard diagram draws with
struct FSOLKeyboardDiagramFonts
{
    FSlateFontInfo Key;                                 // A key's legend
    FSlateFontInfo Label;                               // A key's short action label
    FSlateFontInfo Badge;                               // The "+N" badge
    FSlateFontInfo Text;                                // Legend names, captions, the hover strip's actions
    FSlateFontInfo Strong;                              // The hover strip's keys
    FSlateFontInfo Cap;                                 // The hover strip's cap of the hovered key (the 33b cap font)
};

/**
 * The live keyboard diagram (SDD 33 section 7, 33c): one leaf paints the cached diagram model and panel: the area
 * legend, every layout key as a Small hull cap (the area tint fill and bar, the legend clipped to the key and the short
 * label, the "+N" badge unless it would overlap the legend (D35), the lock mark on fixed keys, the D32 modifier mark on
 * keys that start a chord, free keys dimmed), the mouse zones, the "Other keys" strip, the "Unbound" line and the hover
 * detail strip (D36: the hovered key as a 33b cap on its own row, then its lines at the full strip width, the action
 * text wrapped; the hint in the cap row with no hover). The texts and the legends' fits are shaped once per content,
 * unit or paint-scale change (and after a Slate invalidate-all), so a steady-state paint allocates nothing. No tick: an
 * active timer runs only while a hover ramp (HOVER_RAMP_S, instant at effects strength 0) is in progress. Mouse moves
 * report the local point to the owner (USOLKeyboardDiagram), which hovers the key and hands the lines back.
 */
class SOLTEST_API SSOLKeyboardDiagram : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SSOLKeyboardDiagram) {}
    SLATE_END_ARGS()

    // Makes the leaf tickless and hit-testable, keeps each layout key's main key and listens for Slate's invalidate-all
    void Construct(const FArguments& args);

    // Stops listening for Slate's invalidate-all
    virtual ~SSOLKeyboardDiagram() override;

    // Sets the brushes and the fonts; every text is measured and shaped again. capBrush: the hover strip's cap (the 33b
    // key cap, its LED); keyBrush: the diagram's keys and Other keys caps (D38: the same hull without the LED)
    void SetStyle(const FSlateBrush& capBrush, const FSlateBrush& keyBrush, const FSOLKeyboardDiagramFonts& fonts);

    // Sets the model (copied): the badge texts and the unbound line are rebuilt, the key texts reshaped
    void SetModel(const FSOLKeyDiagramModel& model);

    // Sets the panel (copied); the detail text is wrapped again on a width change
    void SetPanel(const FSOLDiagramPanel& panel);

    // Sets the hovered key (INDEX_NONE for none) and its strip lines (copied), starting the hover ramp at an effects
    // strength
    void SetHover(int32 key, const TArray<FSOLDiagramDetailLine>& lines, float effectsStrength);

    // Sets what a mouse move over the leaf (the local point, menu units) and a mouse leave call
    void SetHoverHandlers(TFunction<void(const FVector2f&)> onMove, TFunction<void()> onLeave);

    // Returns whether the last paint drew every layout key's hull with the keys' own hull material (D38: set and not
    // the strip cap's), every legend shaped at the paint scale and the detail strip (verification)
    bool WasPaintedWhole() const;

    // Returns the number of key hulls the last paint drew (layout keys, mirrors and mouse zones included)
    int32 GetPaintedKeyCount() const { return mPaintedKeys; }

    // Returns the hover strip's wrapped visual rows (the hint counts as one; verification: the strip shows the panel's
    // DetailLines of them)
    int32 GetDetailRowCount() const { return mDetailRows.Num(); }

    // Returns the visual rows a set of strip lines wraps into at a strip width (the same wrap the strip uses; each
    // line one row without a measure service). D36: the owner sizes the strip with it once per width and model
    int32 CountDetailRows(const TArray<FSOLDiagramDetailLine>& lines, float stripWidth) const;

    // Returns whether the leaf can measure text (Slate is up and the style is set)
    bool CanMeasure() const { return GetMeasure() != nullptr; }

    // Returns the hover strip cap's drawn width (0 with no cap; verification)
    float GetDetailCapWidth() const { return mDetailCapWidth; }

    // Returns the number of key legends the last paint drew wider than their key's content (verification: 0, D35
    // clips them)
    int32 GetWideLegendCount() const { return mPaintedWideLegends; }

    // Returns the "+N" badges the last paint hid because they would overlap the legend (D35; verification)
    int32 GetHiddenBadgeCount() const { return mPaintedHiddenBadges; }

    // Returns whether a layout key's legend is drawn clipped (D35; false before the first paint; verification: D38
    // wants none)
    bool IsLegendClipped(int32 key) const { return mLegendFits.IsValidIndex(key) && mLegendFits[key].bClipped; }

    // Returns whether a layout key's legend is drawn as its D38 abbreviation (false before the first paint)
    bool IsLegendAbbreviated(int32 key) const
    {
        return mLegendAbbreviated.IsValidIndex(key) && mLegendAbbreviated[key];
    }

    // Returns whether a layout key's "+N" badge is hidden because it would overlap the legend (D35; false before the
    // first paint or for a key without a badge)
    bool IsBadgeHidden(int32 key) const
    {
        return mLegendFits.IsValidIndex(key) && !mLegendFits[key].bBadge && mShapedBadges.IsValidIndex(key)
            && mShapedBadges[key].IsValid();
    }

    // Paints the panel
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // The panel's laid-out size (its width and Height)
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

    // Reports the local point to the owner
    virtual FReply OnMouseMove(const FGeometry& myGeometry, const FPointerEvent& mouseEvent) override;

    // Reports the leave to the owner
    virtual void OnMouseLeave(const FPointerEvent& mouseEvent) override;

private:

    // One visual line of the hover strip: a detail line's first line (swatch and keys) or a wrapped continuation
    struct FDetailRow
    {
        int32 Line = INDEX_NONE;                        // Index into mDetailLines (INDEX_NONE: the hint)
        bool bFirst = true;
        FString Text;                                   // This visual line's part of the action (or the hint)
    };

    // Returns the font measure service, or null without Slate or before the style is set
    const FSlateFontMeasure* GetMeasure() const;

    // Rebuilds the badge texts and the unbound line from the model
    void BuildModelTexts();

    // Wraps the hover strip's lines (or the hint) in the strip width at scale 1
    void WrapDetail();

    // Slate invalidated every widget (a font cache flush unloads the faces of the kept shaped texts): reshape
    void HandleInvalidateAll(bool bClearResourcesImmediately);

    // Shapes the key, legend and strip texts at a paint scale and fits each key's legend and badge (D35)
    void ReshapeStatic(float scale) const;

    // Returns a layout key's hull box in local units at rest (no hover lift; empty when the unit leaves no room)
    FBox2f KeyBox(int32 index) const;

    // Shapes the hover strip's texts at a paint scale
    void ReshapeDetail(float scale) const;

    // Returns a key's hover look now (the ramp of the gaining or the losing key; the resting look otherwise)
    FSOLSolidButtonLook LookOf(int32 key, double nowS) const;

    // Returns whether a hover ramp is still in progress at a time
    bool IsRamping(double nowS) const;

    // Keeps repainting while the hover ramp runs; stops when it settles
    EActiveTimerReturnType HandleRampTimer(double currentTime, float deltaTime);

    // Paints one layout key: its hull on layerId, its tint and bar above, then its texts and marks
    void PaintKey(int32 index, const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
        const FLinearColor& styleTint, ESlateDrawEffect effects, double nowS) const;

    // Paints the area legend, the "Other keys" strip and the "Unbound" line
    void PaintSections(const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
        const FLinearColor& styleTint, ESlateDrawEffect effects) const;

    // Paints the hover strip's cap of the hovered key (a 33b key cap: the Small hull, the mouse glyph or the legend)
    void PaintDetailCap(const FVector2f& topLeft, const FGeometry& geometry, FSlateWindowElementList& out,
        int32 layerId, const FLinearColor& styleTint, ESlateDrawEffect effects) const;

    // Paints the hover strip
    void PaintDetail(const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
        const FLinearColor& styleTint, ESlateDrawEffect effects) const;

    // Paints a shaped text at a local x, vertically centred in a line; returns its width (local units)
    float PaintText(const FShapedGlyphSequencePtr& text, float x, float lineTop, float lineHeight,
        const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId, const FLinearColor& color,
        ESlateDrawEffect effects) const;

    // Draws a filled box in local units
    void PaintBox(const FBox2f& box, const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
        const FLinearColor& color, ESlateDrawEffect effects) const;

    // Draws a line of a thickness between two local points (a rotated box)
    void PaintLine(const FVector2f& from, const FVector2f& to, float thickness, const FGeometry& geometry,
        FSlateWindowElementList& out, int32 layerId, const FLinearColor& color, ESlateDrawEffect effects) const;

    FSOLKeyDiagramModel mModel;
    FSOLDiagramPanel mPanel;
    TArray<int32> mMainOf;                          // Each layout key's main key (itself unless a mirror)
    TArray<FString> mBadgeTexts;                    // Each layout key's "+N" ("" without a badge)
    TArray<FString> mOverflowLabels;                // Each Other key's shown name
    FString mUnboundText;                           // The unbound actions' names
    TArray<FSOLDiagramDetailLine> mDetailLines;
    TArray<FDetailRow> mDetailRows;
    float mDetailKeysWidth = 0.0f;                  // The keys column (scale 1, local units)
    FSOLKeyCap mDetailCap;                          // The hovered key's cap at the strip's left (SDD 7: "the key as a
                                                    // cap row"); rebuilt on a hover change only
    float mDetailCapWidth = 0.0f;                   // Its drawn width (0: no cap, the hint shows)
    int32 mDetailCapKey = INDEX_NONE;               // The layout key the cap was built for
    float mWrappedWidth = -1.0f;                    // The strip width the rows were wrapped in (-1: stale)
    FSlateBrush mCapBrush;                          // The hover strip's cap: the 33b key cap's Small hull, its LED
    FSlateBrush mKeyBrush;                          // D38: the keys and Other keys caps: the Small hull without the LED
    FSlateBrush mFlatBrush;                         // A flat white box: tints, bars, swatches and marks
    FSOLKeyboardDiagramFonts mFonts;
    bool mHasStyle = false;
    FDelegateHandle mInvalidateAllHandle;
    TFunction<void(const FVector2f&)> mOnMove;
    TFunction<void()> mOnLeave;

    // The hover ramp: the gaining key and the losing key, each from its look at the change
    int32 mHoveredKey = INDEX_NONE;
    int32 mLeavingKey = INDEX_NONE;
    FSOLSolidButtonLook mHoveredFrom;
    FSOLSolidButtonLook mLeavingFrom;
    double mHoverChangeS = -1.0e9;
    float mStrength = 1.0f;
    TSharedPtr<FActiveTimerHandle> mRampTimer;

    // The shaped texts, kept between paints
    mutable TArray<FShapedGlyphSequencePtr> mShapedLegends;     // Per layout key: the legend, or its longest prefix
                                                                // that fits the key when D35 clips it
    mutable TArray<FSOLKeyTextFit> mLegendFits;                 // Per layout key: FitKeyText's result, LegendX from
                                                                // the hull's left edge and LegendWidth what is drawn
    mutable TArray<bool> mLegendAbbreviated;                    // Per layout key: the D38 abbreviation is drawn
    mutable TArray<FShapedGlyphSequencePtr> mShapedLabels;      // Per layout key
    mutable TArray<FShapedGlyphSequencePtr> mShapedBadges;      // Per layout key
    mutable TArray<FShapedGlyphSequencePtr> mShapedAreas;       // Per area (the legend)
    mutable TArray<FShapedGlyphSequencePtr> mShapedOverflow;    // Per Other key: its name
    mutable TArray<FShapedGlyphSequencePtr> mShapedOverflowLabels; // ... and its short label
    mutable FShapedGlyphSequencePtr mShapedOtherCaption;
    mutable FShapedGlyphSequencePtr mShapedUnboundCaption;
    mutable FShapedGlyphSequencePtr mShapedUnbound;
    mutable TArray<FShapedGlyphSequencePtr> mShapedDetailKeys;  // Per detail row (null on continuations)
    mutable TArray<FShapedGlyphSequencePtr> mShapedDetailText;  // Per detail row
    mutable FShapedGlyphSequencePtr mShapedDetailCap;           // The strip cap's drawn legend (null: a mouse button)
    mutable bool mPaintedDetailCap = false;
    mutable float mShapedScale = -1.0f;             // The scale the static texts were shaped at (-1: stale)
    mutable float mShapedDetailScale = -1.0f;       // ... and the strip's
    mutable float mPaintedScale = 0.0f;
    mutable int32 mPaintedKeys = 0;
    mutable int32 mPaintedLegends = 0;
    mutable int32 mPaintedDetailRows = 0;
    mutable int32 mPaintedWideLegends = 0;          // Legends wider than their key's content (verification)
    mutable int32 mPaintedHiddenBadges = 0;         // Badges D35 hid (verification)
};

/**
 * The UMG wrapper of SSOLKeyboardDiagram (SDD 33 section 7, 33c). It keeps the live keys, the diagram model, the layout
 * width and panel, the hovered key and its detail lines itself, so the state is set and read without Slate (headless
 * tests) and handed to the leaf when it is built. The owner binds USOLSettingsSubsystem::OnKeyBindingsChanged and calls
 * SetKeys with the resolved live keys.
 */
UCLASS()
class SOLTEST_API USOLKeyboardDiagram : public UWidget
{
    GENERATED_BODY()

public:

    // Lays the empty panel out at width 0
    USOLKeyboardDiagram();

    // Rebuilds the model (SOLKeyDiagram::Build, CONTROL_ROWS) when the keys differ from the last ones set; returns
    // whether it rebuilt. A hovered key stays hovered (its lines rebuilt); the panel is laid out again
    bool SetKeys(const TArray<FString>& liveKeys);

    // Returns the diagram model
    const FSOLKeyDiagramModel& GetModel() const { return mModel; }

    // Returns the number of the model's assignments
    int32 GetAssignmentCount() const { return mModel.Assignments.Num(); }

    // Sets the layout width (menu units) the panel is laid out in (LayoutPanel with the model's Overflow / Unbound
    // shown); a change within DIAGRAM_WIDTH_TOLERANCE keeps the panel. Not a per-frame call: the owner calls it when
    // its width changes (a relayout re-wraps every key's strip lines)
    void SetLayoutWidth(float width);

    // Returns the layout width set
    float GetLayoutWidth() const { return mLayoutWidth; }

    // Returns the laid-out panel
    const FSOLDiagramPanel& GetPanel() const { return mPanel; }

    // Hovers the key under a local point (menu units, through the panel's scale and SOLKeyboardLayout::KeyAt); a point
    // off every key clears the hover. Returns the hovered key or INDEX_NONE
    int32 HoverAt(const FVector2f& localPoint);

    // Hovers a key (index into the model's keys; a mirror key hovers its main key); INDEX_NONE or out of range clears
    void SetHoveredKey(int32 key);

    // Returns the hovered key, or INDEX_NONE
    int32 GetHoveredKey() const { return mHoveredKey; }

    // Returns the hovered key's strip lines (DetailAssignments + DetailLine); empty with no hover
    const TArray<FSOLDiagramDetailLine>& GetDetailLines() const { return mDetailLines; }

    // Returns whether the leaf's last paint drew the whole diagram (false before the leaf exists or paints;
    // verification)
    bool WasPaintedWhole() const;

    // Returns the number of keys the leaf's last paint drew (0 before the leaf exists or paints; verification)
    int32 GetPaintedKeyCount() const;

    // Returns the leaf's wrapped hover strip rows (0 before the leaf exists; verification)
    int32 GetDetailRowCount() const;

    // Returns the leaf's hover strip cap width (0 before the leaf exists or with no cap; verification)
    float GetDetailCapWidth() const;

    // Returns the leaf's legends wider than their key in the last paint (0 before the leaf exists; verification)
    int32 GetWideLegendCount() const;

    // Returns whether a layout key's legend is drawn clipped (false before the leaf paints; verification, D38: none)
    bool IsLegendClipped(int32 key) const;

    // Returns whether a layout key's legend is drawn as its D38 abbreviation (false before the leaf paints)
    bool IsLegendAbbreviated(int32 key) const;

    // Returns whether a layout key's "+N" badge is hidden by D35 (false before the leaf paints)
    bool IsBadgeHidden(int32 key) const;

    // D36: returns the line count the hover strip is sized for, computed once per width and model (never per hover):
    // headless (no leaf, or one that cannot measure) max(1, SOLKeyDiagram::MaxDetailCount(model)), with the leaf the
    // busiest key's wrapped row count at the strip's width (at least the headless count); both clamped to
    // DIAGRAM_DETAIL_LINES. GetPanel().DetailLines equals it
    int32 GetDetailLineBudget() const { return mDetailLineBudget; }

    // D35: returns the "+N" badges the leaf's last paint hid (0 before the leaf exists; verification)
    int32 GetPaintedHiddenBadges() const;

    // D38: returns the keys' LED-less Small hull material (null before the leaf is built with the solid menu
    // subsystem; verification)
    const UMaterialInstanceDynamic* GetKeyMaterial() const;

    // Drops the leaf
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:

    // Builds the leaf with the solid kit's Small hull brush and fonts and hands it the state
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:

    // Sizes the hover strip (the budget) and lays the panel out in the layout width with the model's Other keys and
    // Unbound sections
    void LayoutPanel();

    // Returns the hover strip's line budget for the model and the layout width (see GetDetailLineBudget)
    int32 ComputeDetailLineBudget();

    // Rebuilds the hovered key's strip lines and hands the hover to the leaf
    void RefreshHover();

    // Returns the live effects strength (HUD_EFFECTS_DEFAULT without the solid menu subsystem)
    float GetEffectsStrength() const;

    TSharedPtr<SSOLKeyboardDiagram> mDiagram;

    // D38: the Small hull material of the diagram's keys with the LED off (LedX < 0), made once with the leaf; the
    // solid menu subsystem pushes the strength and DPI scale to it while it lives
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> KeyMaterial;

    TArray<FString> mLiveKeys;                      // The keys of the last SetKeys
    FSOLKeyDiagramModel mModel;
    FSOLDiagramPanel mPanel;
    TArray<int32> mDetailAssignments;               // DetailAssignments scratch (the hovered key; the budget's keys)
    TArray<FSOLDiagramDetailLine> mDetailLines;
    TArray<FSOLDiagramDetailLine> mBudgetLines;     // ComputeDetailLineBudget's scratch, one key's lines at a time
    int32 mDetailLineBudget = 1;
    float mLayoutWidth = 0.0f;
    int32 mHoveredKey = INDEX_NONE;
    bool mHasKeys = false;
};
