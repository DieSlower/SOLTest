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
class USOLSolidMenuSubsystem;

class FActiveTimerHandle;
class FSlateFontMeasure;

// The fonts the keyboard diagram draws with
struct FSOLKeyboardDiagramFonts
{
    FSlateFontInfo Key;                                 // A key's legend
    FSlateFontInfo Label;                               // A key's short action label
    FSlateFontInfo Badge;                               // The "+N" badge
    FSlateFontInfo Text;                                // Legend names, captions, the hint, the popup's actions
    FSlateFontInfo Strong;                              // The hover popup's keys
    FSlateFontInfo Cap;                                 // The hover popup's cap of the hovered key (the 33b cap font)
};

/**
 * The live keyboard diagram (SDD 33 section 7, 33c): one leaf paints the cached diagram model and panel: the area
 * legend, every layout key as a Small hull cap (the area tint fill and bar, the legend clipped to the key and the short
 * label, the "+N" badge unless it would overlap the legend (D35), the lock mark on fixed keys, the D32 modifier mark on
 * keys that start a chord, free keys dimmed), the mouse zones, the "Other keys" strip, the "Unbound" line and the
 * "Hover a key" hint line. D45: the hovered key's detail (the key as a 33b cap on its own row, then its lines, the
 * action text wrapped in SOLKeyboardLayout::PopupWrapWidth) is a popup the page's SSOLKeyboardDiagramPopupLayer paints
 * over the page (PaintPopup: placed by SOLKeyboardLayout::PlacePopup by the hovered key, inside the layer). The texts
 * and the legends' fits are shaped once per content, hover, unit or paint-scale change (and after a Slate
 * invalidate-all), so a steady-state paint allocates nothing. No tick: an active timer runs only while a hover ramp
 * (HOVER_RAMP_S, instant at effects strength 0) is in progress. Mouse moves report the local point to the owner
 * (USOLKeyboardDiagram), which hovers the key and hands the lines back.
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

    // Sets the brushes and the fonts; every text is measured and shaped again. capBrush: the hover popup's cap (the
    // 33b key cap, its LED); keyBrush: the diagram's keys and Other keys caps (D38: the same hull without the LED);
    // popupBrush: the hover popup's hull panel (D45)
    void SetStyle(const FSlateBrush& capBrush, const FSlateBrush& keyBrush, const FSlateBrush& popupBrush,
        const FSOLKeyboardDiagramFonts& fonts);

    // Sets the model (copied): the badge texts and the unbound line are rebuilt, the key texts reshaped
    void SetModel(const FSOLKeyDiagramModel& model);

    // Sets the panel (copied)
    void SetPanel(const FSOLDiagramPanel& panel);

    // Sets the hovered key (INDEX_NONE for none) and its popup lines (copied, wrapped once here), starting the hover
    // ramp at an effects strength
    void SetHover(int32 key, const TArray<FSOLDiagramDetailLine>& lines, float effectsStrength);

    // D45: sets the layout cap the popup is placed by: the cap under the cursor, a mirror cap (the numpad Enter) of
    // the hovered key included (INDEX_NONE or a cap that is not the hovered key's: the hovered key's own cap)
    void SetHoverAnchor(int32 cap)
    {
        mHoverAnchor = cap;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    // Sets what a mouse move over the leaf (the local point, menu units) and a mouse leave call
    void SetHoverHandlers(TFunction<void(const FVector2f&)> onMove, TFunction<void()> onLeave);

    // Sets the subsystem whose menu clock (USOLSolidMenuSubsystem::GetClockS) the hover ramp runs on, as every solid
    // widget's animation does (weakly held; the real application time without one)
    void SetClockSource(const USOLSolidMenuSubsystem* source);

    // Returns whether a hover ramp is still in progress at a time (a time before the last hover change counts as
    // settled; public for verification)
    bool IsRamping(double nowS) const;

    // Returns the menu clock now (the clock source's, else the real application time; public for verification)
    double GetNowS() const;

    // Returns whether the last paint drew every layout key's hull with the keys' own hull material (D38: set and not
    // the popup cap's), every legend shaped at the paint scale and the hint (verification)
    bool WasPaintedWhole() const;

    // D45: paints the hovered key's popup into a layer's geometry over the page (SSOLKeyboardDiagramPopupLayer, after
    // this leaf in the same frame): nothing with no hover or when this leaf did not paint this frame (another tab);
    // else the hull panel sized by SOLKeyboardLayout::PopupSize, placed by PlacePopup by the key (both in the layer's
    // space) inside the layer less DIAGRAM_POPUP_EDGE_PAD, the cap row and the lines on it. Returns the layer after
    // the last one used
    int32 PaintPopup(const FGeometry& layerGeometry, FSlateWindowElementList& out, int32 layerId,
        const FWidgetStyle& widgetStyle, bool bParentEnabled) const;

    // D45: returns whether the popup was painted in the frame of this leaf's last paint (verification)
    bool WasPopupPainted() const { return mPaintFrame != 0 && mPopupFrame == mPaintFrame; }

    // D45: returns the last painted popup's rect in the popup layer's local space (verification)
    const FBox2f& GetPopupRect() const { return mPopupRect; }

    // D45: returns the hovered key's box the last popup was placed by, in the popup layer's space (verification)
    const FBox2f& GetPopupKeyRect() const { return mPopupKeyRect; }

    // D45: returns the bounds the last popup was kept in, in the popup layer's space (verification)
    const FBox2f& GetPopupBounds() const { return mPopupBounds; }

    // D45: returns the side of the key the last popup took (verification)
    ESOLPopupSide GetPopupSide() const { return mPopupSide; }

    // Returns the number of key hulls the last paint drew (layout keys, mirrors and mouse zones included)
    int32 GetPaintedKeyCount() const { return mPaintedKeys; }

    // Returns the hover popup's wrapped visual rows (0 with no hover; verification)
    int32 GetDetailRowCount() const { return mDetailRows.Num(); }

    // Returns the hover popup cap's drawn width (0 with no cap; verification)
    float GetDetailCapWidth() const { return mDetailCapWidth; }

    // Returns the hover popup's rows drawn by its last paint (verification)
    int32 GetPaintedDetailRows() const { return mPaintedDetailRows; }

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

    // One visual line of the hover popup: a detail line's first line (swatch and keys) or a wrapped continuation
    struct FDetailRow
    {
        int32 Line = INDEX_NONE;                        // Index into mDetailLines
        bool bFirst = true;
        FString Text;                                   // This visual line's part of the action
    };

    // Returns the font measure service, or null without Slate or before the style is set
    const FSlateFontMeasure* GetMeasure() const;

    // Rebuilds the badge texts and the unbound line from the model
    void BuildModelTexts();

    // Wraps the hover popup's lines in SOLKeyboardLayout::PopupWrapWidth, measured at a paint scale, and measures its
    // content width (const: the popup paint re-wraps at its own scale)
    void WrapDetail(float scale) const;

    // Slate invalidated every widget (a font cache flush unloads the faces of the kept shaped texts): reshape
    void HandleInvalidateAll(bool bClearResourcesImmediately);

    // Shapes the key, legend, section and hint texts at a paint scale and fits each key's legend and badge (D35)
    void ReshapeStatic(float scale) const;

    // Returns a layout key's hull box in local units at rest (no hover lift; empty when the unit leaves no room)
    FBox2f KeyBox(int32 index) const;

    // Shapes the hover popup's texts at a paint scale
    void ReshapeDetail(float scale) const;

    // Returns a key's hover look now (the ramp of the gaining or the losing key; the resting look otherwise)
    FSOLSolidButtonLook LookOf(int32 key, double nowS) const;

    // Keeps repainting while the hover ramp runs; stops when it settles
    EActiveTimerReturnType HandleRampTimer(double currentTime, float deltaTime);

    // Paints one layout key: its hull on layerId, its tint and bar above, then its texts and marks
    void PaintKey(int32 index, const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
        const FLinearColor& styleTint, ESlateDrawEffect effects, double nowS) const;

    // Paints the area legend, the hint, the "Other keys" strip and the "Unbound" line
    void PaintSections(const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
        const FLinearColor& styleTint, ESlateDrawEffect effects) const;

    // Paints the hover popup's cap of the hovered key with the shared cap painter (SOLKeyCapPaint::PaintCap)
    void PaintDetailCap(const FVector2f& topLeft, const FGeometry& geometry, FSlateWindowElementList& out,
        int32 layerId, const FLinearColor& styleTint, ESlateDrawEffect effects) const;

    // Paints the hover popup's content (the cap row, then the rows) in a content box of a geometry
    void PaintDetail(const FBox2f& content, const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
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
    // The popup's wrap, mutable: the popup paint re-wraps at its own scale (on a hover or scale change only)
    mutable TArray<FDetailRow> mDetailRows;
    mutable float mDetailKeysWidth = 0.0f;          // The keys column (local units, at mWrapScale)
    mutable float mPopupContentWidth = 0.0f;        // D45: the popup's content width (PopupContentWidth)
    mutable float mWrapScale = 1.0f;                // D45: the paint scale the popup was wrapped and measured at
    mutable FSOLKeyCap mDetailCap;                  // The hovered key's cap at the popup's top (SDD 7: "the key as a
                                                    // cap row"); rebuilt on a hover change only
    mutable float mDetailCapWidth = 0.0f;           // Its drawn width (0: no cap)
    mutable int32 mDetailCapKey = INDEX_NONE;       // The layout key the cap was built for
    FSlateBrush mCapBrush;                          // The hover popup's cap: the 33b key cap's Small hull, its LED
    FSlateBrush mKeyBrush;                          // D38: the keys and Other keys caps: the Small hull without the LED
    FSlateBrush mPopupBrush;                        // D45: the hover popup's hull panel
    FSlateBrush mFlatBrush;                         // A flat white box: tints, bars, swatches and marks
    FSOLKeyboardDiagramFonts mFonts;
    bool mHasStyle = false;
    FDelegateHandle mInvalidateAllHandle;
    TFunction<void(const FVector2f&)> mOnMove;
    TFunction<void()> mOnLeave;

    // The hover ramp: the gaining key and the losing key, each from its look at the change
    int32 mHoveredKey = INDEX_NONE;
    int32 mHoverAnchor = INDEX_NONE;                // D45: the cap the popup is placed by (a mirror of the hovered key)
    int32 mLeavingKey = INDEX_NONE;
    FSOLSolidButtonLook mHoveredFrom;
    FSOLSolidButtonLook mLeavingFrom;
    double mHoverChangeS = -1.0e9;                  // On the menu clock (GetNowS)
    float mStrength = 1.0f;
    TWeakObjectPtr<const USOLSolidMenuSubsystem> mClockSource;  // The menu clock's owner (SetClockSource)
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
    mutable FShapedGlyphSequencePtr mShapedHint;                // D45: the "Hover a key" hint
    mutable TArray<FShapedGlyphSequencePtr> mShapedDetailKeys;  // Per detail row (null on continuations)
    mutable TArray<FShapedGlyphSequencePtr> mShapedDetailText;  // Per detail row
    mutable FShapedGlyphSequencePtr mShapedDetailCap;           // The popup cap's drawn legend (null: a mouse button)
    mutable bool mPaintedDetailCap = false;
    mutable bool mPaintedHint = false;
    mutable float mShapedScale = -1.0f;             // The scale the static texts were shaped at (-1: stale)
    mutable float mShapedDetailScale = -1.0f;       // ... and the popup's
    mutable float mShapedKeysWidth = 0.0f;          // D45: the popup's keys column as shaped (local units)
    mutable float mShapedContentWidth = 0.0f;       // ... and its content width (PopupContentWidth of the shaped texts)
    mutable FGeometry mPaintGeometry;               // D45: the leaf's geometry at its last paint (the popup's key box)
    mutable uint64 mPaintFrame = 0;                 // GFrameCounter at the last paint (0: never)
    mutable uint64 mPopupFrame = 0;                 // GFrameCounter at the last popup paint (0: never)
    mutable bool mPopupPending = false;             // Set by this leaf's paint, taken by the next popup paint
    mutable FBox2f mPopupRect = FBox2f(ForceInit);  // The last popup's rect, its key and its bounds (layer local)
    mutable FBox2f mPopupKeyRect = FBox2f(ForceInit);
    mutable FBox2f mPopupBounds = FBox2f(ForceInit);
    mutable ESOLPopupSide mPopupSide = ESOLPopupSide::Below;
    mutable float mPaintedScale = 0.0f;
    mutable int32 mPaintedKeys = 0;
    mutable int32 mPaintedLegends = 0;
    mutable int32 mPaintedDetailRows = 0;
    mutable int32 mPaintedWideLegends = 0;          // Legends wider than their key's content (verification)
    mutable int32 mPaintedHiddenBadges = 0;         // Badges D35 hid (verification)
};

class USOLKeyboardDiagram;

/**
 * D45: the keyboard diagram's hover popup layer: a hit-test-invisible, tickless leaf over the whole page (the last
 * child of an overlay above the page's content), so the popup is drawn over the page and is never clipped by the
 * Keyboard tab's scroll box. Each paint asks the diagram's leaf to paint its popup in this layer's geometry
 * (SSOLKeyboardDiagram::PaintPopup); it never takes hover from the keys and never changes the page's layout (desired
 * size zero).
 */
