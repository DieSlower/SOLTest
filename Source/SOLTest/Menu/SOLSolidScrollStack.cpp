/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidScrollStack.h"

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidScroll.h"
#include "Menu/SOLSolidStyle.h"
#include "Menu/SOLSolidTraceLayer.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

//////////////////////////////////////////////////////////////////////////
// Builds the scroll box (solid bar shown only when needed, no clipping until the items do not fit) and its item box,
// optionally in a size box for the visible-item cap
UWidget* FSOLSolidScrollStack::Build(UWidgetTree& tree, const float itemHeight, const float gap, const float viewTop,
    const int32 maxVisibleItems)
{
    ItemHeight = itemHeight;
    Gap = gap;
    ViewTop = viewTop;
    Items = tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

    // The scroll box: the bar right at the edge, the stack's own focus follow instead of Slate's, no overscroll (the
    // offset read for the traces is the drawn one)
    Scroll = tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
    Scroll->SetWidgetBarStyle(SOLSolidStyle::MakeScrollBarStyle(USOLSolidMenuSubsystem::Get(&tree)));
    Scroll->SetScrollbarThickness(FVector2D(SOL::SolidMenu::SCROLL_BAR_THICKNESS));
    Scroll->SetScrollbarPadding(FMargin(0.0f));
    Scroll->SetAlwaysShowScrollbar(false);
    Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::NoScroll);
    Scroll->SetAllowOverscroll(false);
    Scroll->SetClipping(EWidgetClipping::Inherit);
    Scroll->AddChild(Items);
    bIsScrollOn = false;
    ResetPushed();
    Cap = nullptr;
    MaxVisibleItems = maxVisibleItems;
    if (maxVisibleItems <= 0)
    {
        return Scroll;
    }

    // The cap's height follows the items as they are added (no items yet: no height to cap at)
    Cap = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
    Cap->SetContent(Scroll);
    return Cap;
}

//////////////////////////////////////////////////////////////////////////
// Adds an item with the gap after it and returns its slot; the cap follows the new item count
UVerticalBoxSlot* FSOLSolidScrollStack::AddItem(UWidget* item)
{
    if (Items == nullptr || item == nullptr)
    {
        return nullptr;
    }
    UVerticalBoxSlot* itemSlot = Items->AddChildToVerticalBox(item);
    itemSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Gap));
    if (Cap != nullptr && MaxVisibleItems > 0)
    {
        SetMaxVisibleItems(MaxVisibleItems);
    }
    return itemSlot;
}

//////////////////////////////////////////////////////////////////////////
// Caps the view at this many items of the item count (no cap at <= 0, nor before an item exists)
void FSOLSolidScrollStack::SetMaxVisibleItems(const int32 maxVisibleItems)
{
    MaxVisibleItems = maxVisibleItems;
    if (Cap == nullptr || Items == nullptr)
    {
        return;
    }
    if (maxVisibleItems <= 0 || Items->GetChildrenCount() == 0)
    {
        Cap->ClearMaxDesiredHeight();
        return;
    }
    const float content = SOLSolidScroll::StackContentHeight(Items->GetChildrenCount(), ItemHeight, Gap, 0.0f);
    Cap->SetMaxDesiredHeight(SOLSolidScroll::StackViewHeight(content, maxVisibleItems * (ItemHeight + Gap)));
}

