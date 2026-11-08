/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLPageTabs.h"

#include "Menu/SOLGlossary.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

namespace
{
    // The Info and About tabs' labels, in tab order: their lengths are the pages' tab counts
    const TCHAR* const PAGE_TABS_INFO_LABELS[] = { SOLMenuText::INFO_FLIGHT, SOLMenuText::INFO_TIME_TAB,
        SOLMenuText::INFO_COMBAT, SOLMenuText::INFO_HOW_IT_WORKS };
    const TCHAR* const PAGE_TABS_ABOUT_LABELS[] = { SOLMenuText::ABOUT_TAB_GAME, SOLMenuText::ABOUT_CREDITS_TITLE };
    static_assert(UE_ARRAY_COUNT(PAGE_TABS_INFO_LABELS) == static_cast<int32>(ESOLInfoTab::Count),
        "one label per Info tab (SOLMenuContent::FillInfoTabs fills each ESOLInfoTab)");
    static_assert(UE_ARRAY_COUNT(PAGE_TABS_ABOUT_LABELS) == static_cast<int32>(ESOLAboutTab::Count),
        "one label per About tab (SOLMenuContent::FillAboutTabs fills each ESOLAboutTab)");
    static_assert(static_cast<int32>(ESOLHelpTab::Count) == 5,
        "the Help page's tabs are SOLGlossary's (TabCount, TabLabel and TabCaption route Help through it)");

    //////////////////////////////////////////////////////////////////////////
    // Counts the Controls tabs: one per run of rows with the same area
    int32 PageTabsControlsCount()
    {
        int32 count = 0;
        const TCHAR* area = nullptr;
        for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
        {
            if (area == nullptr || FCString::Strcmp(area, row.Area) != 0)
            {
                area = row.Area;
                ++count;
            }
        }
        return count;
    }
}

//////////////////////////////////////////////////////////////////////////
// The tab every page opens on
int32 SOLPageTabs::OpeningTab()
{
    return 0;
}

//////////////////////////////////////////////////////////////////////////
// The number of tabs of a page
int32 SOLPageTabs::TabCount(const ESOLReadOnlyPage page)
{
    switch (page)
    {
    case ESOLReadOnlyPage::Controls:
        return PageTabsControlsCount();
    case ESOLReadOnlyPage::Info:
        return UE_ARRAY_COUNT(PAGE_TABS_INFO_LABELS);
    case ESOLReadOnlyPage::About:
        return UE_ARRAY_COUNT(PAGE_TABS_ABOUT_LABELS);
    case ESOLReadOnlyPage::Help:
        return SOLGlossary::TabCount();
    default:
        return 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// A tab's label (empty for an invalid page or tab)
FString SOLPageTabs::TabLabel(const ESOLReadOnlyPage page, const int32 tab)
{
    if (tab < 0 || tab >= TabCount(page))
    {
        return FString();
    }
    switch (page)
    {
    case ESOLReadOnlyPage::Controls:
    {
        // The area of the first row of the tab-th run
        int32 index = -1;
        const TCHAR* area = nullptr;
        for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
        {
            if (area == nullptr || FCString::Strcmp(area, row.Area) != 0)
            {
                area = row.Area;
                if (++index == tab)
                {
                    return area;
                }
            }
        }
        return FString();
    }
    case ESOLReadOnlyPage::Info:
        return PAGE_TABS_INFO_LABELS[tab];
    case ESOLReadOnlyPage::About:
        return PAGE_TABS_ABOUT_LABELS[tab];
    case ESOLReadOnlyPage::Help:
        return SOLGlossary::TabLabel(static_cast<ESOLHelpTab>(tab));
    default:
        return FString();
    }
}

//////////////////////////////////////////////////////////////////////////
// A tab's group caption: only the Help page has captions (SOLGlossary's)
FString SOLPageTabs::TabCaption(const ESOLReadOnlyPage page, const int32 tab)
{
    if (page != ESOLReadOnlyPage::Help || tab < 0 || tab >= TabCount(page))
    {
        return FString();
    }
    return SOLGlossary::TabCaption(static_cast<ESOLHelpTab>(tab));
}

//////////////////////////////////////////////////////////////////////////
// The tab that shows after a click
int32 SOLPageTabs::SelectTab(const int32 current, const int32 requested, const int32 count)
{
    if (requested >= 0 && requested < count)
    {
        return requested;
    }
    return current >= 0 && current < count ? current : OpeningTab();
}

//////////////////////////////////////////////////////////////////////////
// Whether a click changes the tab (a re-click on the shown tab does not)
bool SOLPageTabs::IsTabChange(const int32 current, const int32 requested, const int32 count)
{
    return requested >= 0 && requested < count && requested != current;
}

//////////////////////////////////////////////////////////////////////////
// The Controls tab that shows a CONTROL_ROWS row
int32 SOLPageTabs::ControlsTabOfRow(const int32 row)
{
    if (row < 0 || row >= static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)))
    {
        return INDEX_NONE;
    }
    int32 tab = 0;
    for (int32 index = 1; index <= row; ++index)
    {
        if (FCString::Strcmp(SOLMenuText::CONTROL_ROWS[index].Area, SOLMenuText::CONTROL_ROWS[index - 1].Area) != 0)
        {
            ++tab;
        }
    }
    return tab;
}

//////////////////////////////////////////////////////////////////////////
// The width of a page's left column in a row of rowWidth units: Controls keeps its key column, Info narrows its label
// column when the value column right of it would be short of room, Help keeps its symbol column, About has none
float SOLPageTabs::LeftColumnWidth(const ESOLReadOnlyPage page, const float rowWidth)
{
    using namespace SOL::MenuStyle;
    switch (page)
    {
    case ESOLReadOnlyPage::Controls:
        return KEY_COLUMN_WIDTH;
    case ESOLReadOnlyPage::Info:
        // A row not laid out yet (not a positive number) or one with room for the value column keeps the full column
        return rowWidth > 0.0f && rowWidth < LABEL_COLUMN_WIDTH + BUTTON_GAP + VALUE_COLUMN_MIN_WIDTH
            ? LABEL_COLUMN_NARROW_WIDTH : LABEL_COLUMN_WIDTH;
    case ESOLReadOnlyPage::Help:
        // The glossary's symbol column at every width (SDD 33 D37: no narrow variant)
        return SOL::Help::GLOSSARY_SYMBOL_BOX;
    default:
        return 0.0f;
    }
}
