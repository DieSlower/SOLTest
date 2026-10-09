/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLPauseMenuWidget.h"

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Input/Events.h"

namespace
{
    // Pause tabs in bar order; their switcher pages are added in the same order (index = SOLMenuState::EMenuTab)
    constexpr int32 PAUSE_TAB_COUNT = static_cast<int32>(SOLMenuState::EMenuTab::Quit) + 1;

    //////////////////////////////////////////////////////////////////////////
    // Creates a menu button in the tree with a fixed height
    USOLMenuButton* PauseMenuMakeButton(UWidgetTree& tree, const TCHAR* label, const int32 index, const int32 fontSize,
        USizeBox*& outBox, const float height)
    {
        USOLMenuButton* button = tree.ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(tree, label, index, fontSize);
        outBox = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        outBox->SetHeightOverride(height);
        outBox->SetContent(button);
        return button;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint) and makes the menu focusable
void USOLPauseMenuWidget::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    Menu = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLMenuSubsystem>() : nullptr;

    // Full-screen dimmer holding a centred fixed-size panel
    UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    root->SetBrushColor(SCREEN_DIM);
    root->SetHorizontalAlignment(HAlign_Center);
    root->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = root;
    USizeBox* panelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    panelBox->SetWidthOverride(PAUSE_WIDTH);
    panelBox->SetHeightOverride(PAUSE_HEIGHT);
    root->SetContent(panelBox);
    UBorder* panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    panel->SetBrushColor(PANEL_BACKGROUND);
    panel->SetPadding(FMargin(PANEL_PADDING));
    panelBox->SetContent(panel);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    panel->SetContent(column);
    column->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::PAUSED_TITLE, HEADING_FONT_SIZE,
        ACCENT_COLOR, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BUTTON_GAP));

    // Tab bar: equal-width buttons
    UHorizontalBox* tabBar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    column->AddChildToVerticalBox(tabBar)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, PANEL_PADDING * 0.5f));
    const TCHAR* tabLabels[] = { SOLMenuText::RESUME, SOLMenuText::CONTROLS, SOLMenuText::INFO, SOLMenuText::SETTINGS,
        SOLMenuText::ABOUT, SOLMenuText::QUIT_TAB };
    static_assert(UE_ARRAY_COUNT(tabLabels) == PAUSE_TAB_COUNT, "one label per tab");
    TabButtons.Reset();
    for (int32 tab = 0; tab < PAUSE_TAB_COUNT; ++tab)
    {
        USizeBox* box = nullptr;
        USOLMenuButton* button = PauseMenuMakeButton(*WidgetTree, tabLabels[tab], tab, TAB_FONT_SIZE, box, TAB_HEIGHT);
        button->OnMenuClicked.BindUObject(this, &USOLPauseMenuWidget::HandleTabClicked);
        UHorizontalBoxSlot* tabSlot = tabBar->AddChildToHorizontalBox(box);
        tabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        tabSlot->SetPadding(FMargin(tab == 0 ? 0.0f : TAB_GAP, 0.0f, 0.0f, 0.0f));
        TabButtons.Add(button);
    }

    // Pages, one per tab
    PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    column->AddChildToVerticalBox(PageSwitcher)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    UVerticalBox* resumePage = SOLMenuContent::MakePage(*WidgetTree, SOLMenuText::RESUME);
    resumePage->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::PAUSED_BODY, BODY_FONT_SIZE,
        TEXT_COLOR, false, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BUTTON_GAP));
    resumePage->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::PAUSED_HINT, SMALL_FONT_SIZE,
        DIM_TEXT_COLOR, false, true));
    PageSwitcher->AddChild(resumePage);
    PageSwitcher->AddChild(SOLMenuContent::BuildControlsPage(*WidgetTree));
    PageSwitcher->AddChild(SOLMenuContent::BuildInfoPage(*WidgetTree));
    PageSwitcher->AddChild(SOLMenuContent::BuildSettingsPage(*WidgetTree));
    PageSwitcher->AddChild(SOLMenuContent::BuildAboutPage(*WidgetTree));

    // Quit page: a prompt and the two ways out
    UVerticalBox* quitPage = SOLMenuContent::MakePage(*WidgetTree, SOLMenuText::QUIT_PROMPT);
    const TCHAR* quitLabels[] = { SOLMenuText::QUIT_TO_MENU, SOLMenuText::QUIT_TO_DESKTOP };
    QuitButtons.Reset();
    for (int32 choice = 0; choice < UE_ARRAY_COUNT(quitLabels); ++choice)
    {
        USizeBox* box = nullptr;
        USOLMenuButton* button = PauseMenuMakeButton(*WidgetTree, quitLabels[choice], choice, BUTTON_FONT_SIZE, box,
            BUTTON_HEIGHT);
        box->SetWidthOverride(COLUMN_WIDTH);
        button->OnMenuClicked.BindUObject(this, &USOLPauseMenuWidget::HandleQuitClicked);
        UVerticalBoxSlot* quitSlot = quitPage->AddChildToVerticalBox(box);
        quitSlot->SetHorizontalAlignment(HAlign_Left);
        quitSlot->SetPadding(FMargin(0.0f, BUTTON_GAP, 0.0f, 0.0f));
        QuitButtons.Add(button);
    }
    PageSwitcher->AddChild(quitPage);
    ShowTab(SOLMenuState::EMenuTab::Resume);
}

