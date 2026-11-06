/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

#include "SOLSolidScrollStack.generated.h"

class SSOLSolidTraceLayer;
class UScrollBox;
class USizeBox;
class USOLMenuButton;
class UVerticalBox;
class UVerticalBoxSlot;
class UWidget;
class UWidgetTree;

/**
 * A scrolling stack of equal items (SDD 46 D17, part 46g; the tab rail, the main-menu column and the pause panel): a
 * UScrollBox (the solid scroll bar, no scroll on a focus change) holding a vertical box of items, each itemHeight
 * tall with gap after it, optionally in a size box capped at a number of visible items. While the items fit
 * (SOLSolidScroll::NeedsScroll) the bar is collapsed and the scroll box does not clip (a hovered item's lift still
 * rises past the view's top, as before 46g); once they do not, the bar shows and it clips. Tick follows the keyboard
 * focus (a newly focused item scrolls whole into view, SOLSolidScroll::OffsetToShow; the focus as each button read it
 * in its own refresh, USOLMenuButton::WasFocusedAtRefresh, so the owner refreshes its buttons first) and pushes the
 * scroll to the panel's trace layer (SSOLSolidTraceLayer::SetScroll) only when it changed, so the hover traces and
 * LEDs drawn there move with the items. A plain data holder; the owner ticks it once per frame. No allocation per
 * frame.
 */
USTRUCT()
struct SOLTEST_API FSOLSolidScrollStack
{
    GENERATED_BODY()

    // Builds the scroll box and its item box; itemHeight and gap are each item's height and the gap after it, viewTop
    // the view's top in the trace layer's local units; maxVisibleItems > 0 wraps the scroll box in a size box capped
    // at that many items (applied as the items are added, never a 0 height). Returns the outermost widget
    UWidget* Build(UWidgetTree& tree, float itemHeight, float gap, float viewTop, int32 maxVisibleItems = 0);

    // Adds an item (with the gap after it) and returns its slot; a cap from Build follows the new item count
    UVerticalBoxSlot* AddItem(UWidget* item);

    // Caps the view at this many items of the item count, kept as items are added (needs a cap box from Build; <= 0:
    // no cap)
    void SetMaxVisibleItems(int32 maxVisibleItems);

    // Follows the keyboard focus among the buttons (item index = button index; each button's focus at its last
    // refresh, so refresh them first) and pushes the scroll offset and view band (0 tall while the items fit) to the
    // trace layer when they changed; shows the bar and clips only while the items need scrolling
    void Tick(const TSharedPtr<SSOLSolidTraceLayer>& layer, const TArray<TObjectPtr<USOLMenuButton>>& buttons);

    // Scrolls so an item shows whole, moving as little as possible (SOLSolidScroll::OffsetToShow); nothing before the
    // first layout or while the items fit
    void ShowItem(int32 index);

    // Forgets what was pushed and the focused item (a rebuilt trace layer gets everything again on the next tick)
    void ResetPushed();

    // Scrolls to the top (the next tick shows the focused item, if any, whole)
    void ScrollToTop();

    // Scrolls to the end (applied by the scroll box on its next tick)
    void ScrollToEnd();

    // Returns the scroll offset (0 = top)
    float GetScrollOffset() const;

    // Returns the view's height from the last layout (0 before one)
    float GetViewHeight() const;

    // Returns the items' height (desired, with each item's gap)
    float GetContentHeight() const;

    // Returns whether the items need scrolling at the last layout (SOLSolidScroll::NeedsScroll)
    bool NeedsScroll() const;

    UPROPERTY(Transient)
    TObjectPtr<UScrollBox> Scroll;

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> Items;

    UPROPERTY(Transient)
    TObjectPtr<USizeBox> Cap;               // Caps the view height (null without a cap)

    float ItemHeight = 0.0f;
    float Gap = 0.0f;
    float ViewTop = 0.0f;                   // The view's top in the trace layer's local units
    float PushedOffset = -1.0f;             // Scroll offset last pushed to the trace layer (-1: none)
    float PushedViewHeight = -1.0f;         // ... and view band height (0 while the items fit)
    TWeakPtr<SSOLSolidTraceLayer> PushedLayer;  // The layer they went to (compared only)
    int32 FocusedIndex = INDEX_NONE;        // The focused item last followed
    int32 MaxVisibleItems = 0;              // The view's cap in items (<= 0: none), applied as items are added
    bool bIsScrollOn = false;               // The bar shows and the scroll box clips (the items do not fit)
};
