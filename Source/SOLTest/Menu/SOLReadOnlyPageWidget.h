/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLHudSymbolView.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLSolidTabRail.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#include "SOLReadOnlyPageWidget.generated.h"

class UImage;
class UScrollBox;
class USOLKeyboardDiagram;
class USOLKeyboardDiagramPopup;
class USOLKeyCapRow;
class USizeBox;
class UTextBlock;
class UWidgetSwitcher;

// (33d-ii) One HUD glossary row of the Help page: its GLOSSARY_ENTRIES row and tab, the symbol box and its view (null
// for a row without art, rule (c), and for a crop row, whose box holds the crop image instead), the name and
// description texts, and the description and sample last written (SOLMenuContent::RefreshHelpPage writes a text only
// when it changed); the widgets are reflected references
USTRUCT()
struct FSOLHelpGlossaryRow
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    int32 Entry = INDEX_NONE;

    UPROPERTY(Transient)
    int32 Tab = 0;

    UPROPERTY(Transient)
    TObjectPtr<USizeBox> Box;

    UPROPERTY(Transient)
    TObjectPtr<USOLHudSymbolView> View;

    UPROPERTY(Transient)
    TObjectPtr<UImage> CropImage;               // (33e-ii, D39) A map row's screenshot crop (its View is null)

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Name;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Description;

    FString LastDescription;
    FString LastSample;
};

// A read-only page's two-column row: its left-column text (the Info labels) or key caps (the Controls key cells,
// 33b) in its box, the right cell beside it (the action, the value), its tab and the left column's current width (46i:
// the Info labels' column narrows in narrow rows, SOLPageTabs::LeftColumnWidth); the fit checks read them
// (verification: none may run past its column); the widgets are reflected references
USTRUCT()
struct FSOLReadOnlyColumnText
{
    GENERATED_BODY()

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Text;                    // Null on a key cap row

    UPROPERTY(Transient)
    TObjectPtr<USOLKeyCapRow> Caps;                 // A Controls row's key caps (33b), else null

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
 * The Controls, Info, About and (33d-ii) Help pages in the solid shell (SDD 46 section 6.5, Q6): the page's sections
 * are the tabs of a vertical tab column (the shared FSOLSolidTabRail; Help's with its group captions) left of a content
 * recess showing the chosen tab's read-only rows in its own scroll box (key / value columns, headings, prose).
 * SOLMenuContent fills the tabs from today's entries (the Controls rows by area, the Info values generated from the
 * real constants, the About texts). The page opens on its first tab with every tab at its top each time ShowOpening is
 * called (Q8: no last-tab or scroll memory) and clears the rail's stale hover trace; the hover trace shows only on the
 * tab column (the rows are plain text, nothing else hovers). The owning menu refreshes the key-naming texts from the
 * live keys (SOLMenuContent::RefreshControlsPage and RefreshInfoPage on the arrays below) and calls ShowOpening when it
 * shows the page. The rail's buttons are ticked here, the animations scale with the HUD effects strength through the
 * rail (0 = instant). (46i) The tick also fits the left column to the rows' laid-out width
 * (SOLPageTabs::LeftColumnWidth: the Info labels' column narrows in a narrow view, e.g. 1000x800, so the values keep
 * room), written only when it changes. (33d-ii) A Help page also owns the Keyboard tab's USOLKeyboardDiagram (laid out
 * at DIAGRAM_START_LAYOUT_WIDTH from the fill, so it paints on the first frame, then in the rows' width, only on a
 * change) and the glossary rows' USOLHudSymbolViews, whose batches the tick lets rebuild for the shown tab only
 * (through one scratch the page owns); it binds USOLSettingsSubsystem::OnKeyBindingsChanged while constructed to
 * refresh its key-naming texts (SOLMenuContent::RefreshHelpPage).
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

    // The Controls page's key cap cells, one per SOLMenuText::CONTROL_ROWS row (for
    // SOLMenuContent::RefreshControlsPage)
    const TArray<TObjectPtr<USOLKeyCapRow>>& GetKeyCells() const { return KeyCells; }

    // The Info page's key-naming texts (for SOLMenuContent::RefreshInfoPage)
    const TArray<TObjectPtr<UTextBlock>>& GetKeyTexts() const { return KeyTexts; }

    // Empties the key cells and reserves room for count (called by SOLMenuContent before it fills the Controls tabs)
    void ResetKeyCells(int32 count) { KeyCells.Reset(count); }

    // Adds a key cell (called by SOLMenuContent while it fills the Controls tabs)
    void AddKeyCell(USOLKeyCapRow* cell) { KeyCells.Add(cell); }

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

    // (33b) Adds a two-column row whose left cell is key caps (wrapping in the column's width) in its box, and its
    // right cell (called by SOLMenuContent while it fills the Controls tabs)
    void RegisterColumnCaps(USOLKeyCapRow* caps, USizeBox* box, UTextBlock* value, int32 tab, float width);

    // Returns the left column's current width (SOLPageTabs::LeftColumnWidth for the last laid-out rows; 0 without
    // two-column rows)
    float GetLeftColumnWidth() const { return mLeftColumnWidth; }

    // Returns the tab's scroll box to fill (called by SOLMenuContent)
    UScrollBox& GetFillScroll(int32 tab) const
    {
        checkf(TabScrolls.IsValidIndex(tab), TEXT("tab %d has no scroll box"), tab);
        return *TabScrolls[tab];
    }

    // ---- (33d-ii) The Help page's parts (SOLMenuContent::FillHelpTabs fills them, RefreshHelpPage refreshes them) ---

    // Sets the Keyboard tab's diagram (the page owns it, as USOLKeyboardDiagramSmokePage does) and lays it out at a
    // starting width (> 0: the page's first frame paints it; FitDiagram corrects it once the rows are laid out)
    void SetDiagram(USOLKeyboardDiagram* diagram, float startLayoutWidth);

    // Returns the Keyboard tab's diagram, or null (verification)
    USOLKeyboardDiagram* GetDiagram() const { return Diagram; }

    // D45: returns the diagram's hover popup layer (null without a diagram)
    USOLKeyboardDiagramPopup* GetDiagramPopup() const { return DiagramPopup; }

    // Sets the Keyboard tab's line under the diagram
    void SetKeyboardHint(UTextBlock* hint) { KeyboardHint = hint; }

    // Returns the Keyboard tab's line under the diagram, or null (verification)
    UTextBlock* GetKeyboardHint() const { return KeyboardHint; }

    // Empties the glossary rows and reserves room for count
    void ResetGlossaryRows(int32 count) { GlossaryRows.Reset(count); }

    // Adds a glossary row (called by SOLMenuContent while it fills the glossary tabs)
    void AddGlossaryRow(const FSOLHelpGlossaryRow& row) { GlossaryRows.Add(row); }

    // Returns every glossary row, in tab and table order (verification)
    const TArray<FSOLHelpGlossaryRow>& GetGlossaryRows() const { return GlossaryRows; }

    // Returns the glossary rows for RefreshHelpPage to update their last written texts
    TArray<FSOLHelpGlossaryRow>& GetGlossaryRowsForRefresh() { return GlossaryRows; }

    // Returns the number of glossary rows of a tab (verification)
    int32 GetGlossaryRowCount(int32 tab) const;

    // Returns how often the key bindings changed handler ran (verification: the live refresh came from the event)
    int32 GetBindingsChangedCount() const { return mBindingsChangedCount; }

