/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMainMenuWidget.h"

#include "Menu/SOLBuildVersion.h"
#include "Menu/SOLDemoPageWidget.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidStyle.h"
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
#include "Misc/EngineVersion.h"

namespace
{
    // Both panels slide in from the left (the column from the screen edge, the shell from the column)
    const FVector2f MAIN_MENU_SLIDE_DIRECTION(-1.0f, 0.0f);
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

    // Full-screen transparent root; the column and the page shell side by side, vertically centred, inset from the
    // left. The shell takes what room is left right of the column, up to PAGE_WIDTH (narrow views shrink it)
    UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    root->SetBrushColor(FLinearColor::Transparent);
    root->SetPadding(FMargin(SCREEN_MARGIN));
    root->SetHorizontalAlignment(HAlign_Left);
    root->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = root;
    UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    root->SetContent(row);
    BuildColumn(*row);
    BuildShell(*row);
    ShowPage(ESOLMainMenuPage::None);
    RefreshStartBody();
}

//////////////////////////////////////////////////////////////////////////
// Builds the hull column: title step, tagline, the stack, the hazard stripe, the hint, the lower-edge traces and the
// chin recess with the version line, every row at a fixed height so the traces sit at known places
void USOLMainMenuWidget::BuildColumn(UHorizontalBox& row)
{
    using namespace SOL::MenuStyle;
    using namespace SOL::SolidMenu;
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(this);

    // The hull: chamfers, the title step, the chin; rivets and vents
    ColumnPanel = WidgetTree->ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass(), TEXT("ColumnPanel"));
    FSOLSolidHull hull = USOLSolidPanel::MakePanelHull();
    hull.TitleStepWidth = COLUMN_TITLE_STEP_WIDTH;
    hull.TitleStepHeight = COLUMN_TITLE_STEP_HEIGHT;
    hull.ChinWidth = COLUMN_CHIN_WIDTH;
    hull.ChinHeight = COLUMN_CHIN_HEIGHT;
    ColumnPanel->SetHull(ESOLSolidStyle::Panel, hull);
    const float inset = PANEL_INSET_PX + COLUMN_PADDING;
    ColumnPanel->SetContentPadding(FMargin(inset, inset, inset, PANEL_INSET_PX + VERSION_RECESS_PADDING));
    ColumnPanel->SetDecor(true, true);
    row.AddChildToHorizontalBox(SOLMenuContent::MakeSized(*WidgetTree, ColumnPanel, SOL::SolidMenu::COLUMN_WIDTH,
        COLUMN_HEIGHT))->SetVerticalAlignment(VAlign_Center);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    ColumnPanel->SetContent(column);

    // Title (on the stepped top edge) and tagline
    UTextBlock* title = SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::GAME_TITLE, TITLE_FONT_SIZE,
        SOLSolidStyle::BrightTextColor(), true);
    column->AddChildToVerticalBox(SOLMenuContent::MakeSized(*WidgetTree, title, 0.0f, COLUMN_TITLE_HEIGHT));
    UTextBlock* tagline = SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::GAME_TAGLINE, SUBTITLE_FONT_SIZE,
        SOLSolidStyle::TextColor());
    column->AddChildToVerticalBox(SOLMenuContent::MakeSized(*WidgetTree, tagline, 0.0f, COLUMN_TAGLINE_HEIGHT));
    column->AddChildToVerticalBox(SOLMenuContent::MakeSized(*WidgetTree, WidgetTree->ConstructWidget<USpacer>(
        USpacer::StaticClass()), 0.0f, COLUMN_STACK_GAP));

    // The stack, scrolling past STACK_MAX_VISIBLE_ENTRIES (its view starts at stackTop, column local); each entry's
    // hover trace runs from its right end (the point) to the hull's right edge
    const TCHAR* entryLabels[] = { SOLMenuText::PLAY, SOLMenuText::DEMO, SOLMenuText::START_LOCATION_FORMAT,
        SOLMenuText::CONTROLS, SOLMenuText::SETTINGS, SOLMenuText::ABOUT, SOLMenuText::QUIT };
    static_assert(UE_ARRAY_COUNT(entryLabels) == static_cast<int32>(EEntry::Count), "one label per entry");
    const float stackTop = inset + COLUMN_TITLE_HEIGHT + COLUMN_TAGLINE_HEIGHT + COLUMN_STACK_GAP;
    column->AddChildToVerticalBox(EntryStack.Build(*WidgetTree, BUTTON_HEIGHT, BUTTON_GAP, stackTop,
        STACK_MAX_VISIBLE_ENTRIES));
    const float buttonRight = SOL::SolidMenu::COLUMN_WIDTH - inset;
    EntryButtons.Reset();
    for (int32 entry = 0; entry < static_cast<int32>(EEntry::Count); ++entry)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, entryLabels[entry], entry, BUTTON_FONT_SIZE, ESOLSolidButtonVariant::Stack);
        button->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleEntryClicked);
        EntryStack.AddItem(SOLMenuContent::MakeSized(*WidgetTree, button, 0.0f, BUTTON_HEIGHT));
        EntryButtons.Add(button);
    }
    SOLMenuContent::MakeStackLights(EntryStack, inset + BUTTON_LED_X_PX, buttonRight,
        SOL::SolidMenu::COLUMN_WIDTH - PANEL_INSET_PX, mEntryTraces, mEntryLeds);

    // The hazard stripe under Quit, then the key hint
    column->AddChildToVerticalBox(SOLMenuContent::MakeHazardStripe(*WidgetTree, solid, TEXT("HazardStripe")));
    HintText = SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::MAIN_MENU_HINT, SMALL_FONT_SIZE,
        SOLSolidStyle::DimTextColor(), false, true);
    column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.0f, COLUMN_HINT_GAP, 0.0f, 0.0f));
    column->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()))->SetSize(
        FSlateChildSize(ESlateSizeRule::Fill));

    // The chin recess, bottom-left: the version, studio and engine line
    VersionText = SOLMenuContent::MakeText(*WidgetTree, SOLBuildVersion::FormatVersionLine(
        SOLBuildVersion::CurrentVersion(), FEngineVersion::Current().ToString(EVersionComponent::Patch)),
        SMALL_FONT_SIZE, SOLSolidStyle::DimTextColor(), false, true);
    const float recessWidth = PANEL_INSET_PX + COLUMN_CHIN_WIDTH - inset;
    UVerticalBoxSlot* recessSlot = column->AddChildToVerticalBox(SOLMenuContent::MakeSized(*WidgetTree,
        USOLSolidPanel::MakeRecess(*WidgetTree, VersionText, VERSION_RECESS_PADDING), recessWidth,
        VERSION_RECESS_HEIGHT));
    recessSlot->SetHorizontalAlignment(HAlign_Left);

    // Circuit traces along the lower edge, above the recess: two rows, lit from their ring terminals
    const float rowB = COLUMN_HEIGHT - (PANEL_INSET_PX + VERSION_RECESS_PADDING) - VERSION_RECESS_HEIGHT
        - COLUMN_TRACE_GAP;
    const float rowA = rowB - COLUMN_TRACE_GAP;
    mTraceRowA = rowA;
    FSOLSolidTrace traces[SOLMenuContent::EDGE_TRACE_COUNT];
    SOLMenuContent::MakeEdgeTraces(inset + LED_RADIUS_PX * 2.0f, buttonRight - LED_RADIUS_PX * 2.0f, rowA, traces);
    ColumnPanel->SetTraces(traces);
}

