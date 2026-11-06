/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLSolidTabRail.h"

#include "SOLReadOnlyPageWidget.generated.h"

class UScrollBox;
class UTextBlock;
class UWidgetSwitcher;

/**
 * The Controls, Info and About pages in the solid shell (SDD 46 section 6.5, Q6): the page's sections are the tabs of a
 * vertical tab column (the shared FSOLSolidTabRail) left of a content recess showing the chosen tab's read-only rows in
 * its own scroll box (key / value columns, headings, prose). SOLMenuContent fills the tabs from today's entries (the
 * Controls rows by area, the Info values generated from the real constants, the About texts). The page opens on its
 * first tab with every tab at its top each time ShowOpening is called (Q8: no last-tab or scroll memory) and clears the
 * rail's stale hover trace; the hover trace shows only on the tab column (the rows are plain text, nothing else
 * hovers). The owning menu refreshes the key-naming texts from the live keys (SOLMenuContent::RefreshControlsPage and
 * RefreshInfoPage on the arrays below) and calls ShowOpening when it shows the page. The rail's buttons are ticked
 * here, the animations scale with the HUD effects strength through the rail (0 = instant).
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

    // Every left-column text (the Controls key cells, the Info labels) with its tab and column width (verification:
    // none may run past its column)
    struct FColumnText
    {
        const UTextBlock* Text;
        int32 Tab;
        float Width;
    };
    const TArray<FColumnText>& GetColumnTexts() const { return ColumnTexts; }

    // Adds a left-column text (called by SOLMenuContent while it fills a tab)
    void RegisterColumnText(const UTextBlock* text, int32 tab, float width);

    // Returns the tab's scroll box to fill (called by SOLMenuContent)
    UScrollBox& GetFillScroll(int32 tab) const
    {
        checkf(TabScrolls.IsValidIndex(tab), TEXT("tab %d has no scroll box"), tab);
        return *TabScrolls[tab];
    }

    // Storage SOLMenuContent fills
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> KeyCells;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> KeyTexts;

protected:

    // Binds the rail's hover traces to its trace layer
    virtual void NativeConstruct() override;

    // Ticks the rail's buttons (highlight) and the trace layer's strength
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:

    // A tab button was clicked (index = the tab's position)
    void HandleTabClicked(int32 index);

    UPROPERTY(Transient)
    FSOLSolidTabRail Rail;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UScrollBox>> TabScrolls;      // Each tab's rows

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> TabSwitcher;        // Shows the selected tab's scroll box

    TArray<FColumnText> ColumnTexts;
    ESOLReadOnlyPage mPage = ESOLReadOnlyPage::Count;
    int32 mTab = 0;
};
