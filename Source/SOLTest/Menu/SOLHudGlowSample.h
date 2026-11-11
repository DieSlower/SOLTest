/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHudShapeMath.h"

#include "Components/Widget.h"
#include "CoreMinimal.h"

#include "SOLHudGlowSample.generated.h"

class SSOLHudShapeLayer;
class UMaterialInstanceDynamic;

/**
 * The HUD glow sample under the Settings page's HUD glow row (SDD 37 D16, section 4.10): a UMG wrapper of a small
 * retained SSOLHudShapeLayer drawn with its own M_SOLHudShape instance, showing one dark HUD panel backing, a ring, a
 * line and one glow-text line the way the flight HUD draws them. The page calls Update from its tick with the pending
 * Settings values: the glow scale (FSOLGlowScaleFeed) and the effects strength go to the material and the glow text's
 * outline alpha to the layer, each only when it changes, so the sample follows the slider before Apply. The batch is
 * rebuilt only when the view's pixel size changes. Its own material instance leaves the Help glossary's shared one
 * untouched (the glossary follows HUD glow from part 37b). The view clips to its bounds and takes no hit test.
 */
UCLASS()
class SOLTEST_API USOLHudGlowSample : public UWidget
{
    GENERATED_BODY()

public:

    // Clips to the view's bounds and takes no hit test
    USOLHudGlowSample();

    // Follows the pending values: pushes the glow scale of hudGlowSetting, its text alpha and the effects strength
    // (sanitized) when they change, and rebuilds the picture when the view's pixel size changed (its laid-out size at
    // layoutScale; 0: the geometry's own scale); returns whether anything changed
    bool Update(float hudGlowSetting, float effectsStrength, float layoutScale = 0.0f);

    // Returns the glow scale last pushed to the material, -1 before the first push (verification)
    float GetGlowScale() const { return mGlowFeed.GetScale(); }

    // Returns the layer's glow-text outline alpha, -1 without a layer (verification)
    float GetTextGlowAlpha() const;

    // Returns the effects strength last pushed to the material, -1 before the first push (verification)
    float GetEffectsStrength() const { return mStrength; }

    // Returns the shapes, text lines and backings the last paint drew (verification)
    int32 GetPaintedShapeCount() const;
    int32 GetPaintedTextCount() const;
    int32 GetPaintedBackingCount() const;

    // Returns the shapes of the last build (verification)
    int32 GetShapeCount() const;

    // Returns how many times the picture was built (verification: once per pixel size)
    int32 GetBuildCount() const { return mBuildCount; }

    // Returns the pixel size of the last build ((0, 0) before the first)
    FIntPoint GetBuiltSizePx() const { return mSizePx; }

    // Returns the material the layer draws with, or null (verification: the sample's own instance)
    UObject* GetMaterial() const;

    // Drops the layer and forgets the build and the pushed values, so a rebuilt widget pushes and builds again
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:

    // Creates the retained shape layer with the sample's own material instance, the HUD fonts and the glow text
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:

    // Refills the layer at a pixel size: the backing, the ring, the line and the text line (fitted to its room)
    void Rebuild(const FIntPoint& sizePx, float pxPerUnit);

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> Material;          // The sample's own M_SOLHudShape instance

    TSharedPtr<SSOLHudShapeLayer> mLayer;
    FSOLGlowScaleFeed mGlowFeed;                            // Glow scale last pushed (SDD 37 section 4.10)
    FText mText;                                            // The text line, made once
    FIntPoint mSizePx = FIntPoint::ZeroValue;               // Pixel size of the last build
    float mStrength = -1.0f;                                // Effects strength last pushed
    int32 mBuildCount = 0;
};