class SOLTEST_API SSOLKeyboardDiagramPopupLayer : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SSOLKeyboardDiagramPopupLayer) {}
    SLATE_END_ARGS()

    // Makes the layer tickless and hit-test invisible
    void Construct(const FArguments& args);

    // Sets the diagram whose popup the layer paints (weak: either may outlive the other)
    void SetDiagram(USOLKeyboardDiagram* diagram);

    // Paints the diagram's popup (nothing without one)
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // Zero: the layer takes no room
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    TWeakObjectPtr<USOLKeyboardDiagram> mDiagram;
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
    // its width changes (a relayout refits every key's legend when the key unit changes; the popup's wrap is fixed)
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

    // D45: returns the layout cap the popup is placed by (the cap given to SetHoveredKey: a mirror cap of the hovered
    // key included), or INDEX_NONE
    int32 GetHoverAnchor() const { return mHoverAnchor; }

    // Returns the hovered key's popup lines (DetailAssignments + DetailLine); empty with no hover
    const TArray<FSOLDiagramDetailLine>& GetDetailLines() const { return mDetailLines; }

    // Returns whether the leaf's last paint drew the whole diagram (false before the leaf exists or paints;
    // verification)
    bool WasPaintedWhole() const;

    // Returns the number of keys the leaf's last paint drew (0 before the leaf exists or paints; verification)
    int32 GetPaintedKeyCount() const;

    // Returns the leaf's wrapped hover popup rows (0 before the leaf exists; verification)
    int32 GetDetailRowCount() const;

    // Returns the leaf's hover popup cap width (0 before the leaf exists or with no cap; verification)
    float GetDetailCapWidth() const;

    // Returns the leaf (null before it is built; the popup layer and verification)
    const TSharedPtr<SSOLKeyboardDiagram>& GetLeaf() const { return mDiagram; }

    // Returns the leaf's legends wider than their key in the last paint (0 before the leaf exists; verification)
    int32 GetWideLegendCount() const;

    // Returns whether a layout key's legend is drawn clipped (false before the leaf paints; verification, D38: none)
    bool IsLegendClipped(int32 key) const;

    // Returns whether a layout key's legend is drawn as its D38 abbreviation (false before the leaf paints)
    bool IsLegendAbbreviated(int32 key) const;

    // Returns whether a layout key's "+N" badge is hidden by D35 (false before the leaf paints)
    bool IsBadgeHidden(int32 key) const;

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

    // Lays the panel out in the layout width with the model's Other keys and Unbound sections
    void LayoutPanel();

    // Rebuilds the hovered key's popup lines and hands the hover to the leaf
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
    TArray<int32> mDetailAssignments;               // DetailAssignments scratch (the hovered key)
    TArray<FSOLDiagramDetailLine> mDetailLines;
    float mLayoutWidth = 0.0f;
    int32 mHoveredKey = INDEX_NONE;
    int32 mHoverAnchor = INDEX_NONE;                // D45: the cap the popup is placed by
    bool mHasKeys = false;
};

/**
 * D45: the UMG wrapper of SSOLKeyboardDiagramPopupLayer: the page puts it over its content (an overlay's last child,
 * hit-test invisible) and gives it the diagram whose hover popup it paints.
 */
UCLASS()
class SOLTEST_API USOLKeyboardDiagramPopup : public UWidget
{
    GENERATED_BODY()

public:

    // Makes the widget hit-test invisible
    USOLKeyboardDiagramPopup();

    // Sets the diagram whose popup the layer paints
    void SetDiagram(USOLKeyboardDiagram* diagram);

    // Drops the layer
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:

    // Builds the layer and hands it the diagram
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:

    UPROPERTY(Transient)
    TObjectPtr<USOLKeyboardDiagram> Diagram;

    TSharedPtr<SSOLKeyboardDiagramPopupLayer> mLayer;
};
