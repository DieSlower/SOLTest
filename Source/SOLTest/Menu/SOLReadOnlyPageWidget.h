/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLPageTabs.h"
#include "Menu/SOLSolidTabRail.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLReadOnlyPageWidget.generated.h"

class UScrollBox;
class USizeBox;
class UTextBlock;
class UWidgetSwitcher;

// A read-only page's two-column row: its left-column text (the Controls key cells, the Info labels) in its box, the
// right cell beside it (the action, the value), its tab and the left column's current width (46i: the Info labels'
// column narrows in narrow rows, SOLPageTabs::LeftColumnWidth); the fit checks read them (verification: none may run
// past its column); the widgets are reflected references
USTRUCT()
struct FSOLReadOnlyColumnText
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Text;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Value;

    UPROPERTY(Transient)
    TObjectPtr<USizeBox> Box;

    UPROPERTY(Transient)
    int32 Tab = 0;

    UPROPERTY(Transient)
    float Width = 0.0f;
};

/**
 * The Controls, Info and About pages in the solid shell (SDD 46 section 6.5, Q6): the page's sections are the tabs of a
 * vertical tab column (the shared FSOLSolidTabRail) left of a content recess showing the chosen tab's read-only rows in
 * its own scroll box (key / value columns, headings, prose). SOLMenuContent fills the tabs from today's entries (the
 * Controls rows by area, the Info values generated from the real constants, the About texts). The page opens on its
 * first tab with every tab at its top each time ShowOpening is called (Q8: no last-tab or scroll memory) and clears the
 * rail's stale hover trace; the hover trace shows only on the tab column (the rows are plain text, nothing else
 * hovers). The owning menu refreshes the key-naming texts from the live keys (SOLMenuContent::RefreshControlsPage and
 * RefreshInfoPage on the arrays below) and calls ShowOpening when it shows the page. The rail's buttons are ticked
 * here, the animations scale with the HUD effects strength through the rail (0 = instant). (46i) The tick also fits
 * the left column to the rows' laid-out width (SOLPageTabs::LeftColumnWidth: the Info labels' column narrows in a
 * narrow view, e.g. 1000x800, so the values keep room), written only when it changes.
 */
UCLASS()
class SOLTEST_API USOLReadOnlyPageWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Builds the page's tabs and content (once, before the widget is added to a tree)
    void Setup(ESOLReadOnlyPage page);

    // The page was shown: its first tab, every tab scrolled to its top, no hover left from the last visit
    void ShowOpening();

    // Shows a tab as a click on its tab button does (SOLPageTabs::SelectTab: an invalid tab keeps the current one)
    void SelectTab(int32 tab);

    // Returns the page kind
    ESOLReadOnlyPage GetPageKind() const { return mPage; }

    // Returns the tab showing
    int32 GetSelectedTab() const { return mTab; }

    // Returns the number of tabs
    int32 GetTabCount() const { return Rail.Buttons.Num(); }

    // Returns the content switcher's shown index (verification: equals the selected tab), or INDEX_NONE
    int32 GetShownContentIndex() const;

    // Returns a tab's button, or null (verification)
    USOLMenuButton* GetTabButton(int32 tab) const { return Rail.GetButton(tab); }

    // Returns whether the rail draws a tab's hover trace, or any (verification)
    bool IsTabHoverTraceShown(int32 tab) const { return Rail.IsTraceShown(tab); }
    bool IsAnyTabHoverTraceShown() const { return Rail.IsAnyTraceShown(); }

    // Returns the tab rail (its scroll state and ScrollToEnd; verification)
    FSOLSolidTabRail& GetRail() { return Rail; }

    // Returns a tab's scroll box, or null (verification)
    UScrollBox* GetTabScroll(int32 tab) const;

    // Returns a tab's scroll offset (0 at the top), or -1 for an invalid tab (verification)
    float GetTabScrollOffset(int32 tab) const;

    // The Controls page's key cells, one per SOLMenuText::CONTROL_ROWS row (for SOLMenuContent::RefreshControlsPage)
    const TArray<TObjectPtr<UTextBlock>>& GetKeyCells() const { return KeyCells; }

    // The Info page's key-naming texts (for SOLMenuContent::RefreshInfoPage)
    const TArray<TObjectPtr<UTextBlock>>& GetKeyTexts() const { return KeyTexts; }

    // Empties the key cells and reserves room for count (called by SOLMenuContent before it fills the Controls tabs)
    void ResetKeyCells(int32 count) { KeyCells.Reset(count); }

    // Adds a key cell (called by SOLMenuContent while it fills the Controls tabs)
    void AddKeyCell(UTextBlock* cell) { KeyCells.Add(cell); }

    // Empties the key-naming texts and reserves room for count (called by SOLMenuContent before it fills Info)
    void ResetKeyTexts(int32 count) { KeyTexts.Reset(count); }

    // Adds a key-naming text (called by SOLMenuContent while it fills the Info tabs)
    void AddKeyText(UTextBlock* text) { KeyTexts.Add(text); }

    // Every two-column row (the Controls key cells, the Info labels, their right cells) with its tab and the left
    // column's current width (verification: none may run past its column)
    using FColumnText = FSOLReadOnlyColumnText;
    const TArray<FSOLReadOnlyColumnText>& GetColumnTexts() const { return ColumnTexts; }

    // Adds a two-column row: its left text in its box (width wide) and its right cell (called by SOLMenuContent while
    // it fills a tab)
    void RegisterColumnText(UTextBlock* text, USizeBox* box, UTextBlock* value, int32 tab, float width);

    // Returns the left column's current width (SOLPageTabs::LeftColumnWidth for the last laid-out rows; 0 without
    // two-column rows)
    float GetLeftColumnWidth() const { return mLeftColumnWidth; }

    // Returns the tab's scroll box to fill (called by SOLMenuContent)
    UScrollBox& GetFillScroll(int32 tab) const
    {
        checkf(TabScrolls.IsValidIndex(tab), TEXT("tab %d has no scroll box"), tab);
        return *TabScrolls[tab];
    }

protected:

    // Binds the rail's hover traces to its trace layer
    virtual void NativeConstruct() override;

    // Ticks the rail's buttons (highlight) and the trace layer's strength
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:

    // A tab button was clicked (index = the tab's position)
    void HandleTabClicked(int32 index);

    // (46i) Sets the left column's width for the rows' laid-out width (SOLPageTabs::LeftColumnWidth), only on a change
    void FitLeftColumn();

    UPROPERTY(Transient)
    FSOLSolidTabRail Rail;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UScrollBox>> TabScrolls;      // Each tab's rows

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> TabSwitcher;        // Shows the selected tab's scroll box

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> KeyCells;        // SOLMenuContent fills these two (Controls, Info)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> KeyTexts;

    UPROPERTY(Transient)
    TArray<FSOLReadOnlyColumnText> ColumnTexts;
    ESOLReadOnlyPage mPage = ESOLReadOnlyPage::Count;
    int32 mTab = 0;
    float mLeftColumnWidth = 0.0f;                  // The left column's width now (every row's box has it)
};
