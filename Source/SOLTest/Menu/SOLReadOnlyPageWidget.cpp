/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLReadOnlyPageWidget.h"

#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a page's heading text
    const TCHAR* ReadOnlyPageTitle(const ESOLReadOnlyPage page)
    {
        switch (page)
        {
        case ESOLReadOnlyPage::Controls:
            return SOLMenuText::CONTROLS_TITLE;
        case ESOLReadOnlyPage::Info:
            return SOLMenuText::INFO_TITLE;
        default:
            return SOLMenuText::ABOUT_TITLE;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the heading, the tab rail beside the content recess (one scroll box per tab in a switcher) and the tabs'
// content
void USOLReadOnlyPageWidget::Setup(const ESOLReadOnlyPage page)
{
    using namespace SOL::MenuStyle;
    if (mPage != ESOLReadOnlyPage::Count || page == ESOLReadOnlyPage::Count)
    {
        return;
    }
    mPage = page;
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(this);
    UVerticalBox* root = SOLMenuContent::MakePage(*WidgetTree, ReadOnlyPageTitle(page));
    WidgetTree->RootWidget = root;
    UHorizontalBox* body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    root->AddChildToVerticalBox(body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    // The tab rail
    TArray<FString> labels;
    const int32 tabCount = SOLPageTabs::TabCount(page);
    labels.Reserve(tabCount);
    for (int32 tab = 0; tab < tabCount; ++tab)
    {
        labels.Add(SOLPageTabs::TabLabel(page, tab));
    }
    FSOLMenuButtonClicked onClicked;
    onClicked.BindUObject(this, &USOLReadOnlyPageWidget::HandleTabClicked);
    body->AddChildToHorizontalBox(Rail.Build(*WidgetTree, labels, onClicked))->SetPadding(
        FMargin(0.0f, 0.0f, SOL::SolidMenu::SETTINGS_BODY_GAP, 0.0f));

    // The content recess: one scroll box per tab in a switcher
    TabSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    const FScrollBarStyle scrollBarStyle = SOLSolidStyle::MakeScrollBarStyle(solid);
    TabScrolls.Reset(tabCount);
    for (int32 tab = 0; tab < tabCount; ++tab)
    {
        UScrollBox* scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
        scroll->SetWidgetBarStyle(scrollBarStyle);
        scroll->SetScrollbarThickness(FVector2D(SOL::SolidMenu::SCROLL_BAR_THICKNESS));
        scroll->SetScrollbarPadding(FMargin(BUTTON_GAP, 0.0f, 0.0f, 0.0f));
        TabSwitcher->AddChild(scroll);
        TabScrolls.Add(scroll);
    }
    body->AddChildToHorizontalBox(USOLSolidPanel::MakeRecess(*WidgetTree, TabSwitcher,
        SOL::SolidMenu::SETTINGS_CONTENT_PADDING))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    SOLMenuContent::FillReadOnlyTabs(*this, *WidgetTree);
    SelectTab(SOLPageTabs::OpeningTab());
}

//////////////////////////////////////////////////////////////////////////
// The page was shown: the first tab, every tab at its top, no stale hover
void USOLReadOnlyPageWidget::ShowOpening()
{
    SelectTab(SOLPageTabs::OpeningTab());
    for (UScrollBox* scroll : TabScrolls)
    {
        scroll->ScrollToStart();
    }
    Rail.ClearStaleHover();
}

//////////////////////////////////////////////////////////////////////////
// Shows a tab: the content switcher's child and the tab buttons' selected mark
void USOLReadOnlyPageWidget::SelectTab(const int32 tab)
{
    mTab = SOLPageTabs::SelectTab(mTab, tab, Rail.Buttons.Num());
    if (TabSwitcher != nullptr && TabSwitcher->GetActiveWidgetIndex() != mTab)
    {
        TabSwitcher->SetActiveWidgetIndex(mTab);
    }
    Rail.Select(mTab);
}

//////////////////////////////////////////////////////////////////////////
// Returns the content switcher's shown index
int32 USOLReadOnlyPageWidget::GetShownContentIndex() const
{
    return TabSwitcher != nullptr ? TabSwitcher->GetActiveWidgetIndex() : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's scroll box, or null
UScrollBox* USOLReadOnlyPageWidget::GetTabScroll(const int32 tab) const
{
    return TabScrolls.IsValidIndex(tab) ? TabScrolls[tab].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's scroll offset, or -1 for an invalid tab
float USOLReadOnlyPageWidget::GetTabScrollOffset(const int32 tab) const
{
    return TabScrolls.IsValidIndex(tab) ? TabScrolls[tab]->GetScrollOffset() : -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Adds a left-column text for the fit check
void USOLReadOnlyPageWidget::RegisterColumnText(const UTextBlock* text, const int32 tab, const float width)
{
    ColumnTexts.Add({ text, tab, width });
}

//////////////////////////////////////////////////////////////////////////
// Binds the rail's hover traces to its trace layer
void USOLReadOnlyPageWidget::NativeConstruct()
{
    Super::NativeConstruct();
    Rail.Bind();
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the rail's highlights and its trace layer's strength
void USOLReadOnlyPageWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    Rail.Tick(USOLSolidMenuSubsystem::BeginFrame(this), false);
}

//////////////////////////////////////////////////////////////////////////
// A tab button was clicked
void USOLReadOnlyPageWidget::HandleTabClicked(const int32 index)
{
    // A re-click on the shown tab is a no-op (no scroll reset)
    if (SOLPageTabs::IsTabChange(mTab, index, Rail.Buttons.Num()))
    {
        SelectTab(index);
    }
}
