/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLPageTabs.h"

#include "Menu/SOLMenuText.h"

namespace
{
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
        return 4;
    case ESOLReadOnlyPage::About:
        return 2;
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
    {
        const TCHAR* const labels[] = { SOLMenuText::INFO_FLIGHT, SOLMenuText::INFO_TIME_TAB, SOLMenuText::INFO_COMBAT,
            SOLMenuText::INFO_HOW_IT_WORKS };
        return labels[tab];
    }
    case ESOLReadOnlyPage::About:
        return tab == 0 ? SOLMenuText::ABOUT_TAB_GAME : SOLMenuText::ABOUT_CREDITS_TITLE;
    default:
        return FString();
    }
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