protected:

    // Binds the rail's hover traces to its trace layer, and (Help) the key bindings changed event
    virtual void NativeConstruct() override;

    // (33d-ii) Unbinds the key bindings changed event
    virtual void NativeDestruct() override;

    // Ticks the rail's buttons (highlight) and the trace layer's strength
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:

    // A tab button was clicked (index = the tab's position)
    void HandleTabClicked(int32 index);

    // Returns the rows' laid-out width: the content switcher's less a scroll bar's room (taken as shown, so every tab
    // gets the same width); 0 before the first layout (the one row-width rule FitLeftColumn and FitDiagram share)
    float GetContentRowWidth() const;

    // (46i) Sets the left column's width for the rows' laid-out width (SOLPageTabs::LeftColumnWidth), only on a change
    void FitLeftColumn();

    // (33d-ii) Lays the diagram out in the rows' width when it changed by more than DIAGRAM_WIDTH_TOLERANCE
    void FitDiagram();

    // (33d-ii) Lets the shown glossary tab's symbol views rebuild when their size or the strength changed (hidden
    // tabs' views catch up when shown); the size at the page's current layout scale, so every row of a newly shown tab
    // (also one scrolled out of view) is built before its first paint
    void UpdateShownSymbolViews(float strength, float layoutScale);

    // (33d-ii) Refreshes the Help page from the live keys (the key bindings changed handler)
    void HandleKeyBindingsChanged();

    UPROPERTY(Transient)
    FSOLSolidTabRail Rail;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UScrollBox>> TabScrolls;      // Each tab's rows

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> TabSwitcher;        // Shows the selected tab's scroll box

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLKeyCapRow>> KeyCells;     // SOLMenuContent fills these two (Controls, Info)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> KeyTexts;

    UPROPERTY(Transient)
    TArray<FSOLReadOnlyColumnText> ColumnTexts;

    UPROPERTY(Transient)
    TObjectPtr<USOLKeyboardDiagram> Diagram;        // (33d-ii) Help's Keyboard tab

    UPROPERTY(Transient)
    TObjectPtr<USOLKeyboardDiagramPopup> DiagramPopup; // D45: its hover popup layer over the whole page

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> KeyboardHint;

    UPROPERTY(Transient)
    TArray<FSOLHelpGlossaryRow> GlossaryRows;

    TUniquePtr<FSOLHudSymbolScratch> mSymbolScratch;    // (33d-ii) Help only: the views' shared build scratch (D8)
    FDelegateHandle mBindingsHandle;                // (33d-ii) Help only: OnKeyBindingsChanged
    int32 mBindingsChangedCount = 0;
    float mDiagramWidth = 0.0f;                     // The diagram's last layout width (0: not laid out yet)
    ESOLReadOnlyPage mPage = ESOLReadOnlyPage::Count;
    int32 mTab = 0;
    float mLeftColumnWidth = 0.0f;                  // The left column's width now (every row's box has it)
};
