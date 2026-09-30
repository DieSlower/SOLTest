/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapBodyOverlay.h"

#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLMapPickRadius.h"
#include "SOLConstants.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistry.h"
#include "Visuals/SOLBodyVisuals.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"

namespace
{
    // Disc tessellation and the dark ring just outside a disc that keeps it readable on bright backgrounds
    constexpr int32 MAP_OVERLAY_DISC_SEGMENTS = 20;
    constexpr double MAP_OVERLAY_RIM_PX = 1.0;
    constexpr float MAP_OVERLAY_RIM_ALPHA = 0.6f;

    // Triangles per icon: the disc's fan plus two per ring segment
    constexpr int32 MAP_OVERLAY_TRIANGLES_PER_ICON = 3 * MAP_OVERLAY_DISC_SEGMENTS;

    // Icons at or below this alpha are nearly invisible: they get no label and do not block label space
    constexpr double MAP_OVERLAY_MIN_LABEL_ALPHA = 0.05;

    // Gap between a disc and its label, and the empty space kept around each label (pixels at a 720 px canvas)
    constexpr double MAP_OVERLAY_LABEL_GAP_PX = 3.0;
    constexpr double MAP_OVERLAY_LABEL_PADDING_PX = 1.0;

    // Projection: bodies closer than this in front of the camera (cm) are not drawn
    constexpr double MAP_OVERLAY_NEAR_PLANE_CM = 10.0;

    // Icon colors are the body's base color scaled so the brightest channel reaches this (dark bodies stay visible)
    constexpr float MAP_OVERLAY_ICON_BRIGHTNESS = 1.0f;

    //////////////////////////////////////////////////////////////////////////
    // Returns a body's icon color: its base material color with the hue kept and the brightest channel at full value
    FLinearColor MapOverlayIconColor(const FName bodyName)
    {
        FLinearColor base = FLinearColor::White;
        SOLBodyAppearance::FindBaseColor(bodyName, base);
        const float brightest = FMath::Max3(base.R, base.G, base.B);
        FLinearColor color = brightest > UE_KINDA_SMALL_NUMBER ? base * (MAP_OVERLAY_ICON_BRIGHTNESS / brightest)
            : FLinearColor::White;
        color.A = 1.0f;
        return color;
    }
}

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapBodyOverlay::FSOLMapBodyOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapBodyOverlay::~FSOLMapBodyOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Caches each body's icon color, label text and label priority, and sizes the per-frame buffers
void FSOLMapBodyOverlay::Initialize(const FSOLBodyRegistry& registry)
{
    const int32 bodyCount = registry.Num();
    mIconColors.Reset(bodyCount);
    mLabelSources.Reset(bodyCount);
    mLabels.Reset(bodyCount);
    mLabelOrder.Reset(bodyCount);
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FName bodyName = registry.GetName(index);
        mIconColors.Add(MapOverlayIconColor(bodyName));
        mLabelSources.Add(bodyName.ToString());
        mLabels.Add(FText::FromString(mLabelSources.Last()));
        mLabelOrder.Add(index);
    }

    // Bigger bodies claim their label spot first
    mLabelOrder.Sort([&registry](const int32 a, const int32 b)
    {
        return registry.GetRadiusM(a) > registry.GetRadiusM(b);
    });

    // Unit circle of the disc and ring tessellation, so a frame never calls sin/cos
    mUnitCircle.Reset(MAP_OVERLAY_DISC_SEGMENTS);
    for (int32 segment = 0; segment < MAP_OVERLAY_DISC_SEGMENTS; ++segment)
    {
        const double angle = UE_DOUBLE_TWO_PI * segment / MAP_OVERLAY_DISC_SEGMENTS;
        mUnitCircle.Add(FVector2D(FMath::Cos(angle), FMath::Sin(angle)));
    }

    // Per-frame buffers sized once (each icon is its fill disc plus its rim ring)
    mLabelSizesPx.Init(FVector2D::ZeroVector, bodyCount);
    mIconAlphas.Init(0.0, bodyCount);
    mPickRadiiM.Init(0.0, bodyCount);
    mDistancesM.Init(0.0, bodyCount);
    mScreenPx.Init(FVector2D::ZeroVector, bodyCount);
    mApparentRadiusPx.Init(0.0, bodyCount);
    mIconRadiusPx.Init(0.0, bodyCount);
    mPickRadiusPx.Init(0.0, bodyCount);
    mDrawOrder.Reset(bodyCount);
    mOccupied.Reset(2 * bodyCount);
    mIconBatch = MakeUnique<FCanvasTriangleItem>(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector,
        GWhiteTexture);
    mIconBatch->BlendMode = SE_BLEND_Translucent;
    mIconBatch->TriangleList.Reset(MAP_OVERLAY_TRIANGLES_PER_ICON * bodyCount);
    mHasLabelSizes = false;
}