//////////////////////////////////////////////////////////////////////////
// Follows the keyboard focus, keeps the bar and the clipping to the need to scroll and pushes a changed scroll to the
// trace layer
void FSOLSolidScrollStack::Tick(const TSharedPtr<SSOLSolidTraceLayer>& layer,
    const TArray<TObjectPtr<USOLMenuButton>>& buttons)
{
    if (Scroll == nullptr || Items == nullptr)
    {
        return;
    }
    const float viewHeight = GetViewHeight();
    const float contentHeight = GetContentHeight();
    float offset = Scroll->GetScrollOffset();

    // Before the first layout there is no view: nothing to follow or clip yet
    if (viewHeight > 0.0f)
    {
        // The bar and the clipping only while the items do not fit, past SCROLL_FIT_TOLERANCE_PX: a fitting stack
        // lays out and draws exactly as an unscrolled one (Slate alone would show the bar for a view a rounding error
        // shorter than its items)
        const bool bNeedsScroll = SOLSolidScroll::NeedsScroll(contentHeight, viewHeight);
        if (bNeedsScroll != bIsScrollOn)
        {
            bIsScrollOn = bNeedsScroll;
            Scroll->SetClipping(bNeedsScroll ? EWidgetClipping::ClipToBounds : EWidgetClipping::Inherit);
            Scroll->SetScrollBarVisibility(bNeedsScroll ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        }
        if (!bNeedsScroll && offset != 0.0f)
        {
            Scroll->SetScrollOffset(0.0f);
            offset = 0.0f;
        }

        // A newly focused item (keyboard, or the mouse: hover takes the focus) scrolls whole into view; the focus as
        // the buttons read it in this frame's refresh (no second focus query)
        int32 focused = INDEX_NONE;
        for (int32 index = 0; index < buttons.Num(); ++index)
        {
            if (buttons[index] != nullptr && buttons[index]->WasFocusedAtRefresh())
            {
                focused = index;
                break;
            }
        }
        if (focused != FocusedIndex)
        {
            FocusedIndex = focused;
            const float target = focused == INDEX_NONE ? offset : SOLSolidScroll::OffsetToShow(offset,
                SOLSolidScroll::StackItemTop(focused, ItemHeight, Gap, 0.0f), ItemHeight, contentHeight, viewHeight);
            if (target != offset)
            {
                Scroll->SetScrollOffset(target);
                offset = target;
            }
        }
    }

    // The trace layer draws the items' hover traces and LEDs at the drawn offset, clamped to Slate's own scroll end
    // (its scroll panel's; a layout rounding can put it a unit past content - view)
    const float endOffset = viewHeight > 0.0f ? Scroll->GetScrollOffsetOfEnd()
        : SOLSolidScroll::MaxOffset(contentHeight, viewHeight);
    const float drawnOffset = SOLSolidScroll::ClampOffset(offset, viewHeight + endOffset, viewHeight);

    // A fitting stack gets no view band: the layer then neither clips nor culls (no clip zone pushed per frame, and a
    // hovered item's trace may rise past the view's top with the item's lift, as the unclipped scroll box lets it)
    const float bandHeight = bIsScrollOn ? viewHeight : 0.0f;
    if (layer.IsValid() && (!PushedLayer.HasSameObject(layer.Get()) || drawnOffset != PushedOffset
        || bandHeight != PushedViewHeight))
    {
        layer->SetScroll(drawnOffset, ViewTop, bandHeight);
        PushedLayer = layer;
        PushedOffset = drawnOffset;
        PushedViewHeight = bandHeight;
    }
}

//////////////////////////////////////////////////////////////////////////
// Scrolls so an item shows whole with the least move (laid out and scrolling only)
void FSOLSolidScrollStack::ShowItem(const int32 index)
{
    const float viewHeight = GetViewHeight();
    if (Scroll == nullptr || Items == nullptr || index < 0 || index >= Items->GetChildrenCount() || viewHeight <= 0.0f
        || !NeedsScroll())
    {
        return;
    }
    const float offset = Scroll->GetScrollOffset();
    const float target = SOLSolidScroll::OffsetToShow(offset, SOLSolidScroll::StackItemTop(index, ItemHeight, Gap,
        0.0f), ItemHeight, GetContentHeight(), viewHeight);
    if (target != offset)
    {
        Scroll->SetScrollOffset(target);
    }
}

//////////////////////////////////////////////////////////////////////////
// Forgets what was pushed and the focused item
void FSOLSolidScrollStack::ResetPushed()
{
    PushedLayer.Reset();
    PushedOffset = -1.0f;
    PushedViewHeight = -1.0f;
    FocusedIndex = INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Scrolls to the top; the next tick shows the focused item whole again
void FSOLSolidScrollStack::ScrollToTop()
{
    if (Scroll != nullptr)
    {
        Scroll->ScrollToStart();
    }
    FocusedIndex = INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Scrolls to the end
void FSOLSolidScrollStack::ScrollToEnd()
{
    if (Scroll != nullptr)
    {
        Scroll->ScrollToEnd();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the scroll offset
float FSOLSolidScrollStack::GetScrollOffset() const
{
    return Scroll != nullptr ? Scroll->GetScrollOffset() : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the view's height from the last layout
float FSOLSolidScrollStack::GetViewHeight() const
{
    return Scroll != nullptr ? static_cast<float>(Scroll->GetCachedGeometry().GetLocalSize().Y) : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the items' desired height
float FSOLSolidScrollStack::GetContentHeight() const
{
    return Items != nullptr ? static_cast<float>(Items->GetDesiredSize().Y) : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the items need scrolling at the last layout
bool FSOLSolidScrollStack::NeedsScroll() const
{
    const float viewHeight = GetViewHeight();
    return viewHeight > 0.0f && SOLSolidScroll::NeedsScroll(GetContentHeight(), viewHeight);
}
