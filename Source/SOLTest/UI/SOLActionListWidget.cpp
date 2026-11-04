/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLActionListWidget.h"

#include "Interaction/SOLInteractionSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloFrameWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Fonts, sizes (Slate units) and the hologram palette, as the Overlay Menu
    const TCHAR* const ACTION_LIST_FONT = TEXT("Regular");
    constexpr int32 ACTION_LIST_TITLE_SIZE = 15;
    constexpr int32 ACTION_LIST_ROW_SIZE = 13;
    constexpr int32 ACTION_LIST_HINT_SIZE = 10;
    constexpr float ACTION_LIST_ROW_PADDING = 3.0f;
    const FLinearColor ACTION_LIST_TEXT(0.75f, 0.9f, 1.0f, 1.0f);
    const FLinearColor ACTION_LIST_DIM_TEXT(0.35f, 0.47f, 0.58f, 1.0f);

    // Room the hologram frame needs inside the panel (local units; the frame's pixel sizes are about 1.5 local units at
    // 720-1440 px high views, as in the Overlay Menu): around the content; on the left, so a row's hover brackets clear
    // the top-left accent (6 px in); below the title, so the rows start under the frame's title underline (34 px); on
    // the right, clear of the bottom-right step-in; and above the hint, room for the hint divider
    constexpr float ACTION_LIST_PADDING = 18.0f;
    constexpr float ACTION_LIST_LEFT_ROOM = 8.0f;
    constexpr float ACTION_LIST_TITLE_GAP = 28.0f;
    constexpr float ACTION_LIST_RIGHT_ROOM = 16.0f;
    constexpr float ACTION_LIST_HINT_GAP = 12.0f;

    // Placement estimate before the first layout (the panel's size with the frame's room)
    constexpr float ACTION_LIST_ESTIMATED_WIDTH = 330.0f;
    constexpr float ACTION_LIST_ESTIMATED_ROW = 23.0f;
    constexpr int32 ACTION_LIST_ESTIMATED_EXTRA_ROWS = 5;   // Title, hint and the frame's room, in rows

    // Frames the hologram frame's layout is re-read after a change: the change is arranged in the paint of the frame
    // it happened in, so the next tick reads it (the second frame is a margin)
    constexpr int32 ACTION_LIST_LAYOUT_SETTLE_FRAMES = 2;

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
// Builds the widget tree: a full-screen transparent root, the frame behind the panel with the title, MAX_ACTIONS rows
// and the hint
void USOLActionListWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    RootBorder->SetBrushColor(FLinearColor::Transparent);
    RootBorder->SetHorizontalAlignment(HAlign_Left);
    RootBorder->SetVerticalAlignment(VAlign_Top);
    WidgetTree->RootWidget = RootBorder;

    // The frame fills the panel's slot behind it; the panel itself is clear (the frame draws the tint and the marks)
    PanelStack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PanelStack"));
    RootBorder->SetContent(PanelStack);
    HoloFrame = WidgetTree->ConstructWidget<USOLHoloFrameWidget>(USOLHoloFrameWidget::StaticClass(),
        TEXT("HoloFrame"));
    UOverlaySlot* frameSlot = PanelStack->AddChildToOverlay(HoloFrame);
    frameSlot->SetHorizontalAlignment(HAlign_Fill);
    frameSlot->SetVerticalAlignment(VAlign_Fill);
    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    PanelBorder->SetBrushColor(FLinearColor::Transparent);
    PanelBorder->SetPadding(FMargin(ACTION_LIST_PADDING + ACTION_LIST_LEFT_ROOM, ACTION_LIST_PADDING,
        ACTION_LIST_PADDING + ACTION_LIST_RIGHT_ROOM, ACTION_LIST_PADDING));
    UOverlaySlot* panelSlot = PanelStack->AddChildToOverlay(PanelBorder);
    panelSlot->SetHorizontalAlignment(HAlign_Fill);
    panelSlot->SetVerticalAlignment(VAlign_Fill);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    PanelBorder->SetContent(column);
    TitleText = ActionListMakeText(*WidgetTree, ACTION_LIST_TITLE_SIZE, SOL::HudPalette::SECONDARY);
    column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ACTION_LIST_TITLE_GAP));
    RowColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Rows"));
    column->AddChildToVerticalBox(RowColumn);

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
        RowColumn->AddChildToVerticalBox(border);
        RowBorders.Add(border);
        RowTexts.Add(text);
    }
    HintText = ActionListMakeText(*WidgetTree, ACTION_LIST_HINT_SIZE, ACTION_LIST_DIM_TEXT);
    HintText->SetText(FText::FromString(SOLMenuText::INTERACT_LIST_HINT));
    column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.0f, ACTION_LIST_HINT_GAP, 0.0f, 0.0f));
    mOpenVisibility = GetVisibility();
}

