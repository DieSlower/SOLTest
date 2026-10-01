/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SOLStarField.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

/**
 * The night sky (SDD 5 Appendix B): one instanced quad per bright star (V < 8) from USOLStarFieldData, and a large
 * sphere showing the faint-star cubemap behind them. Stars are effectively at infinity (decision 10), so both are
 * pinned at the Unreal origin, which the render origin keeps within 10 km of the camera (see the constructor), and
 * nothing ever moves or rotates: the baked directions are already in the ecliptic frame, converted once with
 * SOLRender::EclipticToUnreal. Each quad is turned toward the center when it is created, so every sprite faces the
 * camera without per-frame or per-vertex work. It does not tick, binds no events, and is written once in BeginPlay.
 */
UCLASS()
class SOLTEST_API ASOLStarField : public AActor
{
    GENERATED_BODY()

public:

    // Creates the root, the bright-star instance component and the sky sphere
    ASOLStarField();

protected:

    // Fills the bright-star instances and sets up both materials, once; nothing changes afterwards
    virtual void BeginPlay() override;

private:

    // Sets the sky sphere's mesh, material and radius; returns false if an asset is missing
    bool SetUpSky();

    // Adds one camera-facing quad per bright star with its flux-scaled color as custom data; returns false on failure
    bool SetUpBrightStars();

    /** Scene root; both primitives use absolute transforms at the Unreal origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|StarField")
    TObjectPtr<USceneComponent> SceneRoot;

    /** One quad per bright star (M_SOLStarSprite), at STAR_FIELD_SPRITE_RADIUS_CM around the Unreal origin. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|StarField")
    TObjectPtr<UInstancedStaticMeshComponent> BrightStars;

    /** Sphere of radius STAR_FIELD_SKY_RADIUS_CM around the Unreal origin showing the faint-star cube (M_SOLStarSky) */
    UPROPERTY(VisibleAnywhere, Category = "SOL|StarField")
    TObjectPtr<UStaticMeshComponent> Sky;
};
