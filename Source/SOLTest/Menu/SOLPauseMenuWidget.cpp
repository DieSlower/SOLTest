/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLPauseMenuWidget.h"

#include "Menu/SOLDemoPageWidget.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyCapRow.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
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
    using SOLMenuState::EMenuTab;

    // The shell slides in from the left, from the side panel (as the main menu's from its column)
    const FVector2f PAUSE_MENU_SHELL_SLIDE_DIRECTION(-1.0f, 0.0f);

    // How far the side panel slides: its width plus the screen margin it sits in from, so it starts wholly past the
    // left screen edge (PauseSlideX adds PAUSE_SLIDE_MARGIN_PX)
    constexpr float PAUSE_MENU_SLIDE_WIDTH = SOL::SolidMenu::PAUSE_PANEL_WIDTH + SOL::MenuStyle::SCREEN_MARGIN;

    // The side panel slides in over PAUSE_SLIDE_S and out over CLOSE_S (SDD 46 section 7); every phase call on
    // mPanelAnim passes this pair (the page shell keeps PANEL_OPEN_S / CLOSE_S)
    constexpr double PAUSE_MENU_OPEN_S = SOL::SolidMenu::PAUSE_SLIDE_S;
    constexpr double PAUSE_MENU_CLOSE_S = SOL::SolidMenu::CLOSE_S;

    // The side panel's detail area: the paused text, or the Quit prompt and choices (switcher indices)
    constexpr int32 PAUSE_MENU_DETAIL_TEXT = 0;
    constexpr int32 PAUSE_MENU_DETAIL_QUIT = 1;

    // The shell switcher's empty child (Resume and Quit show no page); a page tab's child index is its EMenuTab value
    constexpr int32 PAUSE_MENU_NO_PAGE = 0;
    static_assert(static_cast<int32>(EMenuTab::Resume) == PAUSE_MENU_NO_PAGE && static_cast<int32>(EMenuTab::Controls)
        == 1 && static_cast<int32>(EMenuTab::Info) == 2 && static_cast<int32>(EMenuTab::Settings) == 3
        && static_cast<int32>(EMenuTab::Demo) == 4 && static_cast<int32>(EMenuTab::About) == 5,
        "the shell's switcher children follow EMenuTab (BuildShell)");

    //////////////////////////////////////////////////////////////////////////
    // Returns the shell switcher's child index for a tab (its own index for a page tab, the empty child otherwise)
    int32 PauseMenuPageIndex(const EMenuTab tab)
    {
        return SOLMenuState::IsPageTab(tab) ? static_cast<int32>(tab) : PAUSE_MENU_NO_PAGE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint) and makes the menu focusable
void USOLPauseMenuWidget::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    mOpenVisibility = GetVisibility();
    Menu = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLMenuSubsystem>() : nullptr;

    // Full-screen dim over the live view (its alpha follows the slide, written on a change); the side panel and the
    // page shell side by side, inset from the screen edges; the side panel takes the full height, the shell what room
    // is left right of it, up to PAGE_WIDTH x PAGE_HEIGHT, vertically centred
    DimBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    DimBorder->SetBrushColor(FLinearColor::Transparent);
    DimBorder->SetPadding(FMargin(SCREEN_MARGIN));
    DimBorder->SetHorizontalAlignment(HAlign_Left);
    DimBorder->SetVerticalAlignment(VAlign_Fill);
    WidgetTree->RootWidget = DimBorder;
    UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    DimBorder->SetContent(row);
    BuildSidePanel(*row);
    BuildShell(*row);
    ApplyPanelLook(SOLSolidMenu::EvaluatePausePanel(mPanelAnim, 0.0, 0.0f, PAUSE_MENU_SLIDE_WIDTH));
    ShowTab(EMenuTab::Resume);
}

