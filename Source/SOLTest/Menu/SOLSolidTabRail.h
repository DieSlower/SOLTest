/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLSolidScrollStack.h"
#include "Menu/SOLSolidTraceLayer.h"

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

#include "SOLSolidTabRail.generated.h"

class UScrollBox;
class USizeBox;
class UTextBlock;
class USOLSolidPanel;
struct FSOLSolidMenuFrame;
class UWidgetTree;

/**
 * Pure geometry of the tab rail (SDD 46 section 6.3, shared by Settings, Controls, Info, About and Help), widget-free.
 */
namespace SOLSolidTabRail
{
    // A tab's hover trace in rail local units: a straight run at the tab's centre line from the tab's right end
    // (TAB_COLUMN_WIDTH - SETTINGS_TAB_RAIL_PADDING - SETTINGS_TAB_TRACE_PX) to the rail's edge
    // (TAB_COLUMN_WIDTH - SETTINGS_TAB_TRACE_END_PX); two points, no ring terminal. A negative index reads as tab 0
    SOLTEST_API FSOLSolidTrace TraceForTab(int32 index);

    // The height a rail of this many tabs needs: the rail padding on both sides plus each tab's height and gap
    SOLTEST_API float RailHeight(int32 tabCount);

    // (33d-ii, SDD 33 D22) The rail with group captions: captioned[i] says a caption row (TAB_RAIL_CAPTION_HEIGHT,
    // then BUTTON_GAP) sits above tab i; an index outside the view reads false. With an empty view or no true flag
    // each function equals its caption-less form

    // A tab's top in rail local units: the padding, one tab step per tab above it and one caption step per caption
    // row up to and including its own (a negative index reads 0)
    SOLTEST_API float TabTop(int32 index, TConstArrayView<bool> captioned);

    // The top of the caption row above a tab (one caption step above the tab); -1 when the tab has none, including an
    // index outside the view
    SOLTEST_API float CaptionTop(int32 index, TConstArrayView<bool> captioned);

    // The height a rail of this many tabs with these caption rows needs (a negative count reads 0)
    SOLTEST_API float RailHeight(int32 tabCount, TConstArrayView<bool> captioned);

    // A tab's hover trace (as TraceForTab(index)) at its captioned centre line, TabTop(index, captioned) plus half
    // TAB_HEIGHT
    SOLTEST_API FSOLSolidTrace TraceForTab(int32 index, TConstArrayView<bool> captioned);
}

/**
 * The solid tab column (SDD 46 section 6.3, 46f: promoted from the Settings page): a recess holding one Tab button per
 * tab, top-aligned, each leaving room on its right for its hover trace, which the recess's trace layer draws from the
 * tab's end to the rail's edge. The owning page builds it once (Build), binds the buttons' hover traces to the trace
 * layer when its Slate widget exists (Bind, from NativeConstruct), shows the selected mark (Select), and ticks it
 * once per frame (Tick: the trace layer's effects strength when it changed, and each button's highlight). A plain
 * data holder for the page; it owns no Slate state of its own.
 * (46g, D17) The tabs sit in a scrolling stack (FSOLSolidScrollStack) inside the recess padding: when more tabs than
 * fit, the rail scrolls (the solid bar shows, the focused or newly selected tab scrolls whole into view, the hover
 * traces move with the tabs); while they fit it lays out and draws exactly as before.
 */
USTRUCT()
struct SOLTEST_API FSOLSolidTabRail
{
    GENERATED_BODY()

    // Builds the rail for a list of tab labels (button index = position) and returns the column widget (the recess
    // in a box TAB_COLUMN_WIDTH wide); every button reports a click through onClicked. (33d-ii, SDD 33 D22) A
    // non-empty captions[i] draws a non-clickable group caption row above tab i (no captions: the rail lays out
    // exactly as before)
    USizeBox* Build(UWidgetTree& tree, const TArray<FString>& labels, const FSOLMenuButtonClicked& onClicked,
        const TArray<FString>& captions = TArray<FString>());

    // Binds each button's hover trace to the rail's trace layer (the layer exists once the Slate widget is built)
    void Bind();

    // Shows a tab as selected (the others not) and, on a scrolling rail, scrolls it whole into view
    void Select(int32 index);

    // Pushes the effects strength to the trace layer when it changed, refreshes every button's highlight, then follows
    // the focused tab with the scroll (the scroll pushed to the trace layer when it changed)
    void Tick(const FSOLSolidMenuFrame& frame, bool bHoverElsewhere);

    // Scrolls the tabs to the top (each page's opening, with its first tab)
    void ScrollToTop();

    // Scrolls the tabs to the end (verification)
    void ScrollToEnd();

    // Returns the tabs' scroll box (verification)
    UScrollBox* GetScrollBox() const { return Stack.Scroll; }

    // Returns the tabs' scroll offset (0 = top; verification)
    float GetScrollOffset() const { return Stack.GetScrollOffset(); }

    // Returns the tabs' view height from the last layout (verification)
    float GetViewHeight() const { return Stack.GetViewHeight(); }

    // Returns the tabs' content height (verification)
    float GetContentHeight() const { return Stack.GetContentHeight(); }

    // Returns whether the tabs need scrolling at the last layout (verification)
    bool NeedsScroll() const { return Stack.NeedsScroll(); }

    // Clears the hover trace left from the last visit, unless a tab still has the keyboard focus
    void ClearStaleHover();

    // Returns a tab's button, or null
    USOLMenuButton* GetButton(int32 index) const;

    // Returns whether the rail draws a tab's hover trace
    bool IsTraceShown(int32 index) const;

    // Returns whether the rail draws any hover trace
    bool IsAnyTraceShown() const;

    // (33d-ii) Returns the number of group captions (verification)
    int32 GetCaptionCount() const { return Captions.Num(); }

    // (33d-ii) Returns a group caption's text block, or null (verification)
    UTextBlock* GetCaption(int32 index) const;

    // (33d-ii) Returns the tab a group caption sits above, or INDEX_NONE (verification)
    int32 GetCaptionTab(int32 index) const
    {
        return CaptionTabs.IsValidIndex(index) ? CaptionTabs[index] : INDEX_NONE;
    }

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> Panel;       // The rail recess; its trace layer draws the hovered tab's trace

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> Buttons;

    UPROPERTY(Transient)
    FSOLSolidScrollStack Stack;             // The tabs' scrolling stack (46g)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> Captions; // (33d-ii) The group captions, top to bottom

    TArray<int32> CaptionTabs;              // The tab each caption sits above
    TArray<bool> Captioned;                 // Per tab: a caption row sits above it (empty without captions)
    TArray<FSOLSolidTrace> Traces;          // Each tab's hover trace (rail local)
    float Strength = -1.0f;                 // Effects strength last pushed to the trace layer
};