//////////////////////////////////////////////////////////////////////////
// Shows a tab's page and marks its tab button selected
void USOLPauseMenuWidget::ShowTab(const SOLMenuState::EMenuTab tab)
{
    mTab = tab;
    if (PageSwitcher != nullptr)
    {
        PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(tab));
    }
    for (USOLMenuButton* button : TabButtons)
    {
        button->SetSelected(button->GetIndex() == static_cast<int32>(tab));
    }
}

//////////////////////////////////////////////////////////////////////////
// Gives keyboard focus to the selected tab's button
void USOLPauseMenuWidget::FocusDefault()
{
    if (TabButtons.IsValidIndex(static_cast<int32>(mTab)))
    {
        TabButtons[static_cast<int32>(mTab)]->SetKeyboardFocus();
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the button highlights (focus follows the keyboard and the mouse)
void USOLPauseMenuWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    for (USOLMenuButton* button : TabButtons)
    {
        button->RefreshHighlight();
    }
    for (USOLMenuButton* button : QuitButtons)
    {
        button->RefreshHighlight();
    }
}

//////////////////////////////////////////////////////////////////////////
// Esc resumes; every other key is left to the focused button and Slate navigation
FReply USOLPauseMenuWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    ++mSlateKeyCount;
    if (keyEvent.GetKey() == EKeys::Escape)
    {
        if (Menu != nullptr)
        {
            Menu->RequestEscape();
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(geometry, keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// A tab button was clicked (index = SOLMenuState::EMenuTab)
void USOLPauseMenuWidget::HandleTabClicked(const int32 tab)
{
    if (Menu == nullptr)
    {
        return;
    }
    if (static_cast<SOLMenuState::EMenuTab>(tab) == SOLMenuState::EMenuTab::Resume)
    {
        Menu->RequestResume();
        return;
    }
    Menu->SelectTab(static_cast<SOLMenuState::EMenuTab>(tab));
}

//////////////////////////////////////////////////////////////////////////
// A Quit page button was clicked
void USOLPauseMenuWidget::HandleQuitClicked(const int32 choice)
{
    if (Menu == nullptr)
    {
        return;
    }
    if (static_cast<EQuitChoice>(choice) == EQuitChoice::ToMainMenu)
    {
        Menu->RequestQuitToMenu();
    }
    else
    {
        Menu->RequestQuitGame();
    }
}
