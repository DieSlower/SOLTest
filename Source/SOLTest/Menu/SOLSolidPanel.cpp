/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidPanel.h"

#include "Menu/SOLSolidMenuSubsystem.h"
#include "UI/SOLHoloMenuMath.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

namespace
{
    constexpr float PANEL_HULL_SIZE = 256.0f;    // Nominal hull brush size (the brush stretches to the panel)

    // Where each rivet sits (an overlay slot alignment), in mRivetImages order
    constexpr EHorizontalAlignment PANEL_RIVET_H[] = { HAlign_Left, HAlign_Right, HAlign_Right, HAlign_Left };
    constexpr EVerticalAlignment PANEL_RIVET_V[] = { VAlign_Top, VAlign_Top, VAlign_Bottom, VAlign_Bottom };

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a hull has a feature the shared style material cannot draw (a step, notches or a chin)
    bool PanelHasUniqueShape(const FSOLSolidHull& hull)
    {
        return (hull.TitleStepWidth > 0.0f && hull.TitleStepHeight > 0.0f) || hull.NotchY >= 0.0f
            || (hull.ChinWidth > 0.0f && hull.ChinHeight > 0.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a texture brush of a decor texture tinted with a palette colour (empty when the texture is missing)
    FSlateBrush PanelDecorBrush(UTexture2D* texture, const FVector2f& size, const FColor& tint)
    {
        FSlateBrush brush;
        if (texture != nullptr)
        {
            brush.SetResourceObject(texture);
            brush.TintColor = FSlateColor(SOLSolidStyle::Linear(tint));
        }
        else
        {
            brush.DrawAs = ESlateBrushDrawType::NoDrawType;
        }
        brush.SetImageSize(size);
        return brush;
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates a recess (the recess chamfers, the shared Recess material) around a widget with a padding
USOLSolidPanel* USOLSolidPanel::MakeRecess(UWidgetTree& tree, UWidget* content, const float padding)
{
    using namespace SOL::SolidMenu;
    USOLSolidPanel* recess = tree.ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass());
    FSOLSolidHull hull;
    hull.ChamferTL = RECESS_CHAMFER_LARGE_PX;
    hull.ChamferTR = RECESS_CHAMFER_SMALL_PX;
    hull.ChamferBR = RECESS_CHAMFER_LARGE_PX;
    hull.ChamferBL = RECESS_CHAMFER_SMALL_PX;
    hull.bRecess = true;
    recess->SetHull(ESOLSolidStyle::Recess, hull);
    recess->SetContentPadding(FMargin(padding));
    recess->SetContent(content);
    return recess;
}

//////////////////////////////////////////////////////////////////////////
// Returns the big hull panels' corner chamfers (SDD 46 section 5.2) with no other feature
FSOLSolidHull USOLSolidPanel::MakePanelHull()
{
    using namespace SOL::SolidMenu;
    FSOLSolidHull hull;
    hull.ChamferTL = PANEL_CHAMFER_TL;
    hull.ChamferTR = PANEL_CHAMFER_TR;
    hull.ChamferBR = PANEL_CHAMFER_BR;
    hull.ChamferBL = PANEL_CHAMFER_BL;
    return hull;
}

//////////////////////////////////////////////////////////////////////////
// Sets the hull's style and shape, writing a unique shape into the panel's own material once
void USOLSolidPanel::SetHull(const ESOLSolidStyle style, const FSOLSolidHull& hull)
{
    mStyle = style;
    mHull = hull;
    RefreshHullBrush();
}

//////////////////////////////////////////////////////////////////////////
// Sets the padding between the hull's edge and the content
void USOLSolidPanel::SetContentPadding(const FMargin& padding)
{
    mContentPadding = padding;
    if (mContentBox.IsValid())
    {
        mContentBox->SetPadding(mContentPadding);
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides the rivets and the vent strip
void USOLSolidPanel::SetDecor(const bool bRivets, const bool bVents)
{
    mHasRivets = bRivets;
    mHasVents = bVents;
    RefreshDecor();
}

//////////////////////////////////////////////////////////////////////////
// Applies the open look: the slide offset along the direction and the fade, only when they changed
void USOLSolidPanel::ApplyOpenLook(const float progress, const FVector2f& slideDirection)
{
    const float p = FMath::IsNaN(progress) ? 1.0f : FMath::Clamp(progress, 0.0f, 1.0f);
    const FVector2f offset = slideDirection.GetSafeNormal() * SOLSolidMenu::PanelSlide(p);
    const float opacity = SOLHoloMenu::FillFade(p);
    if (!offset.Equals(mAppliedOffset, SOL::SolidMenu::LIFT_EPSILON_PX))
    {
        mAppliedOffset = offset;
        SetRenderTranslation(FVector2D(offset));
    }
    if (opacity != mAppliedOpacity)
    {
        mAppliedOpacity = opacity;
        SetRenderOpacity(opacity);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the static traces (kept until the trace layer exists)
void USOLSolidPanel::SetTraces(TConstArrayView<FSOLSolidTrace> traces)
{
    mPendingTraces.Reset(traces.Num());
    mPendingTraces.Append(traces.GetData(), traces.Num());
    if (mTraceLayer.IsValid())
    {
        mTraceLayer->SetTraces(mPendingTraces);
    }
}

//////////////////////////////////////////////////////////////////////////
// Feeds the trace layer its power-up fraction, idle pulse and strength
void USOLSolidPanel::SetTraceLook(const float power, const float pulse, const float strength)
{
    if (mTraceLayer.IsValid())
    {
        mTraceLayer->SetPower(power);
        mTraceLayer->SetPulse(pulse);
        mTraceLayer->SetStrength(strength);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the hull draws with a material
bool USOLSolidPanel::IsHullMaterialDrawn() const
{
    return Cast<UMaterialInterface>(HullBrush.GetResourceObject()) != nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Drops the Slate widgets
void USOLSolidPanel::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mOverlay.Reset();
    mHullImage.Reset();
    mContentBox.Reset();
    mTraceLayer.Reset();
    for (TSharedPtr<SImage>& rivet : mRivetImages)
    {
        rivet.Reset();
    }
    mVentImage.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Builds the overlay: hull image, decor, content box, trace layer
TSharedRef<SWidget> USOLSolidPanel::RebuildWidget()
{
    using namespace SOL::SolidMenu;
    RefreshHullBrush();
    const USOLSolidMenuSubsystem* subsystem = USOLSolidMenuSubsystem::Get(this);
    RivetBrush = PanelDecorBrush(subsystem != nullptr ? subsystem->GetTexture(ESOLSolidTexture::Rivet) : nullptr,
        FVector2f(RIVET_SIZE_PX, RIVET_SIZE_PX), RIVET_COLOR);
    VentBrush = PanelDecorBrush(subsystem != nullptr ? subsystem->GetTexture(ESOLSolidTexture::Vent) : nullptr,
        FVector2f(VENT_WIDTH_PX, VENT_HEIGHT_PX), LED_OFF_COLOR);

    mOverlay = SNew(SOverlay);
    // The hull takes the panel's size: no desired size of its own (the content decides)
    mOverlay->AddSlot()[SAssignNew(mHullImage, SImage).Image(&HullBrush).DesiredSizeOverride(FVector2D::ZeroVector)];
    for (int32 index = 0; index < RIVET_COUNT; ++index)
    {
        mOverlay->AddSlot().HAlign(PANEL_RIVET_H[index]).VAlign(PANEL_RIVET_V[index]).Padding(RIVET_INSET_PX)
            [SAssignNew(mRivetImages[index], SImage).Image(&RivetBrush)];
    }
    mOverlay->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Top)
        .Padding(FMargin(0.0f, VENT_INSET_PX, VENT_INSET_PX, 0.0f))[SAssignNew(mVentImage, SImage).Image(&VentBrush)];
    mOverlay->AddSlot()[SAssignNew(mContentBox, SBox).Padding(mContentPadding)];
    mOverlay->AddSlot()[SAssignNew(mTraceLayer, SSOLSolidTraceLayer)];
    if (GetChildrenCount() > 0 && GetContentSlot() != nullptr && GetContentSlot()->Content != nullptr)
    {
        mContentBox->SetContent(GetContentSlot()->Content->TakeWidget());
    }
    mTraceLayer->SetTraces(mPendingTraces);
    RefreshDecor();
    return mOverlay.ToSharedRef();
}

//////////////////////////////////////////////////////////////////////////
// Applies the padding and decor to the built widgets
void USOLSolidPanel::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    if (mContentBox.IsValid())
    {
        mContentBox->SetPadding(mContentPadding);
    }
    RefreshDecor();
}

//////////////////////////////////////////////////////////////////////////
// The content slot is a plain UPanelSlot
UClass* USOLSolidPanel::GetSlotClass() const
{
    return UPanelSlot::StaticClass();
}

//////////////////////////////////////////////////////////////////////////
// Puts an added child's widget into the content box
void USOLSolidPanel::OnSlotAdded(UPanelSlot* slot)
{
    if (mContentBox.IsValid() && slot != nullptr && slot->Content != nullptr)
    {
        mContentBox->SetContent(slot->Content->TakeWidget());
    }
}

//////////////////////////////////////////////////////////////////////////
// Empties the content box
void USOLSolidPanel::OnSlotRemoved(UPanelSlot* /*slot*/)
{
    if (mContentBox.IsValid())
    {
        mContentBox->SetContent(SNullWidget::NullWidget);
    }
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the hull brush: the style's shared material, the panel's own one for a unique shape (its shape written
// here, once per SetHull; recreated when the style changed), or the flat fallback
void USOLSolidPanel::RefreshHullBrush()
{
    using namespace SOL::SolidMenuMaterialParams;
    USOLSolidMenuSubsystem* subsystem = USOLSolidMenuSubsystem::Get(this);
    const FVector2f size(PANEL_HULL_SIZE, PANEL_HULL_SIZE);
    HullBrush = SOLSolidStyle::MakeHullBrush(subsystem, mStyle, size, FSOLSolidButtonLook());
    if (subsystem == nullptr || !subsystem->HasStyleMaterial(mStyle) || !PanelHasUniqueShape(mHull))
    {
        UniqueMaterial = nullptr;
    }
    else
    {
        // A different style needs a material of that style (its parent instance carries the style's colours)
        if (UniqueMaterial == nullptr || mUniqueMaterialStyle != mStyle)
        {
            UniqueMaterial = subsystem->CreateUniqueMaterial(mStyle, this);
            mUniqueMaterialStyle = mStyle;
        }
        if (UniqueMaterial != nullptr)
        {
            // The material clamps the shape for its size (SOLSolidMenu::ClampHull), so the raw values go in
            const TPair<const TCHAR*, float> shape[] = { { CHAMFER_TL, mHull.ChamferTL },
                { CHAMFER_TR, mHull.ChamferTR }, { CHAMFER_BR, mHull.ChamferBR }, { CHAMFER_BL, mHull.ChamferBL },
                { TITLE_STEP_WIDTH, mHull.TitleStepWidth }, { TITLE_STEP_HEIGHT, mHull.TitleStepHeight },
                { NOTCH_Y, mHull.NotchY }, { NOTCH_DEPTH, mHull.NotchDepth }, { NOTCH_HEIGHT, mHull.NotchHeight },
                { CHIN_WIDTH, mHull.ChinWidth }, { CHIN_HEIGHT, mHull.ChinHeight },
                { RECESS, mHull.bRecess ? 1.0f : 0.0f } };
            for (const TPair<const TCHAR*, float>& parameter : shape)
            {
                UniqueMaterial->SetScalarParameterValue(parameter.Key, parameter.Value);
            }
            HullBrush.SetResourceObject(UniqueMaterial);
        }
    }
    if (mHullImage.IsValid())
    {
        mHullImage->Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides the built rivet and vent images
void USOLSolidPanel::RefreshDecor()
{
    for (const TSharedPtr<SImage>& rivet : mRivetImages)
    {
        if (rivet.IsValid())
        {
            rivet->SetVisibility(mHasRivets ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
        }
    }
    if (mVentImage.IsValid())
    {
        mVentImage->SetVisibility(mHasVents ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
    }
}