//////////////////////////////////////////////////////////////////////////
// Builds the side panel: the hull with the title step, "PAUSED", the tab stack (each tab's hover trace and LED at a
// known place) and the detail area (the paused text or the Quit choices)
void USOLPauseMenuWidget::BuildSidePanel(UHorizontalBox& row)
{
    using namespace SOL::MenuStyle;
    using namespace SOL::SolidMenu;

    // The hull: chamfers and the title step; rivets and vents; full height in the row
    SidePanel = WidgetTree->ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass(), TEXT("SidePanel"));
    FSOLSolidHull hull = USOLSolidPanel::MakePanelHull();
    hull.TitleStepWidth = COLUMN_TITLE_STEP_WIDTH;
    hull.TitleStepHeight = COLUMN_TITLE_STEP_HEIGHT;
    SidePanel->SetHull(ESOLSolidStyle::Panel, hull);
    const float inset = PANEL_INSET_PX + COLUMN_PADDING;
    SidePanel->SetContentPadding(FMargin(inset));
    SidePanel->SetDecor(true, true);
    row.AddChildToHorizontalBox(SOLMenuContent::MakeSized(*WidgetTree, SidePanel, PAUSE_PANEL_WIDTH, 0.0f))
        ->SetVerticalAlignment(VAlign_Fill);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    SidePanel->SetContent(column);

    // "PAUSED" on the stepped top edge
    UTextBlock* title = SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::PAUSED_TITLE, TITLE_FONT_SIZE,
        SOLSolidStyle::BrightTextColor(), true);
    column->AddChildToVerticalBox(SOLMenuContent::MakeSized(*WidgetTree, title, 0.0f, COLUMN_TITLE_HEIGHT));
    column->AddChildToVerticalBox(SOLMenuContent::MakeSized(*WidgetTree, WidgetTree->ConstructWidget<USpacer>(
        USpacer::StaticClass()), 0.0f, COLUMN_STACK_GAP));

    // The tab stack, scrolling past STACK_MAX_VISIBLE_ENTRIES (its view starts at stackTop, panel local); each tab's
    // hover trace runs from its right end (the point) to the hull's right edge
    const TCHAR* tabLabels[] = { SOLMenuText::RESUME, SOLMenuText::CONTROLS, SOLMenuText::INFO, SOLMenuText::SETTINGS,
        SOLMenuText::DEMO, SOLMenuText::ABOUT, SOLMenuText::QUIT_TAB };
    static_assert(UE_ARRAY_COUNT(tabLabels) == TAB_COUNT, "one label per tab");
    const float stackTop = inset + COLUMN_TITLE_HEIGHT + COLUMN_STACK_GAP;
    column->AddChildToVerticalBox(TabStack.Build(*WidgetTree, BUTTON_HEIGHT, BUTTON_GAP, stackTop,
        STACK_MAX_VISIBLE_ENTRIES));
    TabButtons.Reset();
    for (int32 tab = 0; tab < TAB_COUNT; ++tab)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, tabLabels[tab], tab, BUTTON_FONT_SIZE, ESOLSolidButtonVariant::Stack);
        button->OnMenuClicked.BindUObject(this, &USOLPauseMenuWidget::HandleTabClicked);
        TabStack.AddItem(SOLMenuContent::MakeSized(*WidgetTree, button, 0.0f, BUTTON_HEIGHT));
        TabButtons.Add(button);
    }
    SOLMenuContent::MakeStackLights(TabStack, inset + BUTTON_LED_X_PX, PAUSE_PANEL_WIDTH - inset,
        PAUSE_PANEL_WIDTH - PANEL_INSET_PX, mTabTraces, mTabLeds);

    // The detail area under the stack: the paused text and key hint, or (Quit) the prompt and the two ways out
    DetailSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    column->AddChildToVerticalBox(DetailSwitcher)->SetPadding(FMargin(0.0f, COLUMN_HINT_GAP, 0.0f, 0.0f));
    UVerticalBox* pausedText = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    pausedText->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::PAUSED_BODY, BODY_FONT_SIZE,
        SOLSolidStyle::TextColor(), false, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BUTTON_GAP));
    pausedText->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::PAUSED_HINT,
        SMALL_FONT_SIZE, SOLSolidStyle::DimTextColor(), false, true));
    DetailSwitcher->AddChild(pausedText);
    UVerticalBox* quitChoices = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    quitChoices->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::QUIT_PROMPT,
        HEADING_FONT_SIZE, SOLSolidStyle::BrightTextColor(), true, true));
    const TCHAR* quitLabels[] = { SOLMenuText::QUIT_TO_MENU, SOLMenuText::QUIT_TO_DESKTOP };
    QuitButtons.Reset();
    for (int32 choice = 0; choice < UE_ARRAY_COUNT(quitLabels); ++choice)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, quitLabels[choice], choice, BUTTON_FONT_SIZE, ESOLSolidButtonVariant::Stack);
        button->OnMenuClicked.BindUObject(this, &USOLPauseMenuWidget::HandleQuitClicked);
        quitChoices->AddChildToVerticalBox(SOLMenuContent::MakeSized(*WidgetTree, button, 0.0f, BUTTON_HEIGHT))
            ->SetPadding(FMargin(0.0f, BUTTON_GAP, 0.0f, 0.0f));
        QuitButtons.Add(button);
    }

    // The hazard stripe under Quit to desktop, as under the main menu's Quit (SDD 46 section 5.2)
    quitChoices->AddChildToVerticalBox(SOLMenuContent::MakeHazardStripe(*WidgetTree,
        USOLSolidMenuSubsystem::Get(this), TEXT("HazardStripe")));
    DetailSwitcher->AddChild(quitChoices);
    column->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()))->SetSize(
        FSlateChildSize(ESlateSizeRule::Fill));
}

