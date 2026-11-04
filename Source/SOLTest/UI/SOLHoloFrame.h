/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloMenuMath.h"
#include "UI/SOLHudShapeBatch.h"

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class USOLHoloMenuSubsystem;

/**
 * One hologram menu frame (SDD 42 sections 4.3 and 6.3): a hit-test-invisible leaf widget a UMG menu places behind its
 * content. It owns the menu's open / close / hover anim state (FSOLHoloMenuAnim) and, in each paint, reads the
 * application time once (FPlatformTime::Seconds, SDD 4.4), advances and evaluates the anim at the live hologram menu
 * strength, builds the option (c) frame for its own geometry with SOLHoloFrameBuild (the faint tint backing, the
 * broken outline, the fill-faded groups, the sweep and the hover mark) and draws the shapes as one custom-vertex
 * element with the menu material, the same draw path as SSOLHudShapeLayer. The layout the menu sets is in the frame's
 * local Slate units; the frame turns it into pixels with the geometry's scale, and its UI scale follows the viewport
 * height like the HUD's (clamped to [1, HUD_MAX_UI_SCALE], without the HUD's Settings HUD scale: a UMG frame scales
 * like its UMG content). While the menu is Open and nothing changed but the time, the built frame is kept and only the
 * sweep line moves (the outline cache of the 42a review): the shapes are bit for bit what a fresh build gives, and at
 * an unchanged window placement only the sweep's four window-space vertices are rewritten. Every buffer is reserved
 * once in Construct: no heap allocation in project code per frame. No timers, no tick. sol.HudRenderer is not
 * followed (a UMG menu has no Canvas fallback); sol.HudComposite reaches the menu material through the subsystem.
 */