//////////////////////////////////////////////////////////////////////////
// Builds the page shell (SOLMenuContent::BuildPageShell) with one switcher child per page (index = ESOLMainMenuPage)
// and binds its Back button
void USOLMainMenuWidget::BuildShell(UHorizontalBox& row)
{
    // The pages, one switcher child per page (index = ESOLMainMenuPage; the empty one for None)
    PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    PageSwitcher->AddChild(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()));
    PageSwitcher->AddChild(BuildStartLocationPage());
    ControlsPage = SOLMenuContent::BuildControlsPage(*WidgetTree);
    ControlsKeyCells = ControlsPage->GetKeyCells();
    PageSwitcher->AddChild(ControlsPage);
    SettingsPage = SOLMenuContent::BuildSettingsPage(*WidgetTree);
    PageSwitcher->AddChild(SettingsPage);
    AboutPage = SOLMenuContent::BuildAboutPage(*WidgetTree);
    PageSwitcher->AddChild(AboutPage);
    DemoPage = WidgetTree->ConstructWidget<USOLDemoPageWidget>(USOLDemoPageWidget::StaticClass());
    PageSwitcher->AddChild(DemoPage);

    // The shell right of the column, PANEL_GAP apart; Back is the same close as Esc
    USOLMenuButton* back = nullptr;
    ShellPanel = SOLMenuContent::BuildPageShell(*WidgetTree, PageSwitcher, back);
    BackButton = back;
    BackButton->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleBackClicked);
    UHorizontalBoxSlot* shellSlot = row.AddChildToHorizontalBox(SOLMenuContent::MakeSized(*WidgetTree, ShellPanel,
        SOL::SolidMenu::PAGE_WIDTH, SOL::SolidMenu::PAGE_HEIGHT));
    shellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    shellSlot->SetVerticalAlignment(VAlign_Center);
    shellSlot->SetPadding(FMargin(SOL::MenuStyle::PANEL_GAP, 0.0f, 0.0f, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Builds the start-location picker: the body text, then one row button per registry body (index = body index) in a
// recess list with the solid scroll bar
UWidget* USOLMainMenuWidget::BuildStartLocationPage()
{
    using namespace SOL::MenuStyle;
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(this);
    const UWorld* world = GetWorld();
    const USOLBodyRegistrySubsystem* registry = world != nullptr ? world->GetSubsystem<USOLBodyRegistrySubsystem>()
        : nullptr;
    UVerticalBox* page = SOLMenuContent::MakePage(*WidgetTree, SOLMenuText::START_LOCATION_TITLE);
    page->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::START_LOCATION_BODY,
        BODY_FONT_SIZE, SOLSolidStyle::DimTextColor(), false, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BUTTON_GAP));
    UScrollBox* bodyList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
    bodyList->SetWidgetBarStyle(SOLSolidStyle::MakeScrollBarStyle(solid));
    bodyList->SetScrollbarThickness(FVector2D(SOL::SolidMenu::SCROLL_BAR_THICKNESS));
    page->AddChildToVerticalBox(USOLSolidPanel::MakeRecess(*WidgetTree, bodyList,
        SOL::SolidMenu::LIST_RECESS_PADDING))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    BodyButtons.Reset();
    const int32 bodyCount = registry != nullptr ? registry->GetRegistry().Num() : 0;
    for (int32 body = 0; body < bodyCount; ++body)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, registry->GetRegistry().GetName(body).ToString(), body, BODY_FONT_SIZE,
            ESOLSolidButtonVariant::Small);
        button->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleBodyClicked);
        Cast<UScrollBoxSlot>(bodyList->AddChild(SOLMenuContent::MakeSized(*WidgetTree, button, 0.0f,
            LIST_BUTTON_HEIGHT)))->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, ROW_GAP));
        BodyButtons.Add(button);
    }
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Added to the viewport: the rebuilt column has a new trace layer, so the entries' hover traces bind to it; then the
// column opens
void USOLMainMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SOLMenuContent::BindStackLights(ColumnPanel != nullptr ? ColumnPanel->GetTraceLayer() : nullptr, EntryButtons,
        mEntryTraces, mEntryLeds, EntryStack);
    PlayShowAnimation();
}

