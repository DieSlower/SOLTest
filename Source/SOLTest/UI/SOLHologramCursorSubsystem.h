/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudShapeMath.h"

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/WorldSubsystem.h"
#include "Widgets/SLeafWidget.h"

#include "SOLHologramCursorSubsystem.generated.h"

class UMaterialInstanceDynamic;
class USOLSettingsSubsystem;

/**
 * The hologram mouse pointer (SDD 22 section 2.5): a leaf widget Slate draws as the game viewport's software cursor,
 * centred on the pointer, so the arrow's tip (SOLHologramCursor::ArrowSegments) sits at the widget centre and the
 * clicks land where the tip points. The arrow is a few glow lines in one custom-vertex draw with the HUD shape
 * material (HUD palette, the effects slider's row jitter), built once; the material's effects strength and (SDD 37
 * D17) its HUD glow scale follow the pending Settings values, each pushed only when it changes.
 */
class SOLTEST_API SSOLHologramCursor : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SSOLHologramCursor) {}
    SLATE_END_ARGS()

    // Builds the arrow's shapes once (local pixels around the widget centre)
    void Construct(const FArguments& args);

    // Sets the material (kept alive by the subsystem) and the settings the effects strength is read from
    void SetMaterial(UMaterialInstanceDynamic* material, USOLSettingsSubsystem* settings);

    // Returns the number of times the cursor was painted (verification)
    int32 GetPaintCount() const { return mPaintCount; }

    // Returns the effects strength last pushed to the material, -1 before the first paint (verification, D19)
    float GetPushedStrength() const { return mPushedStrength; }

    // Returns the HUD glow scale last pushed to the material, -1 before the first paint (verification, SDD 37 D17)
    float GetPushedGlowScale() const { return mGlowFeed.GetScale(); }

    // Draws the arrow as one custom-vertex element
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // The square whose centre is the hot spot (SOLHologramCursor::WidgetSidePx)
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    FSOLHudShapeBatch mBatch;                               // The arrow, local pixels (built once)
    FSlateBrush mBrush;                                     // Brush wrapping the shape material
    mutable FSlateResourceHandle mResourceHandle;           // Renderer handle of the brush, fetched once
    mutable TArray<FSlateVertex> mPaintVertices;            // The arrow in window space, reused every paint
    TWeakObjectPtr<UMaterialInstanceDynamic> mMaterial;
    TWeakObjectPtr<USOLSettingsSubsystem> mSettings;
    mutable float mPushedStrength = -1.0f;                  // Effects strength last pushed to the material
    mutable FSOLGlowScaleFeed mGlowFeed;                    // HUD glow scale last pushed to the material (SDD 37)
    mutable int32 mPaintCount = 0;
};

/**
 * Installs the hologram pointer as the game viewport's software cursor for every cursor type while a game world runs
 * (the hardware arrow is then hidden by Slate everywhere in the game: flight with a free cursor, the map, the Overlay
 * Menu, the F3 panel, the pause and main menus and Settings), and removes it when the world ends.
 */
UCLASS()
class SOLTEST_API USOLHologramCursorSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Creates the material instance and the cursor widget and hands them to the game viewport
    virtual void OnWorldBeginPlay(UWorld& world) override;

    // Takes the cursor widget back from the game viewport
    virtual void Deinitialize() override;

    // Returns the installed cursor widget, or null (verification)
    TSharedPtr<SSOLHologramCursor> GetCursorWidget() const { return mCursor; }

protected:

    // Game and PIE worlds only
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CursorMaterial;

    TSharedPtr<SSOLHologramCursor> mCursor;
    TWeakObjectPtr<class UGameViewportClient> mViewport;    // The viewport the cursor was handed to
};