class SOLTEST_API SSOLHoloFrame : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SSOLHoloFrame) {}
    SLATE_END_ARGS()

    // Reserves the batch and paint buffers once (SOL::HoloMenu::FRAME_MAX_SHAPES quads)
    void Construct(const FArguments& args);

    // Sets the subsystem whose strength, material and view size the frame draws with
    void SetMenuSubsystem(USOLHoloMenuSubsystem* subsystem);

    // Sets the frame's layout in its local units: dividers, tab markers and the title step, whose TitleStepWidthPx is
    // the title text's local width alone (0: no step); the frame scales it to pixels and adds
    // FRAME_TITLE_STEP_PAD_PX x its own UI scale, which a UMG menu does not know. Rect, UiScale and the hover fields
    // are ignored: the rect is the widget's own geometry, the hover comes from SetHoveredItem
    void SetLayout(const FSOLHoloFrameSpec& localSpec);

    // Sets the hovered item (INDEX_NONE: none), its rect in local units and its mark; returns true when a hover cue is
    // due (SOLHoloMenu::ShouldPlayHoverCue: a new item while Open, rate limited; never when bAllowCue is false, for a
    // mark that moved without the menu's highlight changing)
    bool SetHoveredItem(int32 item, const FBox2f& localRect, ESOLHoloHoverMark mark, double nowS,
        bool bAllowCue = true);

    // Starts the open (or reverses a close); returns true when it left Closed or Closing (the open cue)
    bool BeginOpen(double nowS);

    // Starts the close from the current progress; returns true when a close started
    bool BeginClose(double nowS);

    // Drops the anim to Closed at once, with no close and no hover (a menu removed at teardown); no cue
    void ResetClosed();

    // Advances the anim to nowS (Opening -> Open, Closing -> Closed) and returns its phase
    ESOLHoloMenuPhase Advance(double nowS);

    // Returns the anim's look at nowS and the live strength (no state change)
    FSOLHoloMenuLook EvaluateLook(double nowS) const;

    // Returns the live hologram menu strength (HOLO_MENU_EFFECTS_DEFAULT without a subsystem)
    float GetStrength() const;

    // Returns the anim state
    const FSOLHoloMenuAnim& GetAnim() const { return mAnim; }

    // Returns the frame the last paint drew, in pixels relative to the widget's origin (verification)
    const FSOLHoloFrameOutput& GetLastOutput() const { return mOutput; }

    // Returns the pixel layout the last build used (verification: a fresh BuildFrame of it gives GetLastOutput)
    const FSOLHoloFrameSpec& GetLastPixelSpec() const { return mPixelSpec; }

    // Returns the look of the last paint (verification)
    const FSOLHoloMenuLook& GetLastLook() const { return mLastLook; }

    // Returns the strength of the last paint (verification)
    float GetLastStrength() const { return mLastStrength; }

    // Returns the shapes drawn by the last paint (verification)
    int32 GetLastPaintedShapeCount() const { return mLastPaintedShapeCount; }

    // Returns the backings drawn by the last paint (verification)
    int32 GetLastPaintedBackingCount() const { return mLastPaintedBackingCount; }

    // Returns how many paints ran (verification)
    int32 GetPaintCount() const { return mPaintCount; }

    // Returns how many paints reused the built frame (verification)
    int32 GetCacheHitCount() const { return mCacheHitCount; }

    // Returns the material the shapes are drawn with, or null (verification)
    UObject* GetMaterial() const { return mBrush.GetResourceObject(); }

    // Draws the backing and the frame's shapes (one custom-vertex element)
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // Takes no layout space of its own (it fills the slot it is placed in)
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    // What a built frame depends on besides the time: the layout, the pixel size, the scales, the strength and the look
    struct FSOLHoloFrameCacheKey
    {
        uint32 LayoutGeneration = 0;
        FVector2f SizePx = FVector2f::ZeroVector;
        float GeometryScale = 0.0f;
        float UiScale = 0.0f;
        float Strength = 0.0f;
        float FrameFraction = 0.0f;
        float FillAlpha = 0.0f;
        float HoverIntensity = 0.0f;
        float SweepIntensity = 0.0f;
        float BackdropAlpha = 0.0f;

        // Returns whether two keys are exactly equal
        bool Equals(const FSOLHoloFrameCacheKey& other) const;
    };

    // Drops the hover rect when the anim has no hovered item (BeginOpen and BeginClose clear the hover)
    void ClearHoverRect();

    // Points the brush at the subsystem's menu material when it changed (the material may arrive after Construct)
    void RefreshBrush() const;

    // Builds the frame for this paint, or reuses the last build and moves its sweep line; true when it reused
    bool BuildOrReuse(const FSOLHoloFrameCacheKey& key, const FSOLHoloMenuLook& look, float strength) const;

    // Moves the sweep quad of the window-space paint vertices to the sweep's current y
    void PatchSweepVertices(const FSlateRenderTransform& renderTransform, float inverseScale) const;

    TWeakObjectPtr<USOLHoloMenuSubsystem> mSubsystem;   // Strength, material, view size
    FSOLHoloFrameSpec mLocalSpec;                       // The menu's layout, local units
    FBox2f mHoverLocalRect = FBox2f(ForceInit);         // The hovered item, local units (invalid: none)
    ESOLHoloHoverMark mHoverMark = ESOLHoloHoverMark::RowPointer;
    uint32 mLayoutGeneration = 1;                       // Bumped whenever the layout or the hover rect changes
    mutable FSOLHoloMenuAnim mAnim;                     // Advanced in paint too (idempotent)
    mutable FSOLHoloFrameSpec mPixelSpec;               // The layout of the last build, pixels
    mutable FSOLHoloFrameOutput mOutput;                // The last build's shapes and backing (fixed capacity)
    mutable FSOLHoloFrameCacheKey mCacheKey;            // What the last build was made from
    mutable bool mIsCacheValid = false;                 // The last build may be reused (built while Open)
    mutable int32 mSweepShape = INDEX_NONE;             // Index of the Sweep shape in mOutput, or none
    mutable FSOLHudShapeBatch mBatch;                   // The frame's quads, pixels (reserved once)
    mutable FSlateBrush mBrush;                         // Brush wrapping the menu material
    mutable FSlateBrush mBackingBrush;                  // White rounded box tinted per backing layer
    mutable FSlateResourceHandle mResourceHandle;       // Renderer handle of the brush, fetched once per material
    mutable TArray<FSlateVertex> mPaintVertices;        // The batch in window space, reused every paint
    mutable FSlateRenderTransform mPaintTransform;      // The render transform mPaintVertices were made with
    mutable float mPaintInverseScale = 0.0f;            // ... and the inverse geometry scale
    mutable float mBatchSweepY = 0.0f;                  // The sweep's y when mBatch was filled
    mutable bool mIsPaintValid = false;                 // mPaintVertices hold mBatch for mPaintTransform
    mutable FSOLHoloMenuLook mLastLook;
    mutable float mLastStrength = 0.0f;
    mutable int32 mLastPaintedShapeCount = 0;
    mutable int32 mLastPaintedBackingCount = 0;
    mutable int32 mPaintCount = 0;
    mutable int32 mCacheHitCount = 0;
};
