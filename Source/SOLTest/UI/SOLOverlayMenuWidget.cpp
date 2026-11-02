/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlayMenuWidget.h"

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Fonts and sizes (Slate's default family; the rows in mono so the check marks line up)
    const TCHAR* const OVERLAY_MENU_FONT_REGULAR = TEXT("Regular");
    const TCHAR* const OVERLAY_MENU_FONT_MONO = TEXT("Mono");
    constexpr int32 OVERLAY_MENU_TITLE_SIZE = 16;
    constexpr int32 OVERLAY_MENU_ROW_SIZE = 14;
    constexpr int32 OVERLAY_MENU_HINT_SIZE = 11;
    constexpr float OVERLAY_MENU_PADDING = 18.0f;
    constexpr float OVERLAY_MENU_ROW_PADDING = 3.0f;
    constexpr float OVERLAY_MENU_LEFT_INSET = 60.0f;        // Standalone panel inset from the screen's left edge

    // Hologram palette (like the F3 panel): Primary-tinted text on a dark translucent panel, the highlight in
    // Secondary, an unchecked or gated row dimmer
    const FLinearColor OVERLAY_MENU_BACKGROUND(0.01f, 0.03f, 0.06f, 0.78f);
    const FLinearColor OVERLAY_MENU_TEXT(0.75f, 0.9f, 1.0f, 1.0f);
    const FLinearColor OVERLAY_MENU_DIM_TEXT(0.35f, 0.47f, 0.58f, 1.0f);
    const FLinearColor OVERLAY_MENU_ROW_HIGHLIGHT(0.2f, 0.7f, 1.0f, 0.12f);

    //////////////////////////////////////////////////////////////////////////
    // Creates a text block in a font face, size and colour
    UTextBlock* OverlayMenuMakeText(UWidgetTree& tree, const FString& text, const TCHAR* face, const int32 size,
        const FLinearColor& color)
    {
        UTextBlock* block = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        block->SetFont(FCoreStyle::GetDefaultFontStyle(face, size));
        block->SetColorAndOpacity(FSlateColor(color));
        block->SetText(FText::FromString(text));
        return block;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree: a panel with the title, one bordered text row per overlay and the key hint
void USOLOverlayMenuWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    Settings = USOLSettingsSubsystem::Get(this);

    // A transparent full-screen root holding the panel at the left, so the world stays visible and the toggles
    // preview live (SetEmbedded strips the root's inset and the panel's backing for the Settings page)
    RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    RootBorder->SetBrushColor(FLinearColor::Transparent);
    RootBorder->SetHorizontalAlignment(HAlign_Left);
    RootBorder->SetVerticalAlignment(VAlign_Center);
    RootBorder->SetPadding(FMargin(OVERLAY_MENU_LEFT_INSET, 0.0f, 0.0f, 0.0f));
    WidgetTree->RootWidget = RootBorder;
    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    PanelBorder->SetBrushColor(OVERLAY_MENU_BACKGROUND);
    PanelBorder->SetPadding(FMargin(OVERLAY_MENU_PADDING));
    RootBorder->SetContent(PanelBorder);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    PanelBorder->SetContent(column);
    TitleText = OverlayMenuMakeText(*WidgetTree, FString(), OVERLAY_MENU_FONT_REGULAR, OVERLAY_MENU_TITLE_SIZE,
        OVERLAY_MENU_TEXT);
    column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, OVERLAY_MENU_PADDING * 0.5f));

    // One row per table entry, in table order
    RowBorders.Reset(SOLOverlay::OVERLAY_COUNT);
    RowTexts.Reset(SOLOverlay::OVERLAY_COUNT);
    for (int32 row = 0; row < SOLOverlay::OVERLAY_COUNT; ++row)
    {
        UBorder* border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        border->SetBrushColor(FLinearColor::Transparent);
        border->SetPadding(FMargin(OVERLAY_MENU_ROW_PADDING * 2.0f, OVERLAY_MENU_ROW_PADDING));
        UTextBlock* text = OverlayMenuMakeText(*WidgetTree, FString(), OVERLAY_MENU_FONT_MONO, OVERLAY_MENU_ROW_SIZE,
            OVERLAY_MENU_TEXT);
        border->SetContent(text);
        column->AddChildToVerticalBox(border);
        RowBorders.Add(border);
        RowTexts.Add(text);
    }
    HintText = OverlayMenuMakeText(*WidgetTree, FString(), OVERLAY_MENU_FONT_REGULAR,
        OVERLAY_MENU_HINT_SIZE, OVERLAY_MENU_DIM_TEXT);
    column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.0f, OVERLAY_MENU_PADDING * 0.5f, 0.0f, 0.0f));
    RefreshHint();
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Makes the widget the Settings page's embedded list (no inset, backing, title or close keys, not focusable)
void USOLOverlayMenuWidget::SetEmbedded(const bool bEmbedded)
{
    mIsEmbedded = bEmbedded;
    SetIsFocusable(!bEmbedded);
    if (RootBorder != nullptr)
    {
        RootBorder->SetPadding(FMargin(bEmbedded ? 0.0f : OVERLAY_MENU_LEFT_INSET, 0.0f, 0.0f, 0.0f));
        PanelBorder->SetBrushColor(bEmbedded ? FLinearColor::Transparent : OVERLAY_MENU_BACKGROUND);
        PanelBorder->SetPadding(FMargin(bEmbedded ? 0.0f : OVERLAY_MENU_PADDING));
        TitleText->SetVisibility(bEmbedded ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
    RefreshHint();
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Writes the key hint from the current bindings (click only in the embedded list, which takes no keys)
void USOLOverlayMenuWidget::RefreshHint()
{
    if (HintText != nullptr)
    {
        HintText->SetText(FText::FromString(SOLKeyBindings::FormatKeyText(mIsEmbedded
            ? SOLMenuText::OVERLAY_MENU_HINT_EMBEDDED : SOLMenuText::OVERLAY_MENU_HINT,
            Settings != nullptr ? Settings->GetAppliedKeys() : SOLKeyBindings::DefaultKeys())));
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows the standalone menu with its title and remembers the owner's close delegate and toggle key
void USOLOverlayMenuWidget::Open(const FSimpleDelegate& close, const FKey& toggleKey, const FString& title)
{
    mClose = close;
    mToggleKey = toggleKey;
    if (TitleText != nullptr)
    {
        TitleText->SetText(FText::FromString(title));
    }
    RefreshHint();
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Re-reads every row: its check mark, its indent under its parent row and its colour (highlight, unchecked, gated)
void USOLOverlayMenuWidget::RefreshRows()
{
    const SOLOverlay::FOverlayPrefs prefs = Settings != nullptr ? Settings->GetOverlayPrefs()
        : SOLOverlay::MakeDefaultPrefs();
    for (int32 row = 0; row < RowTexts.Num(); ++row)
    {
        const SOLOverlay::FOverlayDef& def = SOLOverlay::GetDef(row);
        const bool bChecked = SOLOverlay::IsEnabled(prefs, static_cast<SOLOverlay::EOverlay>(row));
        const bool bGated = def.Parent != INDEX_NONE
            && !SOLOverlay::IsEnabled(prefs, static_cast<SOLOverlay::EOverlay>(def.Parent));
        FString text = def.Parent != INDEX_NONE ? SOLMenuText::OVERLAY_MENU_CHILD_INDENT : FString();
        text += bChecked ? SOLMenuText::OVERLAY_MENU_CHECKED : SOLMenuText::OVERLAY_MENU_UNCHECKED;
        text += def.Label;
        RowTexts[row]->SetText(FText::FromString(text));
        const bool bHighlighted = row == mHighlightedRow && !mIsEmbedded;
        RowTexts[row]->SetColorAndOpacity(FSlateColor(bHighlighted ? SOL::HudPalette::SECONDARY
            : (bChecked && !bGated ? OVERLAY_MENU_TEXT : OVERLAY_MENU_DIM_TEXT)));
        RowBorders[row]->SetBrushColor(bHighlighted ? OVERLAY_MENU_ROW_HIGHLIGHT : FLinearColor::Transparent);
    }
}

//////////////////////////////////////////////////////////////////////////
// Toggles one row through the settings (applied live) and refreshes
void USOLOverlayMenuWidget::ToggleRow(const int32 row)
{
    if (Settings == nullptr || row < 0 || row >= SOLOverlay::OVERLAY_COUNT)
    {
        return;
    }
    Settings->ToggleOverlay(static_cast<SOLOverlay::EOverlay>(row));
    mHighlightedRow = row;
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Moves the highlight (wrapping) and refreshes
void USOLOverlayMenuWidget::MoveHighlight(const int32 step)
{
    const int32 count = FMath::Max(RowTexts.Num(), 1);
    mHighlightedRow = (mHighlightedRow + step + count) % count;
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Returns a row's centre in absolute (screen) Slate space as last laid out, or zero
FVector2D USOLOverlayMenuWidget::GetRowScreenCenter(const int32 row) const
{
    if (!RowBorders.IsValidIndex(row))
    {
        return FVector2D::ZeroVector;
    }
    const FGeometry& geometry = RowBorders[row]->GetCachedGeometry();
    return FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * 0.5f));
}

//////////////////////////////////////////////////////////////////////////
// Returns the row under a screen-space point, or INDEX_NONE
int32 USOLOverlayMenuWidget::FindRowAt(const FVector2D& screenPosition) const
{
    for (int32 row = 0; row < RowBorders.Num(); ++row)
    {
        if (RowBorders[row]->GetCachedGeometry().IsUnderLocation(screenPosition))
        {
            return row;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Up / Down move the highlight, Space / Enter toggle, Esc and Ctrl+<toggle key> close
FReply USOLOverlayMenuWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    const FKey key = keyEvent.GetKey();
    if (key == EKeys::Up)
    {
        MoveHighlight(-1);
        return FReply::Handled();
    }
    if (key == EKeys::Down)
    {
        MoveHighlight(1);
        return FReply::Handled();
    }
    if (key == EKeys::SpaceBar || key == EKeys::Enter)
    {
        ToggleRow(mHighlightedRow);
        return FReply::Handled();
    }
    if (!mIsEmbedded && (key == EKeys::Escape || (key == mToggleKey && keyEvent.IsControlDown())))
    {
        mClose.ExecuteIfBound();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(geometry, keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// A left click on a row toggles it
FReply USOLOverlayMenuWidget::NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    if (mouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const int32 row = FindRowAt(mouseEvent.GetScreenSpacePosition());
        if (row != INDEX_NONE)
        {
            ToggleRow(row);
            return FReply::Handled();
        }
    }
    return Super::NativeOnMouseButtonDown(geometry, mouseEvent);
}

//////////////////////////////////////////////////////////////////////////
// The row under the mouse is highlighted
FReply USOLOverlayMenuWidget::NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    const int32 row = FindRowAt(mouseEvent.GetScreenSpacePosition());
    if (row != INDEX_NONE && row != mHighlightedRow)
    {
        mHighlightedRow = row;
        RefreshRows();
    }
    return Super::NativeOnMouseMove(geometry, mouseEvent);
}