//////////////////////////////////////////////////////////////////////////
// Starts the column's open, trace power-up and LED blink from now, and closes a shell that is not Closed at once
void USOLMainMenuWidget::PlayShowAnimation()
{
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    mColumnAnim = FSOLHoloMenuAnim();
    SOLMenuContent::BeginPanelOpenWithCue(*this, mColumnAnim, frame.NowS, frame.Strength);
    mColumnOpenS = frame.NowS;
    mColumnProgress = SOLSolidMenu::PanelProgress(mColumnAnim, frame.NowS, frame.Strength);
    mColumnTracePower = SOLSolidMenu::TracePower(0.0, frame.Strength);
    mIsPowerUpRunning = true;
    EntryStack.ScrollToTop();
    if (ColumnPanel != nullptr)
    {
        // The first frame already shows the start of the open (no frame of the old look)
        ColumnPanel->ApplyOpenLook(mColumnProgress, MAIN_MENU_SLIDE_DIRECTION);
        ColumnPanel->SetTraceLook(mColumnTracePower, SOLSolidMenu::IdlePulse(frame.NowS, frame.Strength),
            frame.Strength);
    }
    TickColumnLights(frame.NowS, frame.Strength);
    if (mPage != ESOLMainMenuPage::None || mShellAnim.Phase != ESOLHoloMenuPhase::Closed)
    {
        SnapShellClosed(frame.NowS, frame.Strength);
    }
}