//////////////////////////////////////////////////////////////////////////
// Builds the page shell (SOLMenuContent::BuildPageShell) with the empty child and one child per page tab (index =
// EMenuTab), and binds its Back button
void USOLPauseMenuWidget::BuildShell(UHorizontalBox& row)
{
    // The pages, in EMenuTab order after the empty child (the static_assert above)
    PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    PageSwitcher->AddChild(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()));
    ControlsPage = SOLMenuContent::BuildControlsPage(*WidgetTree);
    ControlsKeyCells = ControlsPage->GetKeyCells();
    PageSwitcher->AddChild(ControlsPage);
    InfoPage = SOLMenuContent::BuildInfoPage(*WidgetTree);
    InfoKeyTexts = InfoPage->GetKeyTexts();
    PageSwitcher->AddChild(InfoPage);
    SettingsPage = SOLMenuContent::BuildSettingsPage(*WidgetTree);
    PageSwitcher->AddChild(SettingsPage);
    DemoPage = WidgetTree->ConstructWidget<USOLDemoPageWidget>(USOLDemoPageWidget::StaticClass());
    DemoPage->SetInFlight(true);
    PageSwitcher->AddChild(DemoPage);
    AboutPage = SOLMenuContent::BuildAboutPage(*WidgetTree);
    PageSwitcher->AddChild(AboutPage);

    // The shell right of the side panel, PANEL_GAP apart; Back is the same close as Esc on a page
    USOLMenuButton* back = nullptr;
    ShellPanel = SOLMenuContent::BuildPageShell(*WidgetTree, PageSwitcher, back);
    BackButton = back;
    BackButton->OnMenuClicked.BindUObject(this, &USOLPauseMenuWidget::HandleBackClicked);
    UHorizontalBoxSlot* shellSlot = row.AddChildToHorizontalBox(SOLMenuContent::MakeSized(*WidgetTree, ShellPanel,
        SOL::SolidMenu::PAGE_WIDTH, SOL::SolidMenu::PAGE_HEIGHT));
    shellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    shellSlot->SetVerticalAlignment(VAlign_Center);
    shellSlot->SetPadding(FMargin(SOL::MenuStyle::PANEL_GAP, 0.0f, 0.0f, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Added to the viewport: the rebuilt side panel has a new trace layer, so the tabs' hover traces and LEDs bind to it
void USOLPauseMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SOLMenuContent::BindStackLights(SidePanel != nullptr ? SidePanel->GetTraceLayer() : nullptr, TabButtons,
        mTabTraces, mTabLeds, TabStack);
}

//////////////////////////////////////////////////////////////////////////
// Starts the slide-in (from Closed: the cue, the power-up and the stack at its top; from Closing: a reversal only) and
// takes input again
void USOLPauseMenuWidget::PlayOpen()
{
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    mIsClosing = false;
    if (GetVisibility() != mOpenVisibility)
    {
        SetVisibility(mOpenVisibility);
    }

    // A reopen during the slide-out reverses it from where it is (SDD 46 section 4.6): the traces stay lit, the
    // LEDs do not blink again and the stack keeps its scroll
    if (SOLMenuContent::BeginPanelOpenWithCue(*this, mPanelAnim, frame.NowS, frame.Strength,
        PAUSE_MENU_OPEN_S, PAUSE_MENU_CLOSE_S))
    {
        mPanelOpenS = frame.NowS;
        mIsPowerUpRunning = true;
        TabStack.ScrollToTop();
    }

    // The first frame already shows the start of the slide and dark traces (no frame of the old look)
    SOLSolidMenu::AdvancePanel(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_OPEN_S, PAUSE_MENU_CLOSE_S);
    ApplyPanelLook(SOLSolidMenu::EvaluatePausePanel(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_SLIDE_WIDTH));
    TickPanelLights(frame.NowS, frame.Strength);
    if (mShellAnim.Phase != ESOLHoloMenuPhase::Closed)
    {
        TickShell(frame.NowS, frame.Strength);
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts the slide-out with the input already back: hit-test invisible at once, the shell closing with the panel
void USOLPauseMenuWidget::BeginClose()
{
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    mIsClosing = true;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    SOLSolidMenu::BeginPanelClose(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_OPEN_S, PAUSE_MENU_CLOSE_S);
    if (ShellPanel != nullptr && mShellAnim.Phase != ESOLHoloMenuPhase::Closed)
    {
        ShellPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
        SOLSolidMenu::BeginPanelClose(mShellAnim, frame.NowS, frame.Strength);
    }
    SOLSolidMenu::AdvancePanel(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_OPEN_S, PAUSE_MENU_CLOSE_S);
    ApplyPanelLook(SOLSolidMenu::EvaluatePausePanel(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_SLIDE_WIDTH));
    if (mShellAnim.Phase != ESOLHoloMenuPhase::Closed)
    {
        TickShell(frame.NowS, frame.Strength);
    }

    // At strength 0 the close is instant: the widget leaves now
    if (mPanelAnim.Phase == ESOLHoloMenuPhase::Closed)
    {
        mIsClosing = false;
        RemoveFromParent();
    }
}

//////////////////////////////////////////////////////////////////////////
// Closes at once: the panel and shell Closed, their closed look applied, the widget removed from the viewport
void USOLPauseMenuWidget::SnapClosed()
{
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    mIsClosing = false;
    mIsPowerUpRunning = false;
    mPanelAnim = FSOLHoloMenuAnim();
    mShellAnim = FSOLHoloMenuAnim();
    ApplyPanelLook(SOLSolidMenu::EvaluatePausePanel(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_SLIDE_WIDTH));
    TickShell(frame.NowS, frame.Strength);
    RemoveFromParent();
}

//////////////////////////////////////////////////////////////////////////
// Applies the side panel's slide (render translation only) and the dim's alpha, each only when it changed
void USOLPauseMenuWidget::ApplyPanelLook(const FSOLPausePanelLook& look)
{
    mPanelLook = look;
    if (SidePanel != nullptr && look.SlideX != mAppliedSlideX)
    {
        mAppliedSlideX = look.SlideX;
        SidePanel->SetRenderTranslation(FVector2D(look.SlideX, 0.0f));
    }
    if (DimBorder != nullptr && look.DimAlpha != mAppliedDimAlpha)
    {
        mAppliedDimAlpha = look.DimAlpha;
        DimBorder->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, look.DimAlpha));
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows a tab: its page in the shell (or the shell closing), the detail area and the selection marks
void USOLPauseMenuWidget::ShowTab(const EMenuTab tab)
{
    // Leaving Settings ends its visit (unapplied changes reverted, one write if owed); Controls, Info, About, Demo and
    // Settings re-read their state when shown (the tab pages open on their first tab)
    if (mTab == EMenuTab::Settings && tab != EMenuTab::Settings && SettingsPage != nullptr)
    {
        SettingsPage->NotifyHidden();
    }
    if (tab == EMenuTab::Settings && SettingsPage != nullptr)
    {
        SettingsPage->RefreshFromSettings();
    }
    if (tab == EMenuTab::Controls && ControlsPage != nullptr)
    {
        SOLMenuContent::RefreshControlsPage(ControlsKeyCells, SOLMenuContent::ResolveLiveKeys(GetWorld()));
        ControlsPage->ShowOpening();
    }
    if (tab == EMenuTab::About && AboutPage != nullptr)
    {
        AboutPage->ShowOpening();
    }
    if (tab == EMenuTab::Demo && DemoPage != nullptr)
    {
        DemoPage->RefreshFromMenu();
    }
    if (tab == EMenuTab::Info && InfoPage != nullptr)
    {
        SOLMenuContent::RefreshInfoPage(InfoKeyTexts, SOLMenuContent::ResolveLiveKeys(GetWorld()));
        InfoPage->ShowOpening();
    }
    mTab = tab;

    // The side panel's detail area: the Quit choices for Quit (Q4), the paused text otherwise
    if (DetailSwitcher != nullptr)
    {
        DetailSwitcher->SetActiveWidgetIndex(tab == EMenuTab::Quit ? PAUSE_MENU_DETAIL_QUIT : PAUSE_MENU_DETAIL_TEXT);
    }

    // The selected tab reads as selected, Resume included (user, 2026-11-06, Q9: the cyan underline and LED mark like
    // every other selected item)
    for (USOLMenuButton* button : TabButtons)
    {
        button->SetSelected(button->GetIndex() == static_cast<int32>(tab));
    }
    if (PageSwitcher == nullptr || ShellPanel == nullptr)
    {
        return;
    }

    // A page tab shows its page at once and opens the shell (a reopen during a close reverses it); Resume and Quit
    // close it, the old page on screen and input-transparent until the shell has closed (TickShell)
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    if (SOLMenuState::IsPageTab(tab))
    {
        PageSwitcher->SetActiveWidgetIndex(PauseMenuPageIndex(tab));
        ShellPanel->SetVisibility(ESlateVisibility::Visible);
        SOLMenuContent::BeginPanelOpenWithCue(*this, mShellAnim, frame.NowS, frame.Strength);
    }
    else if (mShellAnim.Phase != ESOLHoloMenuPhase::Closed)
    {
        ShellPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
        SOLSolidMenu::BeginPanelClose(mShellAnim, frame.NowS, frame.Strength);
    }
    TickShell(frame.NowS, frame.Strength);
}

//////////////////////////////////////////////////////////////////////////
// Runs the shell's open / close, faded with the side panel; once closed it is hidden and shows the empty child
void USOLPauseMenuWidget::TickShell(const double nowS, const float s)
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
            PageSwitcher->SetActiveWidgetIndex(PAUSE_MENU_NO_PAGE);
        }
        return;
    }
    ShellPanel->ApplyOpenLook(FMath::Min(mShellProgress, mPanelLook.Progress), PAUSE_MENU_SHELL_SLIDE_DIRECTION);
}

