/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuButton.h"

#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"

namespace
{
    // Highlight states RefreshHighlight distinguishes
    constexpr int32 MENU_BUTTON_NORMAL = 0;
    constexpr int32 MENU_BUTTON_SELECTED = 1;
    constexpr int32 MENU_BUTTON_FOCUSED = 2;

    // Padding inside a button around its label
    const FMargin MENU_BUTTON_PADDING(18.0f, 8.0f);
}

//////////////////////////////////////////////////////////////////////////
// Styles the button, creates its label in the owner's widget tree and routes clicks and hovers
void USOLMenuButton::Setup(UWidgetTree& tree, const FString& label, const int32 index, const int32 fontSize)
{
    using namespace SOL::MenuStyle;
    mIndex = index;

    // White rounded brushes tinted through the background colour (which RefreshHighlight sets per state), so one style
    // serves every highlight; hovering also focuses, so only the press needs its own (brighter) brush
    const float radius = BUTTON_CORNER_RADIUS;
    FButtonStyle style;
    style.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, radius));
    style.SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, radius));
    style.SetPressed(FSlateRoundedBoxBrush(BUTTON_PRESSED_TINT, radius));
    style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor::White, radius));
    style.SetNormalPadding(MENU_BUTTON_PADDING);
    style.SetPressedPadding(MENU_BUTTON_PADDING);
    SetStyle(style);
    SetBackgroundColor(BUTTON_NORMAL);

    Label = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Label->SetFont(FCoreStyle::GetDefaultFontStyle(FONT_REGULAR, fontSize));
    Label->SetColorAndOpacity(FSlateColor(TEXT_COLOR));
    Label->SetText(FText::FromString(label));
    SetContent(Label);

    OnClicked.AddUniqueDynamic(this, &USOLMenuButton::HandleClicked);
    OnHovered.AddUniqueDynamic(this, &USOLMenuButton::HandleHovered);
    mLastHighlight = MENU_BUTTON_NORMAL;
}

//////////////////////////////////////////////////////////////////////////
// Replaces the label text
void USOLMenuButton::SetLabel(const FString& label)
{
    if (Label != nullptr)
    {
        Label->SetText(FText::FromString(label));
    }
}

//////////////////////////////////////////////////////////////////////////
// Recolours the button for keyboard focus, selection or neither (cheap; skips unchanged states)
void USOLMenuButton::RefreshHighlight()
{
    using namespace SOL::MenuStyle;
    const int32 highlight = HasKeyboardFocus() ? MENU_BUTTON_FOCUSED
        : (mIsSelected ? MENU_BUTTON_SELECTED : MENU_BUTTON_NORMAL);
    if (highlight == mLastHighlight)
    {
        return;
    }
    mLastHighlight = highlight;
    SetBackgroundColor(highlight == MENU_BUTTON_FOCUSED ? BUTTON_FOCUSED
        : (highlight == MENU_BUTTON_SELECTED ? BUTTON_SELECTED : BUTTON_NORMAL));
    if (Label != nullptr)
    {
        Label->SetColorAndOpacity(FSlateColor(highlight == MENU_BUTTON_NORMAL ? TEXT_COLOR : ACCENT_COLOR));
    }
}

//////////////////////////////////////////////////////////////////////////
// UButton click: forwards the index to OnMenuClicked
void USOLMenuButton::HandleClicked()
{
    OnMenuClicked.ExecuteIfBound(mIndex);
}

//////////////////////////////////////////////////////////////////////////
// UButton hover: takes keyboard focus so the highlight follows the mouse
void USOLMenuButton::HandleHovered()
{
    SetKeyboardFocus();
}
