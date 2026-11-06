/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidSlider.h"

#include "Menu/SOLSolidMenuSubsystem.h"
#include "SOLConstants.h"

#include "Rendering/DrawElements.h"

//////////////////////////////////////////////////////////////////////////
// Paints the groove, the fill and the knob: SSlider::OnPaint's horizontal geometry, with the cyan fill on its own layer
// between the two so the knob covers the fill's end
int32 SSOLSolidSlider::OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
    FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
    const bool bParentEnabled) const
{
    if (Style == nullptr)
    {
        return layerId;
    }
    if (Orientation != Orient_Horizontal)
    {
        return SSlider::OnPaint(args, allottedGeometry, cullingRect, outDrawElements, layerId, widgetStyle,
            bParentEnabled);
    }

    // The slider's geometry, as SSlider computes it
    const FVector2f size = allottedGeometry.GetLocalSize();
    const FSlateBrush* barImage = GetBarImage();
    const FSlateBrush* thumbImage = GetThumbImage();
    const FVector2f handleSize = thumbImage->ImageSize;
    const bool bIndent = GetIndentHandleAttribute().Get();
    const float indent = bIndent ? handleSize.X : 0.0f;
    const float percent = FMath::Clamp(GetNormalizedValue(), 0.0f, 1.0f);
    const float sliderY = 0.5f * size.Y;
    const float barStartX = 0.5f * handleSize.X;
    const float barLength = size.X - handleSize.X;
    const FVector2f handleTopLeft(percent * (size.X - (indent + handleSize.X)) + 0.5f * indent,
        sliderY - 0.5f * handleSize.Y);
    const bool bEnabled = ShouldBeEnabled(bParentEnabled);
    const ESlateDrawEffect effects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
    const FLinearColor styleTint = widgetStyle.GetColorAndOpacityTint();

    // The groove
    FSlateDrawElement::MakeBox(outDrawElements, layerId, allottedGeometry.ToPaintGeometry(
        FVector2f(barLength, Style->BarThickness), FSlateLayoutTransform(FVector2f(barStartX,
        sliderY - 0.5f * Style->BarThickness))), barImage, effects, barImage->GetTint(widgetStyle)
        * GetSliderBarColorAttribute().Get().GetColor(widgetStyle) * styleTint);

    // The fill: from the groove's start to the knob's centre, centred in the groove
    const FSOLSliderFill fill = SOLSolidStyle::SliderFill(size.X, handleSize.X, bIndent, percent,
        SOL::SolidMenu::SLIDER_FILL_INSET_PX);
    mPaintedFill = fill;
    mPaintedBarLength = barLength;
    mHasPainted = true;
    ++layerId;
    if (fill.Width > 0.0f)
    {
        const float thickness = mFillBrush.ImageSize.Y;
        const FVector2f fillTopLeft(fill.StartX, sliderY - 0.5f * thickness);
        FSlateDrawElement::MakeBox(outDrawElements, layerId, allottedGeometry.ToPaintGeometry(
            FVector2f(fill.Width, thickness), FSlateLayoutTransform(fillTopLeft)), &mFillBrush, effects,
            mFillBrush.GetTint(widgetStyle) * styleTint);
    }

    // The knob
    ++layerId;
    FSlateDrawElement::MakeBox(outDrawElements, layerId, allottedGeometry.ToPaintGeometry(handleSize,
        FSlateLayoutTransform(handleTopLeft)), thumbImage, effects, thumbImage->GetTint(widgetStyle)
        * GetSliderHandleColorAttribute().Get().GetColor(widgetStyle) * styleTint);
    return layerId;
}

//////////////////////////////////////////////////////////////////////////
// Sets the fill brush and repaints
void SSOLSolidSlider::SetFillBrush(const FSlateBrush& brush)
{
    mFillBrush = brush;
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Returns the fill of the last paint and the bar's length then
bool SSOLSolidSlider::GetPaintedFill(FSOLSliderFill& outFill, float& outBarLength) const
{
    outFill = mPaintedFill;
    outBarLength = mPaintedBarLength;
    return mHasPainted;
}

//////////////////////////////////////////////////////////////////////////
// Starts with the cyan fill brush
USOLSolidSlider::USOLSolidSlider(const FObjectInitializer& objectInitializer)
    : Super(objectInitializer)
    , FillBrush(SOLSolidStyle::MakeSliderFillBrush())
{
}

//////////////////////////////////////////////////////////////////////////
// Gives the slider the solid groove and knob, white bar and handle colours and the cyan fill brush
void USOLSolidSlider::SetSolidStyle(const USOLSolidMenuSubsystem* subsystem)
{
    SetWidgetStyle(SOLSolidStyle::MakeSliderStyle(subsystem));
    SetSliderBarColor(FLinearColor::White);
    SetSliderHandleColor(FLinearColor::White);
    FillBrush = SOLSolidStyle::MakeSliderFillBrush();
    if (mSolidSlider.IsValid())
    {
        mSolidSlider->SetFillBrush(FillBrush);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the fill of the last paint and the bar's length then
bool USOLSolidSlider::GetPaintedFill(FSOLSliderFill& outFill, float& outBarLength) const
{
    outFill = FSOLSliderFill();
    outBarLength = 0.0f;
    return mSolidSlider.IsValid() && mSolidSlider->GetPaintedFill(outFill, outBarLength);
}

//////////////////////////////////////////////////////////////////////////
// Builds the SSOLSolidSlider with USlider::RebuildWidget's arguments and hands it the fill brush
TSharedRef<SWidget> USOLSolidSlider::RebuildWidget()
{
    mSolidSlider = SNew(SSOLSolidSlider)
        .Style(&GetWidgetStyle())
        .IsFocusable(IsFocusable)
        .OnMouseCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureBegin))
        .OnMouseCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnMouseCaptureEnd))
        .OnControllerCaptureBegin(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureBegin))
        .OnControllerCaptureEnd(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleOnControllerCaptureEnd))
        .OnValueChanged(BIND_UOBJECT_DELEGATE(FOnFloatValueChanged, HandleOnValueChanged))
        .PreventThrottling(bPreventThrottling);
    mSolidSlider->SetFillBrush(FillBrush);
    MySlider = mSolidSlider;
    return mSolidSlider.ToSharedRef();
}

//////////////////////////////////////////////////////////////////////////
// Drops the Slate widget with the base class's
void USOLSolidSlider::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mSolidSlider.Reset();
}
