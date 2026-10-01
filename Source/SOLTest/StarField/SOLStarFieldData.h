/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "SOLStarFieldData.generated.h"

/** One bright (V < MagnitudeCutoff) star, as baked by Tools/StarField/bake_star_field.py (SDD 5 Appendix A). */
USTRUCT(BlueprintType)
struct FSOLBrightStar
{
    GENERATED_BODY()

    /** Unit direction toward the star, J2000 ecliptic, right-handed (convert with SOLRender::EclipticToUnreal). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    FVector3f Direction = FVector3f::ZeroVector;

    /** Apparent Johnson V magnitude. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    float VisualMagnitude = 0.0f;

    /** Relative flux, 10^(-0.4 (V - FluxReferenceMagnitude)): 1.0 at the cutoff, ~5970 for Sirius. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    float Flux = 0.0f;

    /** Linear sRGB (Rec.709, D65) color normalized to luminance 1; brightness lives only in Flux. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    FLinearColor Color = FLinearColor::White;
};

/**
 * Baked star-field data (SDD 5): the bright-star sprite list, brightest first, plus the bake metadata the runtime needs
 * to match the sprites' brightness to the faint-star cubemap. Written only by Tools/StarField/import_star_field.py;
 * a plain data container with no logic.
 */
UCLASS(BlueprintType)
class SOLTEST_API USOLStarFieldData : public UDataAsset
{
    GENERATED_BODY()

public:

    /** Bright stars, ascending V (brightest first). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    TArray<FSOLBrightStar> BrightStars;

    /** V magnitude splitting sprites (brighter) from the baked cubemap (this and fainter). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    float MagnitudeCutoff = 8.0f;

    /** Magnitude whose flux is 1.0, in both the sprite Flux values and the cubemap texels. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    float FluxReferenceMagnitude = 8.0f;

    /**
     * Solid angle (sr) of a face-center cubemap texel, (2 / CubeFaceSize)^2. Cube texel values are flux per this solid
     * angle (a mag-8 star's splatted texels sum to 1.0, weighted by relative texel solid angle). To match those units, a sprite of flux F
     * drawn over an on-screen solid angle OmegaSprite (sr) emits F * CubeTexelSolidAngleSr / OmegaSprite, so its total
     * light equals a cube star of the same flux whatever size it is drawn at. Example: a sprite at the minimum
     * on-screen size has emissive F * CubeTexelSolidAngleSr / OmegaMin.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    float CubeTexelSolidAngleSr = 0.0f;

    /** Edge length of one cubemap face in texels, as baked. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    int32 CubeFaceSize = 0;

    /** Catalog attribution the CC BY-SA 4.0 license requires (SDD 5 decision 1). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SOL|StarField")
    FString Attribution;
};
