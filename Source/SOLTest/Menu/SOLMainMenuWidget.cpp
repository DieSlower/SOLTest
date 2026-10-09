/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMainMenuWidget.h"

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Input/Events.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Wraps a widget in a fixed-size box
    USizeBox* MainMenuSized(UWidgetTree& tree, UWidget* content, const float width, const float height)
    {
        USizeBox* box = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        if (width > 0.0f)
        {
            box->SetWidthOverride(width);
        }
        if (height > 0.0f)
        {
            box->SetHeightOverride(height);
        }
        box->SetContent(content);
        return box;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint) and makes the menu focusable
void USOLMainMenuWidget::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    UWorld* world = GetWorld();
    Menu = world != nullptr ? world->GetSubsystem<USOLMenuSubsystem>() : nullptr;
    const USOLBodyRegistrySubsystem* registry = world != nullptr ? world->GetSubsystem<USOLBodyRegistrySubsystem>()
        : nullptr;

    // Full-screen transparent root; column and page panel side by side, vertically centred, inset from the left
    UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    root->SetBrushColor(FLinearColor::Transparent);
    root->SetPadding(FMargin(SCREEN_MARGIN));
    root->SetHorizontalAlignment(HAlign_Left);
    root->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = root;
    UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    root->SetContent(row);

    // Left column: title block, the entry buttons and the key hint, on a translucent panel
    UBorder* columnPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    columnPanel->SetBrushColor(PANEL_BACKGROUND);
    columnPanel->SetPadding(FMargin(PANEL_PADDING));
    row->AddChildToHorizontalBox(MainMenuSized(*WidgetTree, columnPanel, COLUMN_WIDTH + PANEL_PADDING * 2.0f, 0.0f))
        ->SetVerticalAlignment(VAlign_Center);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    columnPanel->SetContent(column);
    column->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::GAME_TITLE, TITLE_FONT_SIZE,
        TEXT_COLOR, true));
    column->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::GAME_TAGLINE, SUBTITLE_FONT_SIZE,
        TEXT_COLOR));
    column->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::STUDIO_NAME, SMALL_FONT_SIZE,
        ACCENT_COLOR, true))->SetPadding(FMargin(0.0f, BUTTON_GAP, 0.0f, PANEL_PADDING));
    const TCHAR* entryLabels[] = { SOLMenuText::PLAY, SOLMenuText::START_LOCATION_FORMAT, SOLMenuText::CONTROLS,
        SOLMenuText::SETTINGS, SOLMenuText::ABOUT, SOLMenuText::QUIT };
    static_assert(UE_ARRAY_COUNT(entryLabels) == static_cast<int32>(EEntry::Count), "one label per entry");
    EntryButtons.Reset();
    for (int32 entry = 0; entry < static_cast<int32>(EEntry::Count); ++entry)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, entryLabels[entry], entry, BUTTON_FONT_SIZE);
        button->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleEntryClicked);
        column->AddChildToVerticalBox(MainMenuSized(*WidgetTree, button, 0.0f, BUTTON_HEIGHT))->SetPadding(
            FMargin(0.0f, 0.0f, 0.0f, BUTTON_GAP));
        EntryButtons.Add(button);
    }
    column->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::MAIN_MENU_HINT, SMALL_FONT_SIZE,
        DIM_TEXT_COLOR, false, true))->SetPadding(FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f, 0.0f));

    // Page panel beside the column: one switcher child per page (index = ESOLMainMenuPage)
    PagePanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PagePanel"));
    PagePanel->SetBrushColor(PANEL_BACKGROUND);
    PagePanel->SetPadding(FMargin(PANEL_PADDING));
    row->AddChildToHorizontalBox(MainMenuSized(*WidgetTree, PagePanel, PAGE_WIDTH, PAGE_HEIGHT))->SetPadding(
        FMargin(PANEL_GAP, 0.0f, 0.0f, 0.0f));
    PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    PagePanel->SetContent(PageSwitcher);
    PageSwitcher->AddChild(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()));

    // Start-location picker: one row button per registry body (index = body index)
    UVerticalBox* startPage = SOLMenuContent::MakePage(*WidgetTree, SOLMenuText::START_LOCATION_TITLE);
    startPage->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::START_LOCATION_BODY,
        BODY_FONT_SIZE, DIM_TEXT_COLOR, false, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BUTTON_GAP));
    UScrollBox* bodyList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
    startPage->AddChildToVerticalBox(bodyList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    BodyButtons.Reset();
    const int32 bodyCount = registry != nullptr ? registry->GetRegistry().Num() : 0;
    for (int32 body = 0; body < bodyCount; ++body)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, registry->GetRegistry().GetName(body).ToString(), body, BODY_FONT_SIZE);
        button->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleBodyClicked);
        Cast<UScrollBoxSlot>(bodyList->AddChild(MainMenuSized(*WidgetTree, button, 0.0f, LIST_BUTTON_HEIGHT)))
            ->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, ROW_GAP));
        BodyButtons.Add(button);
    }
    PageSwitcher->AddChild(startPage);
    PageSwitcher->AddChild(SOLMenuContent::BuildControlsPage(*WidgetTree));
    PageSwitcher->AddChild(SOLMenuContent::BuildSettingsPage(*WidgetTree));
    PageSwitcher->AddChild(SOLMenuContent::BuildAboutPage(*WidgetTree));
    ShowPage(ESOLMainMenuPage::None);
    RefreshStartBody();
}