//////////////////////////////////////////////////////////////////////////
// Evaluates every body's LOD from the map camera and draws its icon and label on the canvas
void FSOLMapBodyOverlay::Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLMapModeSubsystem& mapMode,
    const USOLAnchorSubsystem& anchor, const FSOLBodyRegistry& registry, UFont* font, const FLinearColor& labelColor,
    const float uiScale)
{
    mLastIconsDrawn = 0;
    mLastLabelsDrawn = 0;
    const int32 bodyCount = FMath::Min(registry.Num(), mIconAlphas.Num());
    if (!mIconBatch.IsValid() || bodyCount == 0 || canvas.ClipX <= 0.0f || canvas.ClipY <= 0.0f)
    {
        return;
    }

    // Label sizes need a canvas and a font: measured once, at scale 1
    if (!mHasLabelSizes && font != nullptr)
    {
        for (int32 index = 0; index < bodyCount; ++index)
        {
            float width = 0.0f;
            float height = 0.0f;
            canvas.TextSize(font, mLabelSources[index], width, height);
            mLabelSizesPx[index] = FVector2D(width, height);
        }
        mHasLabelSizes = true;
    }

    // Projection inputs of this frame: the real viewport and the map camera's vertical FOV
    mLodParams.ViewportHeightPx = canvas.ClipY;
    mLodParams.VerticalFovRad = mapMode.GetVerticalFovRad(static_cast<double>(canvas.ClipX) / canvas.ClipY);
    const FVector3d& cameraM = mapMode.GetCameraPositionM();
    const double minPickPx = SOL::MAP_MIN_PICK_RADIUS_PX * uiScale;
    const double iconRadiusPx = 0.5 * SOL::MAP_ICON_DIAMETER_PX * uiScale;
    const double rimPx = MAP_OVERLAY_RIM_PX * uiScale;
    mIconBatch->Texture = GWhiteTexture;
    mIconBatch->TriangleList.Reset();
    mOccupied.Reset();
    mDrawOrder.Reset();

    // Per body: LOD, pick radius and screen projection from the map camera; bodies with any icon alpha on screen
    // become icon candidates, and every projected body can occlude farther icons
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const double radiusM = registry.GetRadiusM(index);
        const double distanceM = FVector3d::Dist(registry.GetPositionM(index), cameraM);
        const FSOLBodyLodResult lod = SOLMapBodyLod::Evaluate(radiusM, distanceM, mLodParams);
        mIconAlphas[index] = lod.IconAlpha;
        mPickRadiiM[index] = SOLMapPickRadius::ComputeMapPickRadiusM(radiusM, distanceM, minPickPx,
            mLodParams.ViewportHeightPx, mLodParams.VerticalFovRad);
        mDistancesM[index] = distanceM;
        mApparentRadiusPx[index] = 0.0;
        mIconRadiusPx[index] = 0.0;
        mPickRadiusPx[index] = 0.0;

        // Project the body where its mesh is placed (same render placement as ASOLBodyVisuals)
        const FVector local = view.Rotation.UnrotateVector(FVector(anchor.ComputeBodyRenderPlacement(index).LocationCm)
            - view.LocationCm);
        if (local.X < MAP_OVERLAY_NEAR_PLANE_CM)
        {
            continue;
        }
        const FVector2D screen(view.Center.X + view.FocalPx * (local.Y / local.X),
            view.Center.Y - view.FocalPx * (local.Z / local.X));
        mScreenPx[index] = screen;
        mApparentRadiusPx[index] = 0.5 * lod.ApparentDiameterPx;

        // On screen, the pick radius in meters (max of the real radius and minPickPx's span) is exactly this
        mPickRadiusPx[index] = FMath::Max(mApparentRadiusPx[index], minPickPx);
        if (lod.IconAlpha <= 0.0)
        {
            continue;
        }

        // Never smaller than the mesh, so the mesh fades out under a solid disc instead of popping
        const double radiusPx = FMath::Max(iconRadiusPx, mApparentRadiusPx[index]);
        if (screen.X < -radiusPx || screen.Y < -radiusPx || screen.X > canvas.ClipX + radiusPx
            || screen.Y > canvas.ClipY + radiusPx)
        {
            continue;
        }
        mIconRadiusPx[index] = radiusPx;
    }

    // Occlusion: a body whose center lies inside a nearer body's apparent disc is hidden behind that body, so it
    // draws no icon and cannot be picked (the nearer body wins the click)
    for (int32 index = 0; index < bodyCount; ++index)
    {
        if (mPickRadiusPx[index] > 0.0 && IsOccluded(index, bodyCount))
        {
            mIconRadiusPx[index] = 0.0;
            mPickRadiusPx[index] = 0.0;
        }
        if (mIconRadiusPx[index] > 0.0)
        {
            mDrawOrder.Add(index);
        }
    }

    // Back to front, so a nearer icon composites over a farther one; each icon is its fill disc plus a dark ring
    // just outside it (the ring never overlaps the fill, so the fill's alpha alone sets the mesh/icon blend)
    mDrawOrder.Sort([this](const int32 a, const int32 b)
    {
        return mDistancesM[a] > mDistancesM[b];
    });
    for (const int32 index : mDrawOrder)
    {
        const FVector2D& screen = mScreenPx[index];
        const double radiusPx = mIconRadiusPx[index];
        const float alpha = static_cast<float>(mIconAlphas[index]);
        FLinearColor fill = mIconColors[index];
        fill.A = alpha;
        AppendDisc(screen, radiusPx, fill);
        AppendRing(screen, radiusPx, radiusPx + rimPx, FLinearColor(0.0f, 0.0f, 0.0f, MAP_OVERLAY_RIM_ALPHA * alpha));
        if (mIconAlphas[index] > MAP_OVERLAY_MIN_LABEL_ALPHA)
        {
            mOccupied.Add(FBox2D(screen - FVector2D(radiusPx), screen + FVector2D(radiusPx)));
        }
        ++mLastIconsDrawn;
    }
    if (mIconBatch->TriangleList.Num() > 0)
    {
        canvas.DrawItem(*mIconBatch);
    }
    if (font == nullptr)
    {
        return;
    }

    // Labels, bigger bodies first: right, left, above or below the disc, the first spot clear of discs and labels
    const double gap = MAP_OVERLAY_LABEL_GAP_PX * uiScale;
    const FVector2D padding(MAP_OVERLAY_LABEL_PADDING_PX * uiScale);
    for (const int32 index : mLabelOrder)
    {
        const double radiusPx = index < bodyCount ? mIconRadiusPx[index] : 0.0;
        if (radiusPx <= 0.0 || mIconAlphas[index] <= MAP_OVERLAY_MIN_LABEL_ALPHA)
        {
            continue;
        }
        const FVector2D& screen = mScreenPx[index];
        const FVector2D size = mLabelSizesPx[index] * uiScale;
        const FVector2D candidates[] =
        {
            FVector2D(screen.X + radiusPx + gap, screen.Y - 0.5 * size.Y),
            FVector2D(screen.X - radiusPx - gap - size.X, screen.Y - 0.5 * size.Y),
            FVector2D(screen.X - 0.5 * size.X, screen.Y - radiusPx - gap - size.Y),
            FVector2D(screen.X - 0.5 * size.X, screen.Y + radiusPx + gap),
        };
        for (const FVector2D& topLeft : candidates)
        {
            const FBox2D rect(topLeft - padding, topLeft + size + padding);
            if (!IsFree(rect))
            {
                continue;
            }
            mOccupied.Add(rect);
            FLinearColor color = labelColor;
            color.A *= static_cast<float>(mIconAlphas[index]);
            FCanvasTextItem item(topLeft, mLabels[index], font, color);
            item.Scale = FVector2D(uiScale, uiScale);
            item.EnableShadow(FLinearColor(0.0f, 0.0f, 0.0f, color.A));
            canvas.DrawItem(item);
            ++mLastLabelsDrawn;
            break;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true when a body's screen center lies inside the apparent disc of a nearer projected body
bool FSOLMapBodyOverlay::IsOccluded(const int32 index, const int32 bodyCount) const
{
    const FVector2D& screen = mScreenPx[index];
    const double distanceM = mDistancesM[index];
    for (int32 other = 0; other < bodyCount; ++other)
    {
        const double otherRadiusPx = mApparentRadiusPx[other];
        if (other == index || otherRadiusPx <= 0.0 || mDistancesM[other] >= distanceM)
        {
            continue;
        }
        if (FVector2D::DistSquared(screen, mScreenPx[other]) < otherRadiusPx * otherRadiusPx)
        {
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Appends a filled disc to the icon triangle batch
void FSOLMapBodyOverlay::AppendDisc(const FVector2D& center, const double radiusPx, const FLinearColor& color)
{
    // A triangle fan around the center, one triangle per segment of the precomputed unit circle
    const int32 segments = mUnitCircle.Num();
    for (int32 segment = 0; segment < segments; ++segment)
    {
        FCanvasUVTri& triangle = mIconBatch->TriangleList.AddDefaulted_GetRef();
        triangle.V0_Pos = center;
        triangle.V1_Pos = center + radiusPx * mUnitCircle[segment];
        triangle.V2_Pos = center + radiusPx * mUnitCircle[(segment + 1) % segments];
        triangle.V0_Color = color;
        triangle.V1_Color = color;
        triangle.V2_Color = color;
    }
}

//////////////////////////////////////////////////////////////////////////
// Appends a ring (annulus) between two radii to the icon triangle batch
void FSOLMapBodyOverlay::AppendRing(const FVector2D& center, const double innerRadiusPx, const double outerRadiusPx,
    const FLinearColor& color)
{
    // Two triangles per segment; the inner edge uses the same points as the disc's rim, so they share an exact edge
    const int32 segments = mUnitCircle.Num();
    for (int32 segment = 0; segment < segments; ++segment)
    {
        const FVector2D& unitA = mUnitCircle[segment];
        const FVector2D& unitB = mUnitCircle[(segment + 1) % segments];
        const FVector2D innerA = center + innerRadiusPx * unitA;
        const FVector2D innerB = center + innerRadiusPx * unitB;
        const FVector2D outerA = center + outerRadiusPx * unitA;
        const FVector2D outerB = center + outerRadiusPx * unitB;
        FCanvasUVTri& first = mIconBatch->TriangleList.AddDefaulted_GetRef();
        first.V0_Pos = innerA;
        first.V1_Pos = outerA;
        first.V2_Pos = outerB;
        first.V0_Color = color;
        first.V1_Color = color;
        first.V2_Color = color;
        FCanvasUVTri& second = mIconBatch->TriangleList.AddDefaulted_GetRef();
        second.V0_Pos = innerA;
        second.V1_Pos = outerB;
        second.V2_Pos = innerB;
        second.V0_Color = color;
        second.V1_Color = color;
        second.V2_Color = color;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true when a rectangle overlaps none of the occupied rectangles
bool FSOLMapBodyOverlay::IsFree(const FBox2D& rect) const
{
    for (const FBox2D& occupied : mOccupied)
    {
        if (rect.Intersect(occupied))
        {
            return false;
        }
    }
    return true;
}
