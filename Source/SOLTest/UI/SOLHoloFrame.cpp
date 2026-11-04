/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloFrame.h"

#include "SOLConstants.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "UI/SOLHudShapeLayer.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
    constexpr int32 HOLOFRAME_LAYERS_USED = 2;          // Backing, shapes

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two boxes are exactly equal (corners and validity)
    bool HoloFrameBoxEquals(const FBox2f& a, const FBox2f& b)
    {
        return a.Min == b.Min && a.Max == b.Max && a.bIsValid == b.bIsValid;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two layouts are exactly equal in the fields SetLayout keeps (rect, scale and hover ignored)
    bool HoloFrameLayoutEquals(const FSOLHoloFrameSpec& a, const FSOLHoloFrameSpec& b)
    {
        if (a.TitleStepWidthPx != b.TitleStepWidthPx || a.bColumnDivider != b.bColumnDivider
            || a.DividerTop != b.DividerTop || a.DividerBottomY != b.DividerBottomY || a.OpenTopY != b.OpenTopY
            || a.OpenBottomY != b.OpenBottomY || a.bBrokenDivider != b.bBrokenDivider
            || a.BrokenStart != b.BrokenStart || a.BrokenEndX != b.BrokenEndX
            || a.BrokenStepAtPx != b.BrokenStepAtPx || a.BrokenStepPx != b.BrokenStepPx
            || a.TabCount != b.TabCount || a.SelectedTab != b.SelectedTab || a.TabDividerX != b.TabDividerX)
        {
            return false;
        }
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            if (!HoloFrameBoxEquals(a.TabRects[tab], b.TabRects[tab]))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a box scaled by k (an invalid box stays invalid)
    FBox2f HoloFrameScaleBox(const FBox2f& box, const float k)
    {
        if (!box.bIsValid)
        {
            return FBox2f(ForceInit);
        }
        return FBox2f(box.Min * k, box.Max * k);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether two keys are exactly equal
bool SSOLHoloFrame::FSOLHoloFrameCacheKey::Equals(const FSOLHoloFrameCacheKey& other) const
{
    return LayoutGeneration == other.LayoutGeneration && SizePx == other.SizePx
        && GeometryScale == other.GeometryScale && UiScale == other.UiScale && Strength == other.Strength
        && FrameFraction == other.FrameFraction && FillAlpha == other.FillAlpha
        && HoverIntensity == other.HoverIntensity && SweepIntensity == other.SweepIntensity
        && BackdropAlpha == other.BackdropAlpha;
}

//////////////////////////////////////////////////////////////////////////
// Reserves the batch and paint buffers once
void SSOLHoloFrame::Construct(const FArguments& /*args*/)
{
    SetVisibility(EVisibility::HitTestInvisible);

    // Its content moves every frame (sweep, hover) without invalidating: volatile, never cached by an invalidation
    // panel
    ForceVolatile(true);
    mBatch.Initialize(SOL::HoloMenu::FRAME_MAX_SHAPES);
    mPaintVertices.Reserve(SOL::HoloMenu::FRAME_MAX_SHAPES * SOL::Hologram::HUD_VERTICES_PER_SHAPE);
    SSOLHudShapeLayer::InitBackingBrush(mBackingBrush);
}

//////////////////////////////////////////////////////////////////////////
// Sets the subsystem the strength, material and view size come from
void SSOLHoloFrame::SetMenuSubsystem(USOLHoloMenuSubsystem* subsystem)
{
    mSubsystem = subsystem;
    RefreshBrush();
}

//////////////////////////////////////////////////////////////////////////
// Keeps the layout (local units) and invalidates the built frame when it changed
void SSOLHoloFrame::SetLayout(const FSOLHoloFrameSpec& localSpec)
{
    if (HoloFrameLayoutEquals(localSpec, mLocalSpec))
    {
        return;
    }
    mLocalSpec = localSpec;
    ++mLayoutGeneration;
}

//////////////////////////////////////////////////////////////////////////
// Sets the hovered item and its rect; returns true when a hover cue is due
bool SSOLHoloFrame::SetHoveredItem(const int32 item, const FBox2f& localRect, const ESOLHoloHoverMark mark,
    const double nowS, const bool bAllowCue)
{
    const FBox2f rect = item != INDEX_NONE ? localRect : FBox2f(ForceInit);
    if (!HoloFrameBoxEquals(rect, mHoverLocalRect) || mark != mHoverMark)
    {
        mHoverLocalRect = rect;
        mHoverMark = mark;
        ++mLayoutGeneration;
    }
    const bool bIsChanged = SOLHoloMenu::SetHovered(mAnim, item, nowS);
    return SOLHoloMenu::ShouldPlayHoverCue(mAnim, bIsChanged && bAllowCue, nowS);
}

//////////////////////////////////////////////////////////////////////////
// Starts the open (or reverses a close)
bool SSOLHoloFrame::BeginOpen(const double nowS)
{
    const bool bIsOpening = SOLHoloMenu::BeginOpen(mAnim, nowS, GetStrength());
    ClearHoverRect();
    return bIsOpening;
}

//////////////////////////////////////////////////////////////////////////
// Starts the close from the current progress
bool SSOLHoloFrame::BeginClose(const double nowS)
{
    const bool bIsClosing = SOLHoloMenu::BeginClose(mAnim, nowS, GetStrength());
    ClearHoverRect();
    return bIsClosing;
}

//////////////////////////////////////////////////////////////////////////
// Drops the anim to Closed at once, with no close and no hover
void SSOLHoloFrame::ResetClosed()
{
    mAnim = FSOLHoloMenuAnim();
    ClearHoverRect();
}

//////////////////////////////////////////////////////////////////////////
// Drops the hover rect when the anim has no hovered item (BeginOpen and BeginClose clear the hover)
void SSOLHoloFrame::ClearHoverRect()
{
    if (mAnim.HoveredItem == INDEX_NONE && mHoverLocalRect.bIsValid)
    {
        mHoverLocalRect = FBox2f(ForceInit);
        ++mLayoutGeneration;
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances the anim to nowS and returns its phase
ESOLHoloMenuPhase SSOLHoloFrame::Advance(const double nowS)
{
    SOLHoloMenu::Advance(mAnim, nowS, GetStrength());
    return mAnim.Phase;
}

//////////////////////////////////////////////////////////////////////////
// Returns the anim's look at nowS and the live strength
FSOLHoloMenuLook SSOLHoloFrame::EvaluateLook(const double nowS) const
{
    return SOLHoloMenu::Evaluate(mAnim, nowS, GetStrength());
}

//////////////////////////////////////////////////////////////////////////
// Returns the live hologram menu strength, or the default without a subsystem
float SSOLHoloFrame::GetStrength() const
{
    const USOLHoloMenuSubsystem* subsystem = mSubsystem.Get();
    return subsystem != nullptr ? subsystem->GetStrength() : SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;
}

//////////////////////////////////////////////////////////////////////////
// Points the brush at the subsystem's menu material when it changed
void SSOLHoloFrame::RefreshBrush() const
{
    const USOLHoloMenuSubsystem* subsystem = mSubsystem.Get();
    UMaterialInstanceDynamic* material = subsystem != nullptr ? subsystem->GetMenuMaterial() : nullptr;
    if (mBrush.GetResourceObject() == material)
    {
        return;
    }
    mBrush = FSlateBrush();
    mBrush.SetResourceObject(material);
    mBrush.ImageSize = FVector2D(1.0, 1.0);
    mResourceHandle = FSlateResourceHandle();
}

//////////////////////////////////////////////////////////////////////////
// Builds the frame for this paint, or, while Open with an unchanged key, reuses the last build and moves its sweep;
// true when it reused
bool SSOLHoloFrame::BuildOrReuse(const FSOLHoloFrameCacheKey& key, const FSOLHoloMenuLook& look,
    const float strength) const
{
    const bool bIsOpen = mAnim.Phase == ESOLHoloMenuPhase::Open;
    if (bIsOpen && mIsCacheValid && key.Equals(mCacheKey))
    {
        // The sweep line is the only thing that moved: the same expression BuildFrame uses, over the backing rect
        if (mSweepShape != INDEX_NONE)
        {
            const FBox2f& rect = mOutput.Backing.Rect;
            const float y = rect.Min.Y + look.SweepFraction * (rect.Max.Y - rect.Min.Y);
            mOutput.Shapes[mSweepShape].A.Y = y;
            mOutput.Shapes[mSweepShape].B.Y = y;
        }
        ++mCacheHitCount;
        return true;
    }

    // The layout in pixels: the widget's own rect, the menu's positions scaled by the geometry, the frame UI scale;
    // the local title step is the title text's width, and the pad around it (SDD 6.3) is added here at the frame's u
    const float k = key.GeometryScale;
    mPixelSpec = mLocalSpec;
    mPixelSpec.Rect = FBox2f(FVector2f::ZeroVector, key.SizePx);
    mPixelSpec.UiScale = key.UiScale;
    mPixelSpec.TitleStepWidthPx = mLocalSpec.TitleStepWidthPx > 0.0f ? mLocalSpec.TitleStepWidthPx * k
        + SOL::HoloMenu::FRAME_TITLE_STEP_PAD_PX * SOLHoloMenu::UiScale(key.UiScale) : 0.0f;
    mPixelSpec.DividerTop = mLocalSpec.DividerTop * k;
    mPixelSpec.DividerBottomY = mLocalSpec.DividerBottomY * k;
    mPixelSpec.OpenTopY = mLocalSpec.OpenTopY * k;
    mPixelSpec.OpenBottomY = mLocalSpec.OpenBottomY * k;
    mPixelSpec.BrokenStart = mLocalSpec.BrokenStart * k;
    mPixelSpec.BrokenEndX = mLocalSpec.BrokenEndX * k;
    mPixelSpec.BrokenStepAtPx = mLocalSpec.BrokenStepAtPx * k;
    mPixelSpec.BrokenStepPx = mLocalSpec.BrokenStepPx * k;
    for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
    {
        mPixelSpec.TabRects[tab] = HoloFrameScaleBox(mLocalSpec.TabRects[tab], k);
    }
    mPixelSpec.TabDividerX = mLocalSpec.TabDividerX * k;
    mPixelSpec.HoverItemRect = HoloFrameScaleBox(mHoverLocalRect, k);
    mPixelSpec.HoverMark = mHoverMark;
    SOLHoloFrameBuild::BuildFrame(mPixelSpec, look, strength, mOutput);

    // The sweep's index for the reuse path; a sweep with no backing rect to move along cannot be reused
    mSweepShape = INDEX_NONE;
    for (int32 index = 0; index < mOutput.ShapeCount; ++index)
    {
        if (mOutput.Shapes[index].Part == ESOLHoloPart::Sweep)
        {
            mSweepShape = index;
            break;
        }
    }
    mCacheKey = key;
    mIsCacheValid = bIsOpen && (mSweepShape == INDEX_NONE || mOutput.bHasBacking);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Moves the sweep quad of the window-space vertices to the sweep's current y (the rest of the frame did not change)
void SSOLHoloFrame::PatchSweepVertices(const FSlateRenderTransform& renderTransform, const float inverseScale) const
{
    if (mSweepShape == INDEX_NONE)
    {
        return;
    }
    const FVector2f offset(0.0f, mOutput.Shapes[mSweepShape].A.Y - mBatchSweepY);
    const TArray<FSlateVertex>& vertices = mBatch.GetVertices();
    const int32 first = mSweepShape * SOL::Hologram::HUD_VERTICES_PER_SHAPE;
    for (int32 index = first; index < first + SOL::Hologram::HUD_VERTICES_PER_SHAPE; ++index)
    {
        mPaintVertices[index].Position = renderTransform.TransformPoint((vertices[index].Position + offset)
            * inverseScale);
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances and evaluates the anim at this paint's time, builds (or reuses) the frame and draws the backing and the
// shapes (one custom-vertex element with the menu material)
int32 SSOLHoloFrame::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& /*widgetStyle*/, const bool /*bParentEnabled*/) const
{
    mLastPaintedShapeCount = 0;
    mLastPaintedBackingCount = 0;
    ++mPaintCount;
    if (!FSlateApplication::IsInitialized())
    {
        return layerId;
    }

    // One time read per paint (SDD 42 section 4.4); the strength is read live, so a Settings change shows at once
    const double nowS = FPlatformTime::Seconds();
    USOLHoloMenuSubsystem* subsystem = mSubsystem.Get();
    const float strength = GetStrength();
    SOLHoloMenu::Advance(mAnim, nowS, strength);
    const FSOLHoloMenuLook look = SOLHoloMenu::Evaluate(mAnim, nowS, strength);
    mLastLook = look;
    mLastStrength = strength;
    if (!look.bVisible)
    {
        mOutput.ShapeCount = 0;
        mOutput.bHasBacking = false;
        mIsCacheValid = false;
        mIsPaintValid = false;
        return layerId;
    }

    // The material's strength and view size (once per frame for every user), and the frame UI scale from the size
    // the material got. sol.HudRenderer is not followed: it switches the HUD to its Canvas fallback, and a UMG menu has
    // no Canvas path, so the frame keeps drawing with the shape material
    FVector2f viewSize = FVector2f::ZeroVector;
    if (subsystem != nullptr)
    {
        subsystem->UpdateMaterialForFrame();
        viewSize = subsystem->GetMaterialViewSizePx();
    }
    RefreshBrush();
    const float scale = allottedGeometry.Scale > 0.0f ? allottedGeometry.Scale : 1.0f;
    FSOLHoloFrameCacheKey key;
    key.LayoutGeneration = mLayoutGeneration;
    key.SizePx = FVector2f(allottedGeometry.GetLocalSize()) * scale;
    key.GeometryScale = scale;
    key.UiScale = viewSize.Y > 0.0f ? FMath::Clamp(viewSize.Y / SOL::Hologram::HUD_REFERENCE_HEIGHT_PX, 1.0f,
        SOL::Hologram::HUD_MAX_UI_SCALE) : 1.0f;
    key.Strength = strength;
    key.FrameFraction = look.FrameFraction;
    key.FillAlpha = look.FillAlpha;
    key.HoverIntensity = look.HoverIntensity;
    key.SweepIntensity = look.SweepIntensity;
    key.BackdropAlpha = look.BackdropAlpha;
    const bool bIsReused = BuildOrReuse(key, look, strength);

    // The faint tint under the shapes, then the shapes: pixels -> local units -> window space
    const float inverseScale = 1.0f / scale;
    if (mOutput.bHasBacking)
    {
        SSOLHudShapeLayer::PaintBacking(allottedGeometry, outDrawElements, layerId, mBackingBrush,
            mOutput.Backing.Rect, mOutput.Backing.CornerPx, mOutput.Backing.FeatherPx,
            SOLHoloFrameBuild::BackingOpacity(mOutput.Backing.Alpha), SOL::HoloMenu::BACKDROP_TINT, inverseScale);
        mLastPaintedBackingCount = 1;
    }

    // A reused build at the same window placement only moves the sweep quad; anything else refills and transforms all
    const FSlateRenderTransform& renderTransform = allottedGeometry.GetAccumulatedRenderTransform();
    if (bIsReused && mIsPaintValid && mResourceHandle.IsValid() && inverseScale == mPaintInverseScale
        && renderTransform == mPaintTransform && mBatch.GetShapeCount() == mOutput.ShapeCount)
    {
        PatchSweepVertices(renderTransform, inverseScale);
        FSlateDrawElement::MakeCustomVerts(outDrawElements, layerId + 1, mResourceHandle, mPaintVertices,
            mBatch.GetIndices(), nullptr, 0, 0);
        mLastPaintedShapeCount = mBatch.GetShapeCount();
        return layerId + HOLOFRAME_LAYERS_USED;
    }
    mBatch.Reset();
    SOLHoloFrameBuild::AppendToBatch(mOutput, mBatch);
    mBatchSweepY = mSweepShape != INDEX_NONE ? mOutput.Shapes[mSweepShape].A.Y : 0.0f;
    mIsPaintValid = SSOLHudShapeLayer::PaintBatch(allottedGeometry, outDrawElements, layerId + 1, mBatch, mBrush,
        mResourceHandle, mPaintVertices, inverseScale);
    if (mIsPaintValid)
    {
        mLastPaintedShapeCount = mBatch.GetShapeCount();
        mPaintTransform = renderTransform;
        mPaintInverseScale = inverseScale;
    }
    return layerId + HOLOFRAME_LAYERS_USED;
}

//////////////////////////////////////////////////////////////////////////
// Takes no layout space of its own
FVector2D SSOLHoloFrame::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D::ZeroVector;
}
