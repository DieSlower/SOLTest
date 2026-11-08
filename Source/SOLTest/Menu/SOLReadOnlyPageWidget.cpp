/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLReadOnlyPageWidget.h"

#include "Menu/SOLKeyboardDiagram.h"
#include "Menu/SOLKeyCapRow.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsSubsystem.h"
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
        case ESOLReadOnlyPage::Help:
            return SOLMenuText::HELP_TITLE;
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
    if (page == ESOLReadOnlyPage::Help)
    {
        mSymbolScratch = MakeUnique<FSOLHudSymbolScratch>();
    }
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(this);
    UVerticalBox* root = SOLMenuContent::MakePage(*WidgetTree, ReadOnlyPageTitle(page));
    WidgetTree->RootWidget = root;
    UHorizontalBox* body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    root->AddChildToVerticalBox(body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    // The tab rail, with its group captions (33d-ii: only Help has any; the other pages pass none and lay out as
    // before)
    TArray<FString> labels;
    TArray<FString> captions;
    const int32 tabCount = SOLPageTabs::TabCount(page);
    labels.Reserve(tabCount);
    captions.Reserve(tabCount);
    for (int32 tab = 0; tab < tabCount; ++tab)
    {
        labels.Add(SOLPageTabs::TabLabel(page, tab));
        captions.Add(SOLPageTabs::TabCaption(page, tab));
    }
    FSOLMenuButtonClicked onClicked;
    onClicked.BindUObject(this, &USOLReadOnlyPageWidget::HandleTabClicked);
    body->AddChildToHorizontalBox(Rail.Build(*WidgetTree, labels, onClicked, captions))->SetPadding(
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
    Rail.ScrollToTop();
    Rail.ClearStaleHover();
    if (Diagram != nullptr)
    {
        Diagram->SetHoveredKey(INDEX_NONE);
    }
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
// Adds a two-column row (its left text, box and right cell) for the column fit and the fit checks
void USOLReadOnlyPageWidget::RegisterColumnText(UTextBlock* text, USizeBox* box, UTextBlock* value, const int32 tab,
    const float width)
{
    FSOLReadOnlyColumnText& column = ColumnTexts.AddDefaulted_GetRef();
    column.Text = text;
    column.Box = box;
    column.Value = value;
    column.Tab = tab;
    column.Width = width;
    mLeftColumnWidth = width;
}

//////////////////////////////////////////////////////////////////////////
// Adds a two-column row whose left cell is key caps, wrapping in the column's width
void USOLReadOnlyPageWidget::RegisterColumnCaps(USOLKeyCapRow* caps, USizeBox* box, UTextBlock* value, const int32 tab,
    const float width)
{
    RegisterColumnText(nullptr, box, value, tab, width);
    ColumnTexts.Last().Caps = caps;
    if (caps != nullptr)
    {
        caps->SetWrapWidth(width);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of glossary rows of a tab
int32 USOLReadOnlyPageWidget::GetGlossaryRowCount(const int32 tab) const
{
    int32 count = 0;
    for (const FSOLHelpGlossaryRow& row : GlossaryRows)
    {
        count += row.Tab == tab ? 1 : 0;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns the rows' laid-out width: the content switcher's less a scroll bar's room, 0 before the first layout
float USOLReadOnlyPageWidget::GetContentRowWidth() const
{
    if (TabSwitcher == nullptr)
    {
        return 0.0f;
    }
    const float switcherWidth = TabSwitcher->GetCachedGeometry().GetLocalSize().X;
    return switcherWidth > 0.0f ? switcherWidth - SOL::SolidMenu::SCROLL_BAR_THICKNESS - SOL::MenuStyle::BUTTON_GAP
        : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Sets the left column's width for the rows' laid-out width, only on a change
void USOLReadOnlyPageWidget::FitLeftColumn()
{
    // Before the first layout the switcher has no size and the full column stays (LeftColumnWidth at 0)
    if (TabSwitcher == nullptr || ColumnTexts.IsEmpty())
    {
        return;
    }
    const float width = SOLPageTabs::LeftColumnWidth(mPage, GetContentRowWidth());
    if (width == mLeftColumnWidth)
    {
        return;
    }
    mLeftColumnWidth = width;
    for (FSOLReadOnlyColumnText& column : ColumnTexts)
    {
        column.Width = width;
        if (column.Box != nullptr)
        {
            column.Box->SetWidthOverride(width);
        }
        if (column.Caps != nullptr)
        {
            column.Caps->SetWrapWidth(width);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// (33d-ii) Sets the Keyboard tab's diagram and lays it out at its starting width, the width FitDiagram compares with
void USOLReadOnlyPageWidget::SetDiagram(USOLKeyboardDiagram* diagram, const float startLayoutWidth)
{
    Diagram = diagram;
    mDiagramWidth = 0.0f;
    if (Diagram != nullptr && startLayoutWidth > 0.0f)
    {
        Diagram->SetLayoutWidth(startLayoutWidth);
        mDiagramWidth = startLayoutWidth;
    }
}

//////////////////////////////////////////////////////////////////////////
// (33d-ii) Lays the diagram out in the rows' width, only when it changed by more than the tolerance
void USOLReadOnlyPageWidget::FitDiagram()
{
    if (Diagram == nullptr)
    {
        return;
    }
    const float width = GetContentRowWidth();
    if (width > 0.0f && FMath::Abs(width - mDiagramWidth) > SOL::Help::DIAGRAM_WIDTH_TOLERANCE)
    {
        mDiagramWidth = width;
        Diagram->SetLayoutWidth(width);
    }
}

//////////////////////////////////////////////////////////////////////////
// (33d-ii) Lets the shown glossary tab's symbol views rebuild when their size (at the page's current layout scale)
// or the strength changed
void USOLReadOnlyPageWidget::UpdateShownSymbolViews(const float strength, const float layoutScale)
{
    if (!mSymbolScratch.IsValid())
    {
        return;
    }
    for (const FSOLHelpGlossaryRow& row : GlossaryRows)
    {
        if (row.Tab == mTab && row.View != nullptr)
        {
            row.View->UpdateBatch(strength, *mSymbolScratch, layoutScale);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// (33d-ii) Refreshes the Help page from the live keys
void USOLReadOnlyPageWidget::HandleKeyBindingsChanged()
{
    ++mBindingsChangedCount;
    SOLMenuContent::RefreshHelpPage(*this, SOLMenuContent::ResolveLiveKeys(GetWorld()));
}

//////////////////////////////////////////////////////////////////////////
// Binds the rail's hover traces to its trace layer, and (Help) the key bindings changed event
void USOLReadOnlyPageWidget::NativeConstruct()
{
    Super::NativeConstruct();
    Rail.Bind();
    USOLSettingsSubsystem* settings = mPage == ESOLReadOnlyPage::Help ? USOLSettingsSubsystem::Get(this) : nullptr;
    if (settings != nullptr && !mBindingsHandle.IsValid())
    {
        mBindingsHandle = settings->OnKeyBindingsChanged().AddUObject(this,
            &USOLReadOnlyPageWidget::HandleKeyBindingsChanged);
    }
}

//////////////////////////////////////////////////////////////////////////
// (33d-ii) Unbinds the key bindings changed event
void USOLReadOnlyPageWidget::NativeDestruct()
{
    USOLSettingsSubsystem* settings = mBindingsHandle.IsValid() ? USOLSettingsSubsystem::Get(this) : nullptr;
    if (settings != nullptr)
    {
        settings->OnKeyBindingsChanged().Remove(mBindingsHandle);
    }
    mBindingsHandle.Reset();
    Super::NativeDestruct();
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the rail's highlights and its trace layer's strength, fits the left column to the rows and (Help) the
// diagram to their width, and lets the shown glossary tab's views rebuild on a size or strength change
void USOLReadOnlyPageWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    Rail.Tick(frame, false);
    FitLeftColumn();
    if (mPage == ESOLReadOnlyPage::Help)
    {
        FitDiagram();
        UpdateShownSymbolViews(frame.Strength, geometry.Scale);
    }
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