//////////////////////////////////////////////////////////////////////////
// Shows a page beside the column (None hides the panel)
void USOLMainMenuWidget::ShowPage(const ESOLMainMenuPage page)
{
    mPage = page;
    if (PageSwitcher == nullptr || PagePanel == nullptr)
    {
        return;
    }
    PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(page));
    PagePanel->SetVisibility(page == ESOLMainMenuPage::None ? ESlateVisibility::Hidden : ESlateVisibility::Visible);

    // The column button of the open page reads as selected
    const int32 pageEntry = page == ESOLMainMenuPage::StartLocation ? static_cast<int32>(EEntry::StartLocation)
        : page == ESOLMainMenuPage::Controls ? static_cast<int32>(EEntry::Controls)
        : page == ESOLMainMenuPage::Settings ? static_cast<int32>(EEntry::Settings)
        : page == ESOLMainMenuPage::About ? static_cast<int32>(EEntry::About) : INDEX_NONE;
    for (USOLMenuButton* button : EntryButtons)
    {
        button->SetSelected(button->GetIndex() == pageEntry);
    }
}

//////////////////////////////////////////////////////////////////////////
// Updates the start-location button label and the picker's selected row from the menu subsystem
void USOLMainMenuWidget::RefreshStartBody()
{
    if (Menu == nullptr || !EntryButtons.IsValidIndex(static_cast<int32>(EEntry::StartLocation)))
    {
        return;
    }
    EntryButtons[static_cast<int32>(EEntry::StartLocation)]->SetLabel(FString::Format(
        SOLMenuText::START_LOCATION_FORMAT, { Menu->GetStartBodyName() }));
    for (USOLMenuButton* button : BodyButtons)
    {
        button->SetSelected(button->GetIndex() == Menu->GetStartBodyIndex());
    }
}

//////////////////////////////////////////////////////////////////////////
// Gives keyboard focus to the Play button
void USOLMainMenuWidget::FocusDefault()
{
    if (EntryButtons.IsValidIndex(static_cast<int32>(EEntry::Play)))
    {
        EntryButtons[static_cast<int32>(EEntry::Play)]->SetKeyboardFocus();
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the button highlights (focus follows the keyboard and the mouse)
void USOLMainMenuWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    for (USOLMenuButton* button : EntryButtons)
    {
        button->RefreshHighlight();
    }
    if (mPage == ESOLMainMenuPage::StartLocation)
    {
        for (USOLMenuButton* button : BodyButtons)
        {
            button->RefreshHighlight();
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Esc closes the open page; every other key is left to the focused button and Slate navigation
FReply USOLMainMenuWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    if (keyEvent.GetKey() == EKeys::Escape)
    {
        if (mPage != ESOLMainMenuPage::None)
        {
            ShowPage(ESOLMainMenuPage::None);
            FocusDefault();
        }
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(geometry, keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// A column button was clicked
void USOLMainMenuWidget::HandleEntryClicked(const int32 entry)
{
    if (Menu == nullptr)
    {
        return;
    }
    // Page entries toggle their page; Play and Quit act through the menu subsystem
    const auto togglePage = [this](const ESOLMainMenuPage page)
    {
        ShowPage(mPage == page ? ESOLMainMenuPage::None : page);
    };
    switch (static_cast<EEntry>(entry))
    {
    case EEntry::Play:
        Menu->RequestPlay();
        break;
    case EEntry::StartLocation:
        togglePage(ESOLMainMenuPage::StartLocation);
        break;
    case EEntry::Controls:
        togglePage(ESOLMainMenuPage::Controls);
        break;
    case EEntry::Settings:
        togglePage(ESOLMainMenuPage::Settings);
        break;
    case EEntry::About:
        togglePage(ESOLMainMenuPage::About);
        break;
    case EEntry::Quit:
        Menu->RequestQuitGame();
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// A start-location row was clicked: makes that body the start body
void USOLMainMenuWidget::HandleBodyClicked(const int32 bodyIndex)
{
    if (Menu != nullptr)
    {
        Menu->SetStartBodyIndex(bodyIndex);
    }
}