//////////////////////////////////////////////////////////////////////////
// Closes the shell at once (SDD 46 section 6.2): a reshow never plays the close of a page left open at Play
void USOLMainMenuWidget::SnapShellClosed(const double nowS, const float s)
{
    if (mPage == ESOLMainMenuPage::Settings && SettingsPage != nullptr)
    {
        SettingsPage->NotifyHidden();
    }
    mPage = ESOLMainMenuPage::None;
    mShellAnim = FSOLHoloMenuAnim();
    TickShell(nowS, s);
    for (USOLMenuButton* button : EntryButtons)
    {
        button->SetSelected(false);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the power-up LED blink (until it ends) and the idle pulse dot on the column's trace layer
void USOLMainMenuWidget::TickColumnLights(const double nowS, const float s)
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = ColumnPanel != nullptr ? ColumnPanel->GetTraceLayer() : nullptr;
    if (layer.IsValid())
    {
        mIsPowerUpRunning = SOLMenuContent::TickStackLights(*layer, EntryButtons, nowS - mColumnOpenS, nowS, s,
            mIsPowerUpRunning);
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows a page in the shell beside the column (None closes the shell, which animates out after the input is back)
void USOLMainMenuWidget::ShowPage(const ESOLMainMenuPage page)
{
    // Leaving Settings ends its visit (unapplied changes reverted, one write if owed); Controls, Settings, About and
    // Demo re-read their state when shown (the tab pages open on their first tab)
    if (mPage == ESOLMainMenuPage::Settings && page != ESOLMainMenuPage::Settings && SettingsPage != nullptr)
    {
        SettingsPage->NotifyHidden();
    }
    if (page == ESOLMainMenuPage::Settings && SettingsPage != nullptr)
    {
        SettingsPage->RefreshFromSettings();
    }
    if (page == ESOLMainMenuPage::Demo && DemoPage != nullptr)
    {
        DemoPage->RefreshFromMenu();
    }
    if (page == ESOLMainMenuPage::Controls && ControlsPage != nullptr)
    {
        SOLMenuContent::RefreshControlsPage(ControlsKeyCells, SOLMenuContent::ResolveLiveKeys(GetWorld()));
        ControlsPage->ShowOpening();
    }
    if (page == ESOLMainMenuPage::About && AboutPage != nullptr)
    {
        AboutPage->ShowOpening();
    }
    const bool bWasOpen = mPage != ESOLMainMenuPage::None;
    mPage = page;
    if (PageSwitcher == nullptr || ShellPanel == nullptr)
    {
        return;
    }

    // Opening shows the page at once and starts the shell's open (a reopen during a close reverses it); closing keeps
    // the page on screen, input-transparent, until the shell has closed (TickShell)
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    if (page != ESOLMainMenuPage::None)
    {
        PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(page));
        ShellPanel->SetVisibility(ESlateVisibility::Visible);
        SOLMenuContent::BeginPanelOpenWithCue(*this, mShellAnim, frame.NowS, frame.Strength);
    }
    else if (bWasOpen)
    {
        ShellPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
        SOLSolidMenu::BeginPanelClose(mShellAnim, frame.NowS, frame.Strength);
    }
    TickShell(frame.NowS, frame.Strength);

    // The column button of the open page reads as selected
    const int32 pageEntry = page == ESOLMainMenuPage::StartLocation ? static_cast<int32>(EEntry::StartLocation)
        : page == ESOLMainMenuPage::Controls ? static_cast<int32>(EEntry::Controls)
        : page == ESOLMainMenuPage::Settings ? static_cast<int32>(EEntry::Settings)
        : page == ESOLMainMenuPage::About ? static_cast<int32>(EEntry::About)
        : page == ESOLMainMenuPage::Demo ? static_cast<int32>(EEntry::Demo) : INDEX_NONE;
    for (USOLMenuButton* button : EntryButtons)
    {
        button->SetSelected(button->GetIndex() == pageEntry);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the shell's open / close: the open look while it moves; once closed it is hidden and shows the empty page
void USOLMainMenuWidget::TickShell(const double nowS, const float s)
{
    if (ShellPanel == nullptr || PageSwitcher == nullptr)
    {
        return;
    }
    SOLSolidMenu::AdvancePanel(mShellAnim, nowS, s);
    mShellProgress = SOLSolidMenu::PanelProgress(mShellAnim, nowS, s);
    if (mShellAnim.Phase == ESOLHoloMenuPhase::Closed)
    {
        if (ShellPanel->GetVisibility() != ESlateVisibility::Hidden)
        {
            ShellPanel->SetVisibility(ESlateVisibility::Hidden);
            PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(ESOLMainMenuPage::None));
        }
        return;
    }
    ShellPanel->ApplyOpenLook(mShellProgress, MAIN_MENU_SLIDE_DIRECTION);
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
// Returns the column's entry button at an index, or null
USOLMenuButton* USOLMainMenuWidget::GetEntryButton(const int32 index) const
{
    return EntryButtons.IsValidIndex(index) ? EntryButtons[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a start-location row, or null
USOLMenuButton* USOLMainMenuWidget::GetBodyButton(const int32 index) const
{
    return BodyButtons.IsValidIndex(index) ? BodyButtons[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Re-applies the stack's visible-entry cap (verification)
void USOLMainMenuWidget::SetStackMaxVisibleEntriesForVerification(const int32 maxVisibleEntries)
{
    EntryStack.SetMaxVisibleItems(maxVisibleEntries);
}

//////////////////////////////////////////////////////////////////////////
// Returns the chin recess's version line as shown
FString USOLMainMenuWidget::GetVersionText() const
{
    return VersionText != nullptr ? VersionText->GetText().ToString() : FString();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the column's trace layer shows an entry's hover trace at more than half its amount
bool USOLMainMenuWidget::IsEntryHoverTraceShown(const int32 index) const
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = ColumnPanel != nullptr ? ColumnPanel->GetTraceLayer() : nullptr;
    return layer.IsValid() && index >= 0 && index < static_cast<int32>(EEntry::Count)
        && layer->IsShowingHoverTrace(mEntryTraces[index]) && layer->GetHoverAmount() > 0.5f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the page index the shell's switcher shows
int32 USOLMainMenuWidget::GetShownPageIndex() const
{
    return PageSwitcher != nullptr ? PageSwitcher->GetActiveWidgetIndex() : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the key hint's bottom edge in column-local units from the last layout, or -1 before one
float USOLMainMenuWidget::GetHintBottom() const
{
    if (HintText == nullptr || ColumnPanel == nullptr)
    {
        return -1.0f;
    }
    const FGeometry& hint = HintText->GetCachedGeometry();
    if (hint.GetLocalSize().Y <= 0.0f)
    {
        return -1.0f;
    }
    const FVector2D bottom = ColumnPanel->GetCachedGeometry().AbsoluteToLocal(hint.LocalToAbsolute(
        FVector2D(0.0, hint.GetLocalSize().Y)));
    return static_cast<float>(bottom.Y);
}

//////////////////////////////////////////////////////////////////////////
// Gives keyboard focus to the Play button
void USOLMainMenuWidget::FocusDefault()
{
    if (USOLMenuButton* play = GetDefaultButton())
    {
        play->SetKeyboardFocus();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the Play button (null before the widget tree is built)
USOLMenuButton* USOLMainMenuWidget::GetDefaultButton() const
{
    const int32 play = static_cast<int32>(EEntry::Play);
    return EntryButtons.IsValidIndex(play) ? EntryButtons[play].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Closes the open page and gives the focus back to Play (Esc and Back)
void USOLMainMenuWidget::ClosePage()
{
    if (mPage != ESOLMainMenuPage::None)
    {
        ShowPage(ESOLMainMenuPage::None);
        FocusDefault();
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the button highlights, then runs the column's open, trace power-up and pulse and the shell's open / close
void USOLMainMenuWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);

    // The highlights: the column's entries always, the shell's Back and start-location rows only while it shows; with
    // Settings open, the mouse over one of its sliders, toggles or Overlay rows wins over a focused button here too
    // (one highlight, 46e follow-up; read live, the page ticks after this widget)
    const bool bHoverElsewhere = mPage == ESOLMainMenuPage::Settings && SettingsPage != nullptr
        && SettingsPage->IsHoverOnFocuslessControl();
    for (USOLMenuButton* button : EntryButtons)
    {
        button->RefreshHighlightAt(frame, bHoverElsewhere);
    }
    const bool bShellShown = mShellAnim.Phase != ESOLHoloMenuPhase::Closed;
    if (bShellShown && BackButton != nullptr)
    {
        BackButton->RefreshHighlightAt(frame, bHoverElsewhere);
    }
    if (bShellShown && GetShownPageIndex() == static_cast<int32>(ESOLMainMenuPage::StartLocation))
    {
        for (USOLMenuButton* button : BodyButtons)
        {
            button->RefreshHighlightAt(frame);
        }
    }

    // The column: open look, then its traces' power-up (from the open's start) and pulse; the stack's scroll follows
    // the focused entry and moves the entries' hover traces and LEDs with it
    if (ColumnPanel != nullptr)
    {
        EntryStack.Tick(ColumnPanel->GetTraceLayer(), EntryButtons);
        SOLSolidMenu::AdvancePanel(mColumnAnim, frame.NowS, frame.Strength);
        mColumnProgress = SOLSolidMenu::PanelProgress(mColumnAnim, frame.NowS, frame.Strength);
        ColumnPanel->ApplyOpenLook(mColumnProgress, MAIN_MENU_SLIDE_DIRECTION);
        mColumnTracePower = SOLSolidMenu::TracePower(frame.NowS - mColumnOpenS, frame.Strength);
        ColumnPanel->SetTraceLook(mColumnTracePower, SOLSolidMenu::IdlePulse(frame.NowS, frame.Strength),
            frame.Strength);
        TickColumnLights(frame.NowS, frame.Strength);
    }
    if (bShellShown)
    {
        TickShell(frame.NowS, frame.Strength);
    }
}

//////////////////////////////////////////////////////////////////////////
// Esc (or Ctrl+F1, SDD 11 section 3.9) closes the open page; every other key is left to the focused button and Slate
// navigation
FReply USOLMainMenuWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    if (keyEvent.GetKey() == EKeys::Escape
        || SOLKeyBindings::IsMenuHotkey(keyEvent.GetKey().GetFName().ToString(), keyEvent.IsControlDown()))
    {
        ClosePage();
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
    case EEntry::Demo:
        togglePage(ESOLMainMenuPage::Demo);
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

//////////////////////////////////////////////////////////////////////////
// The shell's Back button was clicked: closes the page as Esc does
void USOLMainMenuWidget::HandleBackClicked(const int32 /*index*/)
{
    ClosePage();
}
