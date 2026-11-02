/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLActionListWidget.h"

#include "Interaction/SOLInteractionSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Fonts, sizes (Slate units) and the hologram palette, as the Overlay Menu
    const TCHAR* const ACTION_LIST_FONT = TEXT("Regular");
    constexpr int32 ACTION_LIST_TITLE_SIZE = 15;
    constexpr int32 ACTION_LIST_ROW_SIZE = 13;
    constexpr int32 ACTION_LIST_HINT_SIZE = 10;
    constexpr float ACTION_LIST_PADDING = 12.0f;
    constexpr float ACTION_LIST_ROW_PADDING = 3.0f;
    constexpr float ACTION_LIST_ESTIMATED_WIDTH = 300.0f;   // Placement estimate before the first layout
    constexpr float ACTION_LIST_ESTIMATED_ROW = 23.0f;
    const FLinearColor ACTION_LIST_BACKGROUND(0.01f, 0.03f, 0.06f, 0.82f);
    const FLinearColor ACTION_LIST_TEXT(0.75f, 0.9f, 1.0f, 1.0f);
    const FLinearColor ACTION_LIST_DIM_TEXT(0.35f, 0.47f, 0.58f, 1.0f);
    const FLinearColor ACTION_LIST_ROW_HIGHLIGHT(0.2f, 0.7f, 1.0f, 0.12f);

    //////////////////////////////////////////////////////////////////////////
    // Creates a text block in a size and colour
    UTextBlock* ActionListMakeText(UWidgetTree& tree, const int32 size, const FLinearColor& color)
    {
        UTextBlock* block = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        block->SetFont(FCoreStyle::GetDefaultFontStyle(ACTION_LIST_FONT, size));
        block->SetColorAndOpacity(FSlateColor(color));
        return block;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree: a full-screen transparent root, the panel with the title, MAX_ACTIONS rows and the hint
void USOLActionListWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    RootBorder->SetBrushColor(FLinearColor::Transparent);
    RootBorder->SetHorizontalAlignment(HAlign_Left);
    RootBorder->SetVerticalAlignment(VAlign_Top);
    WidgetTree->RootWidget = RootBorder;
    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    PanelBorder->SetBrushColor(ACTION_LIST_BACKGROUND);
    PanelBorder->SetPadding(FMargin(ACTION_LIST_PADDING));
    RootBorder->SetContent(PanelBorder);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    PanelBorder->SetContent(column);
    TitleText = ActionListMakeText(*WidgetTree, ACTION_LIST_TITLE_SIZE, SOL::HudPalette::SECONDARY);
    column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ACTION_LIST_PADDING * 0.5f));

    // Every row the table can need, built once; Open shows the used ones
    RowBorders.Reset(SOL::Interact::MAX_ACTIONS);
    RowTexts.Reset(SOL::Interact::MAX_ACTIONS);
    for (int32 row = 0; row < SOL::Interact::MAX_ACTIONS; ++row)
    {
        UBorder* border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        border->SetBrushColor(FLinearColor::Transparent);
        border->SetPadding(FMargin(ACTION_LIST_ROW_PADDING * 2.0f, ACTION_LIST_ROW_PADDING));
        UTextBlock* text = ActionListMakeText(*WidgetTree, ACTION_LIST_ROW_SIZE, ACTION_LIST_TEXT);
        border->SetContent(text);
        column->AddChildToVerticalBox(border);
        RowBorders.Add(border);
        RowTexts.Add(text);
    }
    HintText = ActionListMakeText(*WidgetTree, ACTION_LIST_HINT_SIZE, ACTION_LIST_DIM_TEXT);
    HintText->SetText(FText::FromString(SOLMenuText::INTERACT_LIST_HINT));
    column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.0f, ACTION_LIST_PADDING * 0.5f, 0.0f, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Fills the list for an object and places it at the cursor
