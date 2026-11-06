/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidTabRail.h"

#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBoxSlot.h"

//////////////////////////////////////////////////////////////////////////
// A tab's hover trace in rail local units
FSOLSolidTrace SOLSolidTabRail::TraceForTab(const int32 index)
{
    using namespace SOL::MenuStyle;
    using namespace SOL::SolidMenu;
    FSOLSolidTrace trace;
    const float y = SETTINGS_TAB_RAIL_PADDING + FMath::Max(index, 0) * (TAB_HEIGHT + BUTTON_GAP) + 0.5f * TAB_HEIGHT;
    trace.Points[0] = FVector2f(TAB_COLUMN_WIDTH - SETTINGS_TAB_RAIL_PADDING - SETTINGS_TAB_TRACE_PX, y);
    trace.Points[1] = FVector2f(TAB_COLUMN_WIDTH - SETTINGS_TAB_TRACE_END_PX, y);
    trace.Count = 2;
    trace.bRingTerminal = false;
    return trace;
}

//////////////////////////////////////////////////////////////////////////
// The height a rail of this many tabs needs
float SOLSolidTabRail::RailHeight(const int32 tabCount)
{
    using namespace SOL::MenuStyle;
    return 2.0f * SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING + FMath::Max(tabCount, 0) * (TAB_HEIGHT + BUTTON_GAP);
}

//////////////////////////////////////////////////////////////////////////
// Builds the rail: a recess holding one Tab button per label, each leaving SETTINGS_TAB_TRACE_PX on its right
USizeBox* FSOLSolidTabRail::Build(UWidgetTree& tree, const TArray<FString>& labels,
    const FSOLMenuButtonClicked& onClicked)
{
    using namespace SOL::MenuStyle;
    using namespace SOL::SolidMenu;
    // The tabs scroll inside the recess padding (the view's top is the padding in the trace layer's units)
    UWidget* tabs = Stack.Build(tree, TAB_HEIGHT, BUTTON_GAP, SETTINGS_TAB_RAIL_PADDING);
    Panel = USOLSolidPanel::MakeRecess(tree, tabs, SETTINGS_TAB_RAIL_PADDING);
    const float tabWidth = TAB_COLUMN_WIDTH - 2.0f * SETTINGS_TAB_RAIL_PADDING - SETTINGS_TAB_TRACE_PX;
    Buttons.Reset(labels.Num());
    Traces.Reset();
    Traces.Reserve(labels.Num());
    for (int32 index = 0; index < labels.Num(); ++index)
    {
        USOLMenuButton* button = tree.ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(tree, labels[index], index, TAB_FONT_SIZE, ESOLSolidButtonVariant::Tab);
        button->SetLabelLetterSpacing(SETTINGS_TAB_LETTER_SPACING);
        button->OnMenuClicked = onClicked;
        Buttons.Add(button);
        USizeBox* box = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        box->SetWidthOverride(tabWidth);
        box->SetHeightOverride(TAB_HEIGHT);
        box->SetContent(button);
        Stack.AddItem(box)->SetHorizontalAlignment(HAlign_Left);
        Traces.Add(SOLSolidTabRail::TraceForTab(index));
    }
    USizeBox* column = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
    column->SetWidthOverride(TAB_COLUMN_WIDTH);
    column->SetContent(Panel);
    Strength = -1.0f;
    return column;
}

//////////////////////////////////////////////////////////////////////////
// Binds each button's hover trace to the rail's trace layer
void FSOLSolidTabRail::Bind()
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = Panel != nullptr ? Panel->GetTraceLayer() : nullptr;
    for (int32 index = 0; index < Buttons.Num(); ++index)
    {
        Buttons[index]->SetHoverTrace(layer, Traces[index]);
    }
    Strength = -1.0f;
    Stack.ResetPushed();
}

//////////////////////////////////////////////////////////////////////////
// Shows a tab as selected and scrolls it whole into view (a scrolling rail; ScrollToBinding selects Key bindings)
void FSOLSolidTabRail::Select(const int32 index)
{
    for (int32 button = 0; button < Buttons.Num(); ++button)
    {
        Buttons[button]->SetSelected(button == index);
    }
    Stack.ShowItem(index);
}

//////////////////////////////////////////////////////////////////////////
// Pushes the effects strength when it changed, refreshes every button's highlight, then runs the stack (it reads the
// focus the refresh found)
void FSOLSolidTabRail::Tick(const FSOLSolidMenuFrame& frame, const bool bHoverElsewhere)
{
    if (frame.Strength != Strength && Panel != nullptr && Panel->GetTraceLayer().IsValid())
    {
        Strength = frame.Strength;
        Panel->SetTraceLook(1.0f, 0.0f, frame.Strength);
    }
    for (USOLMenuButton* button : Buttons)
    {
        button->RefreshHighlightAt(frame.NowS, frame.Strength, bHoverElsewhere);
    }
    Stack.Tick(Panel != nullptr ? Panel->GetTraceLayer() : nullptr, Buttons);
}

//////////////////////////////////////////////////////////////////////////
// Scrolls the tabs to the top
void FSOLSolidTabRail::ScrollToTop()
{
    Stack.ScrollToTop();
}

//////////////////////////////////////////////////////////////////////////
// Scrolls the tabs to the end
void FSOLSolidTabRail::ScrollToEnd()
{
    Stack.ScrollToEnd();
}

//////////////////////////////////////////////////////////////////////////
// Clears the hover trace left from the last visit, unless a tab still has the keyboard focus
void FSOLSolidTabRail::ClearStaleHover()
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = Panel != nullptr ? Panel->GetTraceLayer() : nullptr;
    const bool bTabFocused = Buttons.ContainsByPredicate([](const USOLMenuButton* button)
    {
        return button != nullptr && button->HasKeyboardFocus();
    });
    if (layer.IsValid() && !bTabFocused)
    {
        layer->ClearHoverTrace();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's button, or null
USOLMenuButton* FSOLSolidTabRail::GetButton(const int32 index) const
{
    return Buttons.IsValidIndex(index) ? Buttons[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the rail draws a tab's hover trace
bool FSOLSolidTabRail::IsTraceShown(const int32 index) const
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = Panel != nullptr ? Panel->GetTraceLayer() : nullptr;
    return layer.IsValid() && Traces.IsValidIndex(index) && layer->IsShowingHoverTrace(Traces[index])
        && layer->GetHoverAmount() > 0.5f;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the rail draws any hover trace
bool FSOLSolidTabRail::IsAnyTraceShown() const
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = Panel != nullptr ? Panel->GetTraceLayer() : nullptr;
    return layer.IsValid() && layer->GetHoverAmount() > 0.0f;
}
