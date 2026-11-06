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
    Count,
};

/**
 * The tab logic of the Controls, Info and About pages (SDD 46 section 6.5, Q6, Q8), pure and widget-free. Controls: one
 * tab per area of SOLMenuText::CONTROL_ROWS, in table order (the rows of one area are contiguous; a new area starts a
 * new tab). Info: SOLMenuText::INFO_FLIGHT, INFO_TIME_TAB (the heading
 * "Time and travel" is too wide for a tab), INFO_COMBAT, INFO_HOW_IT_WORKS. About: SOLMenuText::ABOUT_TAB_GAME
 * and SOLMenuText::ABOUT_CREDITS_TITLE. Each page opens on its first tab every time (no last-tab memory).
 */
namespace SOLPageTabs
{
    // The tab every page opens on (the first, 0)
    SOLTEST_API int32 OpeningTab();

    // The number of tabs of a page; 0 for Count and anything past it
    SOLTEST_API int32 TabCount(ESOLReadOnlyPage page);

    // A tab's label; an empty string (never null text) for an invalid page or a tab outside [0, TabCount)
    SOLTEST_API FString TabLabel(ESOLReadOnlyPage page, int32 tab);

    // The tab that shows after a click on requested while current shows, with count tabs: requested when it is in
    // [0, count), otherwise current when it is in [0, count), otherwise OpeningTab()
    SOLTEST_API int32 SelectTab(int32 current, int32 requested, int32 count);

    // The Controls tab that shows a SOLMenuText::CONTROL_ROWS row; INDEX_NONE for a row outside the table
    SOLTEST_API int32 ControlsTabOfRow(int32 row);
}
