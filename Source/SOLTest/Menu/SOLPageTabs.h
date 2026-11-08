/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// The read-only menu pages with a tab column (SDD 46 section 6.5, Q6: each page's sections are its tabs)
enum class ESOLReadOnlyPage : uint8
{
    Controls,
    Info,
    About,
    Help,       // (33d-ii) SDD 33 D22: the Keys group and the HUD glossary
    Count,
};

// The Info page's tabs in order (SOLPageTabs' label table has one label per tab, checked at compile time)
enum class ESOLInfoTab : int32
{
    Flight,
    Time,
    Combat,
    HowItWorks,
    Count,
};

// The About page's tabs in order (as ESOLInfoTab)
enum class ESOLAboutTab : int32
{
    Game,
    Credits,
    Count,
};

/**
 * The tab logic of the Controls, Info and About pages (SDD 46 section 6.5, Q6, Q8), pure and widget-free. Controls: one
 * tab per area of SOLMenuText::CONTROL_ROWS, in table order (the rows of one area are contiguous; a new area starts a
 * new tab). Info: SOLMenuText::INFO_FLIGHT, INFO_TIME_TAB (the heading
 * "Time and travel" is too wide for a tab), INFO_COMBAT, INFO_HOW_IT_WORKS. About: SOLMenuText::ABOUT_TAB_GAME
 * and SOLMenuText::ABOUT_CREDITS_TITLE. Help (33d-ii): the SOLGlossary tabs (ESOLHelpTab). Each page opens on its
 * first tab every time (no last-tab memory).
 */
namespace SOLPageTabs
{
    // The tab every page opens on (the first, 0)
    SOLTEST_API int32 OpeningTab();

    // The number of tabs of a page; 0 for Count and anything past it
    SOLTEST_API int32 TabCount(ESOLReadOnlyPage page);

    // A tab's label; an empty string (never null text) for an invalid page or a tab outside [0, TabCount)
    SOLTEST_API FString TabLabel(ESOLReadOnlyPage page, int32 tab);

    // (33d-ii) The group caption drawn above a tab in the rail (Help: SOLGlossary::TabCaption); empty for every other
    // page, an invalid page and a tab outside [0, TabCount)
    SOLTEST_API FString TabCaption(ESOLReadOnlyPage page, int32 tab);

    // The tab that shows after a click on requested while current shows, with count tabs: requested when it is in
    // [0, count), otherwise current when it is in [0, count), otherwise OpeningTab()
    SOLTEST_API int32 SelectTab(int32 current, int32 requested, int32 count);

    // Whether a click on requested while current shows changes the tab: true only when requested is in [0, count) and
    // is not current. A re-click on the shown tab is a no-op (nothing resets, no scroll reset); an invalid one changes
    // nothing
    SOLTEST_API bool IsTabChange(int32 current, int32 requested, int32 count);

    // The Controls tab that shows a SOLMenuText::CONTROL_ROWS row; INDEX_NONE for a row outside the table
    SOLTEST_API int32 ControlsTabOfRow(int32 row);

    // (46i) The width of a page's left column (the Controls key cells, the Info labels) in rows rowWidth units wide:
    // Controls always KEY_COLUMN_WIDTH; Info LABEL_COLUMN_WIDTH while the value column right of it (after BUTTON_GAP)
    // keeps VALUE_COLUMN_MIN_WIDTH, else LABEL_COLUMN_NARROW_WIDTH (its labels wrap), and LABEL_COLUMN_WIDTH for a row
    // width that is not a positive finite number (not laid out yet); Help always GLOSSARY_SYMBOL_BOX (SDD 33 D37, the
    // glossary's symbol column); 0 for About and anything else (no left column)
    SOLTEST_API float LeftColumnWidth(ESOLReadOnlyPage page, float rowWidth);
}
