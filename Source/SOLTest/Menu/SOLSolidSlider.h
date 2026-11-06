/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSolidStyle.h"

#include "Components/Slider.h"
#include "CoreMinimal.h"
#include "Widgets/Input/SSlider.h"

#include "SOLSolidSlider.generated.h"

class USOLSolidMenuSubsystem;

/**
 * The solid slider's Slate widget (SDD 46 section 5.2, 46e follow-up): SSlider's paint with a cyan fill between the
 * groove and the knob, from the groove's start to the knob's centre (SOLSolidStyle::SliderFill), so the value reads
 * at a glance. The fill follows the same normalized value as the knob, so it tracks a drag, a key step and a
 * SetValue alike. A horizontal slider only; a vertical one paints as SSlider does (no fill). Paint makes no heap
 * allocation: one more box element per slider. The last painted fill is kept for verification. It declares no
 * SLATE_DECLARE_WIDGET: it adds no TSlateAttribute (a new one would need the macro and its attribute registration).
 */
class SOLTEST_API SSOLSolidSlider : public SSlider
{
public:

    // Sets the fill brush (copied; drawn with the slider's own disabled effect) and repaints
    void SetFillBrush(const FSlateBrush& brush);

    // Paints the groove, the fill and the knob (SSlider::OnPaint's horizontal geometry with the fill between)
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // Returns the fill of the last paint and the bar's length then (local units); false before the first paint
    bool GetPaintedFill(FSOLSliderFill& outFill, float& outBarLength) const;

private:

    FSlateBrush mFillBrush;                     // The cyan fill (SOLSolidStyle::MakeSliderFillBrush)
    mutable FSOLSliderFill mPaintedFill;        // Last paint's fill (verification)
    mutable float mPaintedBarLength = 0.0f;     // ... and the bar's length then
    mutable bool mHasPainted = false;
};

/**
 * A solid menu slider (46e follow-up): a USlider whose Slate widget is SSOLSolidSlider (the cyan fill up to the value)
 * and whose style comes from SetSolidStyle (SOLSolidStyle::MakeSliderStyle, bar and handle colours white so the brush
 * tints reach the hull material, and the fill brush). Every solid slider uses it: the Settings page and the smoke
 * comparison board.
 */
UCLASS()
class SOLTEST_API USOLSolidSlider : public USlider
{
    GENERATED_BODY()

public:

    // Starts with the cyan fill brush (a slider never given SetSolidStyle still draws a sensible fill)
    USOLSolidSlider(const FObjectInitializer& objectInitializer);

    // Gives the slider the solid groove and knob (SOLSolidStyle::MakeSliderStyle), white bar and handle colours and
    // the cyan fill brush
    void SetSolidStyle(const USOLSolidMenuSubsystem* subsystem);

    // Returns the fill brush
    const FSlateBrush& GetFillBrush() const { return FillBrush; }

    // Returns the fill of the last paint and the bar's length then (local units); false before the first paint or
    // without a Slate widget (verification)
    bool GetPaintedFill(FSOLSliderFill& outFill, float& outBarLength) const;

protected:

    // Builds the SSOLSolidSlider (the arguments of UE 5.8's USlider::RebuildWidget, mirrored: keep them in step on an
    // engine upgrade) and hands it the fill brush
    virtual TSharedRef<SWidget> RebuildWidget() override;

    // Drops the Slate widget with the base class's
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:

    UPROPERTY(Transient)
    FSlateBrush FillBrush;                      // The cyan fill (SOLSolidStyle::MakeSliderFillBrush)

    TSharedPtr<SSOLSolidSlider> mSolidSlider;   // MySlider as the solid widget
};
