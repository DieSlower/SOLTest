/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidStyle.h"
#include "Menu/SOLSolidTraceLayer.h"

#include "Components/ContentWidget.h"
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

#include "SOLSolidPanel.generated.h"

class SBox;
class SImage;
class SOverlay;
class UMaterialInstanceDynamic;

/**
 * A solid hull panel or recess with its content (SDD 46 sections 4.2, 5.2 and 6.1): a UContentWidget wrapping an
 * SOverlay of an SImage (the hull brush of USOLSolidMenuSubsystem's style material, the look in its tint), the content
 * slot (inside the content padding) and an SSOLSolidTraceLayer on top, plus the optional rivets (T_SOLSolidRivet near
 * the corners) and the vent strip (T_SOLSolidVent top-right). A hull with a title step, chin or notches gets its own
 * material (CreateUniqueMaterial, a handful in total) with the hull's shape parameters written once per SetHull; plain
 * panels and recesses share their style's material. The owning screen drives the open look (render-transform slide
 * and render opacity, so the brush tint stays the look's encoding), the trace power-up and the pulse from its tick;
 * the panel itself does not tick. The brushes are UPROPERTY(Transient) members (the SImages hold them by pointer, and
 * the property keeps each brush's material or texture referenced for as long as the panel lives); ReleaseSlateResources
 * drops the Slate widgets only. Setting a different style recreates the unique material from that style.
 */
UCLASS()
class SOLTEST_API USOLSolidPanel : public UContentWidget
{
    GENERATED_BODY()

public:

    // Sets the hull's style (Panel or Recess; others draw as given) and shape in UMG units; a step, chin or notch
    // makes the panel use its own material, written with the shape now (not per frame)
    void SetHull(ESOLSolidStyle style, const FSOLSolidHull& hull);

    // Sets the padding between the hull's edge and the content
    void SetContentPadding(const FMargin& padding);

    // Shows or hides the corner rivets and the top-right vent strip
    void SetDecor(bool bRivets, bool bVents);

    // Applies the open look at an open progress in [0, 1]: render opacity SOLHoloMenu::FillFade-style fade and a
    // SOLSolidMenu::PanelSlide offset along slideDirection (unit vector, from the panel's anchor side); progress 1 is
    // the identity (no offset, opacity 1)
    void ApplyOpenLook(float progress, const FVector2f& slideDirection);

    // Sets the static circuit traces (layer local UMG units; see SSOLSolidTraceLayer::SetTraces)
    void SetTraces(TConstArrayView<FSOLSolidTrace> traces);

    // Feeds the trace layer its power-up fraction, idle pulse and strength (changes only invalidate its paint)
    void SetTraceLook(float power, float pulse, float strength);

    // Returns the trace layer (null before the Slate widget is built), for the buttons' hover traces
    TSharedPtr<SSOLSolidTraceLayer> GetTraceLayer() const { return mTraceLayer; }

    // Returns the hull style
    ESOLSolidStyle GetStyle() const { return mStyle; }

    // Returns the hull shape as last set (unclamped; the material clamps it for its size)
    const FSOLSolidHull& GetHull() const { return mHull; }

    // Returns whether the panel draws with its own material (a unique shape) rather than its style's shared one
    bool HasUniqueMaterial() const { return UniqueMaterial != nullptr; }

    // Returns whether the hull draws with a material (false: the flat fallback brush)
    bool IsHullMaterialDrawn() const;

    // Drops the Slate widgets (the brushes stay: they are rebuilt with the widget)
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:

    // Builds the overlay: hull image, decor, content box, trace layer
    virtual TSharedRef<SWidget> RebuildWidget() override;

    // Applies the hull brush, padding and decor to the built widgets
    virtual void SynchronizeProperties() override;

    // The content slot is a plain UPanelSlot
    virtual UClass* GetSlotClass() const override;

    // Puts an added child's widget into the content box
    virtual void OnSlotAdded(UPanelSlot* slot) override;

    // Empties the content box
    virtual void OnSlotRemoved(UPanelSlot* slot) override;

private:

    static constexpr int32 RIVET_COUNT = 4;

    // Rebuilds the hull brush from the style, the shape and the subsystem's material (or the flat fallback)
    void RefreshHullBrush();

    // Shows or hides the built rivet and vent images
    void RefreshDecor();

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> UniqueMaterial;    // Only for a hull with a step, chin or notches

    UPROPERTY(Transient)
    FSlateBrush HullBrush;      // The hull (the style's shared material, UniqueMaterial or the flat fallback)

    UPROPERTY(Transient)
    FSlateBrush RivetBrush;     // T_SOLSolidRivet

    UPROPERTY(Transient)
    FSlateBrush VentBrush;      // T_SOLSolidVent

    FSOLSolidHull mHull;
    FMargin mContentPadding;
    ESOLSolidStyle mUniqueMaterialStyle = ESOLSolidStyle::Panel;   // The style UniqueMaterial was made from
    TArray<FSOLSolidTrace> mPendingTraces;      // Traces set before the trace layer exists
    TSharedPtr<SOverlay> mOverlay;
    TSharedPtr<SImage> mHullImage;
    TSharedPtr<SBox> mContentBox;
    TSharedPtr<SSOLSolidTraceLayer> mTraceLayer;
    TSharedPtr<SImage> mRivetImages[RIVET_COUNT];
    TSharedPtr<SImage> mVentImage;
    FVector2f mAppliedOffset = FVector2f::ZeroVector;   // Open look last applied (skips unchanged writes)
    float mAppliedOpacity = 1.0f;
    ESOLSolidStyle mStyle = ESOLSolidStyle::Panel;
    bool mHasRivets = false;
    bool mHasVents = false;
};