//////////////////////////////////////////////////////////////////////////
// Fills the list for an object, places it at the cursor and opens the frame (or reverses a close still playing)
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
    const bool bIsReopenDuringClose = mIsClosing && IsInViewport() && !mIsOpenPending;

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
        (mActions.Num + ACTION_LIST_ESTIMATED_EXTRA_ROWS) * ACTION_LIST_ESTIMATED_ROW * viewportScale);
    const FVector2D cornerPx = SOLListMenu::PlacePanel(cursorPx, panelPx, viewportSizePx,
        SOL::Interact::LIST_CURSOR_OFFSET_PX * viewportScale, SOL::Interact::LIST_MARGIN_PX * viewportScale);
    RootBorder->SetPadding(FMargin(cornerPx.X / viewportScale, cornerPx.Y / viewportScale, 0.0f, 0.0f));

    // Takes input again (a reopen during the close) and opens the frame (or reverses its close from where it is); a
    // frame not built yet (the first open: its Slate widget is made when the list is first arranged) opens from the
    // first tick, which runs before the content's first paint, so the content waits hidden until then
    mIsClosing = false;
    SetVisibility(mOpenVisibility);
    SetIsFocusable(true);
    mIsOpenPending = true;
    if (HoloFrame->GetFrame().IsValid())
    {
        StartFrameOpen();
    }
    else
    {
        ApplyContentOpacity(0.0f);
    }
    MarkLayoutDirty();
    if (bIsReopenDuringClose && HoloFrame->GetFrame().IsValid())
    {
        // The close reverses from its progress (SDD 4.5), but the old object's title step and hint divider must not
        // show over the new content: the layout is rebuilt from the new content now
        UpdateFrameLayoutFromDesired();
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Lays the frame out from the content's desired sizes (a reopen frame: the stack is arranged only at the next paint):
// the title one line as before, so the rows keep their top; their height, the column width and the hint follow
void USOLActionListWidget::UpdateFrameLayoutFromDesired()
{
    const FBox2f lastRows = HoloFrame->ToFrameLocal(RowColumn->GetCachedGeometry());
    if (!SOLHoloMenu::IsValidRect(lastRows))
    {
        return;
    }
    ForceLayoutPrepass();
    const FVector2D titleSize = TitleText->GetDesiredSize();
    const FVector2D rowsSize = RowColumn->GetDesiredSize();
    const float width = static_cast<float>(FMath::Max3(titleSize.X, rowsSize.X, HintText->GetDesiredSize().X));
    FSOLHoloOverlayLayoutInput input;
    input.TitleWidth = static_cast<float>(titleSize.X);
    input.RowColumn = FBox2f(lastRows.Min, lastRows.Min + FVector2f(width, static_cast<float>(rowsSize.Y)));
    input.HintTopY = input.RowColumn.Max.Y + ACTION_LIST_HINT_GAP;
    FSOLHoloFrameSpec layout;
    SOLHoloFrameBuild::MakeOverlayLayout(input, layout);
    HoloFrame->SetLayout(layout);
}

//////////////////////////////////////////////////////////////////////////
// Opens the built frame (or reverses its close) with the open cue; the content follows the frame's fill
void USOLActionListWidget::StartFrameOpen()
{
    mIsOpenPending = false;
    HoloFrame->BeginOpen();

    // The initial highlight goes to the frame while it is Opening, so the open plays no hover cue (at strength 0 too,
    // where the next advance already reads Open)
    UpdateFrameHover();
    mIsAnimating = true;
    ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Starts the non-blocking close: the owner gives input back at once, the frame's close plays, then the widget removes
// itself
void USOLActionListWidget::BeginClose()
{
    mIsClosing = true;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetIsFocusable(false);

    // The keyboard goes back to the game viewport now (the owner's input mode change follows), so no key reaches the
    // list while it closes
    if (HasAnyUserFocus() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
    if (HoloFrame == nullptr || !IsInViewport() || mIsOpenPending)
    {
        FinishClose();
        return;
    }
    HoloFrame->BeginClose();
    if (HoloFrame->AdvanceAnim() == ESOLHoloMenuPhase::Closed)
    {
        FinishClose();
        return;
    }
    mIsAnimating = true;
    ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Ends the close: removes the widget (the next Open adds it again)
void USOLActionListWidget::FinishClose()
{
    mIsAnimating = false;
    mIsOpenPending = false;
    ApplyContentOpacity(0.0f);
    RemoveFromParent();
}

//////////////////////////////////////////////////////////////////////////
// Removes the list at once, without the close (teardown, map close); the frame drops to Closed, so the next Open draws
// in again with its open cue
void USOLActionListWidget::Dismiss()
{
    mIsClosing = true;
    if (HoloFrame != nullptr)
    {
        HoloFrame->ResetClosed();
    }
    FinishClose();
}

//////////////////////////////////////////////////////////////////////////
// While the frame animates: advances it and follows its fill, removing the widget once closed; after a change: lays
// the frame out again. Returns at once while the list is open and idle
void USOLActionListWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    if (HoloFrame == nullptr)
    {
        return;
    }
    if (mIsOpenPending && HoloFrame->GetFrame().IsValid())
    {
        StartFrameOpen();
    }
    if (mIsAnimating)
    {
        const ESOLHoloMenuPhase phase = HoloFrame->AdvanceAnim();
        if (phase == ESOLHoloMenuPhase::Closed)
        {
            FinishClose();
            return;
        }
        ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
        mIsAnimating = phase != ESOLHoloMenuPhase::Open;
    }
    if (mLayoutDirtyFrames > 0 && !mIsClosing)
    {
        UpdateFrameLayout();
    }
}

//////////////////////////////////////////////////////////////////////////
// Asks the frame layout to be rebuilt over the next frames
void USOLActionListWidget::MarkLayoutDirty()
{
    if (HoloFrame != nullptr)
    {
        mLayoutDirtyFrames = ACTION_LIST_LAYOUT_SETTLE_FRAMES;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the frame's layout and hover from the content as last arranged, in the frame's local units (the frame fills
// the panel, so the panel's top-left is the origin): the title step and the hint divider below the rows, no tabs and
// no column divider; waits while the frame has not been arranged yet
void USOLActionListWidget::UpdateFrameLayout()
{
    const FVector2D frameSize = HoloFrame->GetCachedGeometry().GetLocalSize();
    if (frameSize.X <= 0.0 || frameSize.Y <= 0.0)
    {
        return;
    }
    FSOLHoloOverlayLayoutInput input;
    input.TitleWidth = static_cast<float>(TitleText->GetDesiredSize().X);
    input.RowColumn = HoloFrame->ToFrameLocal(RowColumn->GetCachedGeometry());
    input.HintTopY = HoloFrame->ToFrameLocal(HintText->GetCachedGeometry()).Min.Y;
    FSOLHoloFrameSpec layout;
    SOLHoloFrameBuild::MakeOverlayLayout(input, layout);
    HoloFrame->SetLayout(layout);
    UpdateFrameHover();
    --mLayoutDirtyFrames;
}

//////////////////////////////////////////////////////////////////////////
// Sets the frame's hovered item: the highlighted row (brackets and a pointer line around its text, a disabled row's
// reason included); the frame asks for the hover cue when the item changed while open
void USOLActionListWidget::UpdateFrameHover()
{
    const int32 row = mState.Highlighted;
    if (row < 0 || row >= mActions.Num || !RowTexts.IsValidIndex(row))
    {
        HoloFrame->SetHoveredItem(INDEX_NONE, FBox2f(ForceInit), ESOLHoloHoverMark::RowPointer);
        return;
    }
    const FBox2f rect = SOLHoloFrameBuild::TextItemRect(HoloFrame->ToFrameLocal(RowTexts[row]->GetCachedGeometry()),
        static_cast<float>(RowTexts[row]->GetDesiredSize().X), 0.0f);
    HoloFrame->SetHoveredItem(row, rect, ESOLHoloHoverMark::RowPointer);
}

//////////////////////////////////////////////////////////////////////////
// Sets the content's render opacity when it changed (the panel holds the title, the rows and the hint)
void USOLActionListWidget::ApplyContentOpacity(const float opacity)
{
    if (opacity != mContentOpacity && PanelBorder != nullptr)
    {
        mContentOpacity = opacity;
        PanelBorder->SetRenderOpacity(opacity);
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-colours the rows (the highlight in Secondary, a disabled row dim; the frame draws the highlight's mark) and asks
// the frame to follow
void USOLActionListWidget::RefreshRows()
{
    for (int32 row = 0; row < mActions.Num && row < RowTexts.Num(); ++row)
    {
        const bool bHighlighted = row == mState.Highlighted;
        RowTexts[row]->SetColorAndOpacity(FSlateColor(bHighlighted ? SOL::HudPalette::SECONDARY
            : (mActions.Entries[row].IsEnabled() ? ACTION_LIST_TEXT : ACTION_LIST_DIM_TEXT)));
    }
    MarkLayoutDirty();
}

//////////////////////////////////////////////////////////////////////////
// Runs a row's action when it is enabled, plays the select cue when the action ran (at any strength, D21; a refused
// one plays none, as the radial) and closes
bool USOLActionListWidget::ConfirmRow(const int32 row)
{
    if (Interaction == nullptr || mIsClosing || row < 0 || row >= mActions.Num || !mActions.Entries[row].IsEnabled())
    {
        return false;
    }
    // The action first, so the owner's close sees its result (a Jump To closes the map)
    const bool bDone = Interaction->Execute(mObject, mActions.Entries[row].Action);
    if (bDone && HoloFrame != nullptr)
    {
        HoloFrame->PlaySelectCue();
    }
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
// Up / Down move the highlight, Enter runs the highlighted row, Esc closes (nothing while the list closes)
FReply USOLActionListWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    if (mIsClosing)
    {
        return Super::NativeOnKeyDown(geometry, keyEvent);
    }
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
    if (mIsClosing)
    {
        return Super::NativeOnMouseButtonDown(geometry, mouseEvent);
    }
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
// The row under the mouse is highlighted (every mouse move, as before the hologram look, D5)
FReply USOLActionListWidget::NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    const int32 row = mIsClosing ? INDEX_NONE : FindRowAt(mouseEvent.GetScreenSpacePosition());
    if (row != INDEX_NONE && row != mState.Highlighted)
    {
        mState.Highlighted = row;
        RefreshRows();
    }
    return Super::NativeOnMouseMove(geometry, mouseEvent);
}