//////////////////////////////////////////////////////////////////////////
// Runs the side panel's trace power-up (from the open's start) and pulse, the LED blink and the idle pulse dot
void USOLPauseMenuWidget::TickPanelLights(const double nowS, const float s)
{
    if (SidePanel == nullptr)
    {
        return;
    }
    mTracePower = SOLSolidMenu::TracePower(nowS - mPanelOpenS, s);
    SidePanel->SetTraceLook(mTracePower, SOLSolidMenu::IdlePulse(nowS, s), s);
    const TSharedPtr<SSOLSolidTraceLayer> layer = SidePanel->GetTraceLayer();
    if (layer.IsValid())
    {
        mIsPowerUpRunning = SOLMenuContent::TickStackLights(*layer, TabButtons, nowS - mPanelOpenS, nowS, s,
            mIsPowerUpRunning);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the lower-edge traces (two rows ending at the content's bottom edge) when the side panel's laid-out height
// changed: it is the view's height, known only after a layout
void USOLPauseMenuWidget::RefreshEdgeTraces()
{
    using namespace SOL::SolidMenu;
    if (SidePanel == nullptr)
    {
        return;
    }
    const float height = static_cast<float>(SidePanel->GetCachedGeometry().GetLocalSize().Y);
    if (height <= 0.0f || height == mEdgeTraceHeight)
    {
        return;
    }
    mEdgeTraceHeight = height;
    const float inset = PANEL_INSET_PX + COLUMN_PADDING;
    const float buttonRight = PAUSE_PANEL_WIDTH - inset;
    FSOLSolidTrace traces[SOLMenuContent::EDGE_TRACE_COUNT];
    SOLMenuContent::MakeEdgeTraces(inset + LED_RADIUS_PX * 2.0f, buttonRight - LED_RADIUS_PX * 2.0f,
        height - inset - COLUMN_TRACE_GAP, traces);
    SidePanel->SetTraces(traces);
}

//////////////////////////////////////////////////////////////////////////
// Returns a Controls row's key text
FString USOLPauseMenuWidget::GetControlsRowText(const int32 row) const
{
    return ControlsKeyCells.IsValidIndex(row) && ControlsKeyCells[row] != nullptr
        ? ControlsKeyCells[row]->GetText() : FString();
}

//////////////////////////////////////////////////////////////////////////
// Returns an Info page key-naming text
FString USOLPauseMenuWidget::GetInfoKeyText(const int32 index) const
{
    return InfoKeyTexts.IsValidIndex(index) && InfoKeyTexts[index] != nullptr
        ? InfoKeyTexts[index]->GetText().ToString() : FString();
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's button, or nullptr
USOLMenuButton* USOLPauseMenuWidget::GetTabButton(const EMenuTab tab) const
{
    return TabButtons.IsValidIndex(static_cast<int32>(tab)) ? TabButtons[static_cast<int32>(tab)].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a Quit choice's button, or nullptr
USOLMenuButton* USOLPauseMenuWidget::GetQuitButton(const int32 choice) const
{
    return QuitButtons.IsValidIndex(choice) ? QuitButtons[choice].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the side panel shows the Quit prompt and choices
bool USOLPauseMenuWidget::AreQuitChoicesShown() const
{
    return DetailSwitcher != nullptr && DetailSwitcher->GetActiveWidgetIndex() == PAUSE_MENU_DETAIL_QUIT;
}

//////////////////////////////////////////////////////////////////////////
// Returns the page index the shell's switcher shows
int32 USOLPauseMenuWidget::GetShownPageIndex() const
{
    return PageSwitcher != nullptr ? PageSwitcher->GetActiveWidgetIndex() : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the side panel's trace layer shows a tab's hover trace at more than half its amount
bool USOLPauseMenuWidget::IsTabHoverTraceShown(const EMenuTab tab) const
{
    const int32 index = static_cast<int32>(tab);
    const TSharedPtr<SSOLSolidTraceLayer> layer = SidePanel != nullptr ? SidePanel->GetTraceLayer() : nullptr;
    return layer.IsValid() && index >= 0 && index < TAB_COUNT && layer->IsShowingHoverTrace(mTabTraces[index])
        && layer->GetHoverAmount() > 0.5f;
}

//////////////////////////////////////////////////////////////////////////
// Gives keyboard focus to the selected tab's button
void USOLPauseMenuWidget::FocusDefault()
{
    if (USOLMenuButton* tab = GetDefaultButton())
    {
        tab->SetKeyboardFocus();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the selected tab's button (null before the widget tree is built)
USOLMenuButton* USOLPauseMenuWidget::GetDefaultButton() const
{
    const int32 tab = static_cast<int32>(mTab);
    return TabButtons.IsValidIndex(tab) ? TabButtons[tab].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the button highlights, then runs the side panel's slide, dim, scroll and lights and the shell's open /
// close; a finished close removes the widget from the viewport
void USOLPauseMenuWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);

    // The highlights: the tabs and the Quit choices always, the shell's Back only while it shows; on the Settings tab
    // the mouse over one of its sliders, toggles or Overlay rows wins over a focused button (one highlight, 46e
    // follow-up; read live, the page ticks after this widget)
    const bool bHoverElsewhere = mTab == EMenuTab::Settings && SettingsPage != nullptr
        && SettingsPage->IsHoverOnFocuslessControl();
    for (USOLMenuButton* button : TabButtons)
    {
        button->RefreshHighlightAt(frame, bHoverElsewhere);
    }
    for (USOLMenuButton* button : QuitButtons)
    {
        button->RefreshHighlightAt(frame, bHoverElsewhere);
    }
    const bool bShellShown = mShellAnim.Phase != ESOLHoloMenuPhase::Closed;
    if (bShellShown && BackButton != nullptr)
    {
        BackButton->RefreshHighlightAt(frame, bHoverElsewhere);
    }

    // The side panel: slide and dim, the stack's scroll (it moves the tabs' hover traces and LEDs with it), the
    // lower-edge traces for its height, the power-up and pulse
    SOLSolidMenu::AdvancePanel(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_OPEN_S, PAUSE_MENU_CLOSE_S);
    ApplyPanelLook(SOLSolidMenu::EvaluatePausePanel(mPanelAnim, frame.NowS, frame.Strength, PAUSE_MENU_SLIDE_WIDTH));
    if (SidePanel != nullptr)
    {
        TabStack.Tick(SidePanel->GetTraceLayer(), TabButtons);
        RefreshEdgeTraces();
        TickPanelLights(frame.NowS, frame.Strength);
    }
    if (bShellShown)
    {
        TickShell(frame.NowS, frame.Strength);
    }

    // The slide-out has finished: the widget leaves the viewport (the input went back when it began)
    if (mIsClosing && mPanelAnim.Phase == ESOLHoloMenuPhase::Closed)
    {
        mIsClosing = false;
        RemoveFromParent();
    }
}

//////////////////////////////////////////////////////////////////////////
// Esc or Ctrl+F1 (SDD 11 section 3.9) closes an open page, else resumes (Q4; an auto-repeat of a held Esc does
// neither); every other key is left to the focused button and Slate navigation
FReply USOLPauseMenuWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    ++mSlateKeyCount;
    if (keyEvent.GetKey() == EKeys::Escape
        || SOLKeyBindings::IsMenuHotkey(keyEvent.GetKey().GetFName().ToString(), keyEvent.IsControlDown()))
    {
        // The Esc that opened the menu, held down, repeats into it: only a fresh press acts
        if (Menu != nullptr && !keyEvent.IsRepeat())
        {
            if (SOLMenuState::EscapeClosesPage(Menu->GetState()))
            {
                Menu->ClosePausePage();
            }
            else
            {
                Menu->RequestEscape();
            }
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
    if (static_cast<EMenuTab>(tab) == EMenuTab::Resume)
    {
        Menu->RequestResume();
        return;
    }
    Menu->SelectTab(static_cast<EMenuTab>(tab));
}

//////////////////////////////////////////////////////////////////////////
// A Quit choice button was clicked
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

//////////////////////////////////////////////////////////////////////////
// The shell's Back button was clicked: closes the page as Esc does
void USOLPauseMenuWidget::HandleBackClicked(const int32 /*index*/)
{
    if (Menu != nullptr)
    {
        Menu->ClosePausePage();
    }
}