bool USOLActionListWidget::Open(USOLInteractionSubsystem* interaction, const FSOLObjectRef& object,
    const FVector2D& cursorPx, const FVector2D& viewportSizePx, const FSimpleDelegate& close)
{
    Interaction = interaction;
    mClose = close;
    mObject = object;
    if (Interaction == nullptr || RowTexts.IsEmpty() || !Interaction->BuildActions(object, mActions))
    {
        return false;
    }

    // Rows: the label, a disabled one with its reason; the rows beyond the table collapsed
    bool enabled[SOL::Interact::MAX_ACTIONS] = {};
    for (int32 row = 0; row < RowTexts.Num(); ++row)
    {
        const bool bUsed = row < mActions.Num;
        RowBorders[row]->SetVisibility(bUsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (!bUsed)
        {
            continue;
        }
        const FSOLActionEntry& entry = mActions.Entries[row];
        enabled[row] = entry.IsEnabled();
        FString text = SOLActionTable::ActionLabel(entry.Action);
        if (!entry.IsEnabled())
        {
            text += SOLMenuText::INTERACT_LIST_REASON_OPEN;
            text += SOLActionTable::BlockText(entry.Block);
            text += SOLMenuText::INTERACT_LIST_REASON_CLOSE;
        }
        RowTexts[row]->SetText(FText::FromString(text));
    }
    SOLListMenu::Open(mState, MakeArrayView(enabled, mActions.Num));

    // Title: the object's name, else its type
    FSOLObjectInfo info;
    const bool bHasInfo = Interaction->GetObjectInfo(object, info);
    TitleText->SetText(FText::FromString(bHasInfo && !info.Name.IsNone() ? info.Name.ToString()
        : FString(SOLActionTable::TypeName(Interaction->GetObjectType(object)))));
    RefreshRows();

    // At the cursor (viewport pixels -> Slate units), flipped and clamped to stay on screen
    const float viewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_KINDA_SMALL_NUMBER);
    const FVector2D panelPx(ACTION_LIST_ESTIMATED_WIDTH * viewportScale,
        (mActions.Num + 3) * ACTION_LIST_ESTIMATED_ROW * viewportScale);
    const FVector2D cornerPx = SOLListMenu::PlacePanel(cursorPx, panelPx, viewportSizePx,
        SOL::Interact::LIST_CURSOR_OFFSET_PX * viewportScale, SOL::Interact::LIST_MARGIN_PX * viewportScale);
    RootBorder->SetPadding(FMargin(cornerPx.X / viewportScale, cornerPx.Y / viewportScale, 0.0f, 0.0f));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Re-colours the rows: the highlight in Secondary on a faint band, a disabled row dim
void USOLActionListWidget::RefreshRows()
{
    for (int32 row = 0; row < mActions.Num && row < RowTexts.Num(); ++row)
    {
        const bool bHighlighted = row == mState.Highlighted;
        RowTexts[row]->SetColorAndOpacity(FSlateColor(bHighlighted ? SOL::HudPalette::SECONDARY
            : (mActions.Entries[row].IsEnabled() ? ACTION_LIST_TEXT : ACTION_LIST_DIM_TEXT)));
        RowBorders[row]->SetBrushColor(bHighlighted ? ACTION_LIST_ROW_HIGHLIGHT : FLinearColor::Transparent);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a row's action when it is enabled and closes the list
bool USOLActionListWidget::ConfirmRow(const int32 row)
{
    if (Interaction == nullptr || row < 0 || row >= mActions.Num || !mActions.Entries[row].IsEnabled())
    {
        return false;
    }
    // The action first, so the owner's close sees its result (a Jump To closes the map)
    const bool bDone = Interaction->Execute(mObject, mActions.Entries[row].Action);
    RequestClose();
    return bDone;
}

//////////////////////////////////////////////////////////////////////////
// Moves the highlight (wrapping) and refreshes
void USOLActionListWidget::MoveHighlight(const int32 step)
{
    SOLListMenu::MoveHighlight(mState, step);
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Asks the owner to close the list
void USOLActionListWidget::RequestClose()
{
    mClose.ExecuteIfBound();
}

//////////////////////////////////////////////////////////////////////////
// Returns a row's centre in absolute (screen) Slate space as last laid out, or zero
FVector2D USOLActionListWidget::GetRowScreenCenter(const int32 row) const
{
    if (!RowBorders.IsValidIndex(row))
    {
        return FVector2D::ZeroVector;
    }
    const FGeometry& geometry = RowBorders[row]->GetCachedGeometry();
    return FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * 0.5f));
}

//////////////////////////////////////////////////////////////////////////
// Returns the used row under a screen-space point, or INDEX_NONE
int32 USOLActionListWidget::FindRowAt(const FVector2D& screenPosition) const
{
    for (int32 row = 0; row < mActions.Num && row < RowBorders.Num(); ++row)
    {
        if (RowBorders[row]->GetCachedGeometry().IsUnderLocation(screenPosition))
        {
            return row;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Up / Down move the highlight, Enter runs the highlighted row, Esc closes
FReply USOLActionListWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    const FKey key = keyEvent.GetKey();
    if (key == EKeys::Up || key == EKeys::Down)
    {
        MoveHighlight(key == EKeys::Up ? -1 : 1);
        return FReply::Handled();
    }
    if (key == EKeys::Enter || key == EKeys::SpaceBar)
    {
        ConfirmRow(mState.Highlighted);
        return FReply::Handled();
    }
    if (key == EKeys::Escape)
    {
        RequestClose();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(geometry, keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// A left click on an enabled row runs it (a disabled row only highlights); a click outside the rows closes the list
FReply USOLActionListWidget::NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    const int32 row = FindRowAt(mouseEvent.GetScreenSpacePosition());
    if (row == INDEX_NONE)
    {
        RequestClose();
        return FReply::Handled();
    }
    if (mouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !ConfirmRow(row))
    {
        mState.Highlighted = row;
        RefreshRows();
    }
    return FReply::Handled();
}

//////////////////////////////////////////////////////////////////////////
// The row under the mouse is highlighted
FReply USOLActionListWidget::NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    const int32 row = FindRowAt(mouseEvent.GetScreenSpacePosition());
    if (row != INDEX_NONE && row != mState.Highlighted)
    {
        mState.Highlighted = row;
        RefreshRows();
    }
    return Super::NativeOnMouseMove(geometry, mouseEvent);
}
