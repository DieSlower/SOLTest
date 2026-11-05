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
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Misc/EngineVersion.h"

namespace
{
    // Both panels slide in from the left (the column from the screen edge, the shell from the column)
    const FVector2f MAIN_MENU_SLIDE_DIRECTION(-1.0f, 0.0f);

    //////////////////////////////////////////////////////////////////////////
    // Wraps a widget in a fixed-size box (0 leaves that axis free)
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

    //////////////////////////////////////////////////////////////////////////
    // Returns the hull panels' corner chamfers (SDD 46 section 5.2) with no other feature
    FSOLSolidHull MainMenuPanelHull()
    {
        using namespace SOL::SolidMenu;
        FSOLSolidHull hull;
        hull.ChamferTL = PANEL_CHAMFER_TL;
        hull.ChamferTR = PANEL_CHAMFER_TR;
        hull.ChamferBR = PANEL_CHAMFER_BR;
        hull.ChamferBL = PANEL_CHAMFER_BL;
        return hull;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a recess panel (flat sunken fill) around a widget with a padding
    USOLSolidPanel* MainMenuRecess(UWidgetTree& tree, UWidget* content, const float padding)
    {
        using namespace SOL::SolidMenu;
        USOLSolidPanel* recess = tree.ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass());
        FSOLSolidHull hull;
        hull.ChamferTL = RECESS_CHAMFER_LARGE_PX;
        hull.ChamferTR = RECESS_CHAMFER_SMALL_PX;
        hull.ChamferBR = RECESS_CHAMFER_LARGE_PX;
        hull.ChamferBL = RECESS_CHAMFER_SMALL_PX;
        hull.bRecess = true;
        recess->SetHull(ESOLSolidStyle::Recess, hull);
        recess->SetContentPadding(FMargin(padding));
        recess->SetContent(content);
        return recess;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a trace through up to FSOLSolidTrace::MAX_POINTS points (column local UMG units)
    FSOLSolidTrace MainMenuTrace(std::initializer_list<FVector2f> points, const bool bRingTerminal)
    {
        FSOLSolidTrace trace;
        for (const FVector2f& point : points)
        {
            if (trace.Count < FSOLSolidTrace::MAX_POINTS)
            {
                trace.Points[trace.Count++] = point;
            }
        }
        trace.bRingTerminal = bRingTerminal;
        return trace;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the hazard stripe's brush: T_SOLSolidHazard (a stripe mask) tiled along X in amber, the mask's black
    // left black; a flat amber bar when the texture is missing (the palette colour, never the texture's)
    FSlateBrush MainMenuHazardBrush(const USOLSolidMenuSubsystem* solid)
    {
        using namespace SOL::SolidMenu;
        FSlateBrush brush;
        UTexture2D* texture = solid != nullptr ? solid->GetTexture(ESOLSolidTexture::Hazard) : nullptr;
        if (texture != nullptr)
        {
            brush.SetResourceObject(texture);
            brush.Tiling = ESlateBrushTileType::Horizontal;
        }
        brush.DrawAs = ESlateBrushDrawType::Image;
        brush.SetImageSize(FVector2f(HAZARD_TILE_WIDTH_PX, HAZARD_HEIGHT_PX));
        brush.TintColor = FSlateColor(SOLSolidStyle::Linear(AMBER_COLOR));
        return brush;
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
    FSOLSolidHull hull = MainMenuPanelHull();
    hull.TitleStepWidth = COLUMN_TITLE_STEP_WIDTH;
    hull.TitleStepHeight = COLUMN_TITLE_STEP_HEIGHT;
    hull.ChinWidth = COLUMN_CHIN_WIDTH;
    hull.ChinHeight = COLUMN_CHIN_HEIGHT;
    ColumnPanel->SetHull(ESOLSolidStyle::Panel, hull);
    const float inset = PANEL_INSET_PX + COLUMN_PADDING;
    ColumnPanel->SetContentPadding(FMargin(inset, inset, inset, PANEL_INSET_PX + VERSION_RECESS_PADDING));
    ColumnPanel->SetDecor(true, true);
    row.AddChildToHorizontalBox(MainMenuSized(*WidgetTree, ColumnPanel, SOL::SolidMenu::COLUMN_WIDTH, COLUMN_HEIGHT))
        ->SetVerticalAlignment(VAlign_Center);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    ColumnPanel->SetContent(column);

    // Title (on the stepped top edge) and tagline
    UTextBlock* title = SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::GAME_TITLE, TITLE_FONT_SIZE,
        SOLSolidStyle::BrightTextColor(), true);
    column->AddChildToVerticalBox(MainMenuSized(*WidgetTree, title, 0.0f, COLUMN_TITLE_HEIGHT));
    UTextBlock* tagline = SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::GAME_TAGLINE, SUBTITLE_FONT_SIZE,
        SOLSolidStyle::TextColor());
    column->AddChildToVerticalBox(MainMenuSized(*WidgetTree, tagline, 0.0f, COLUMN_TAGLINE_HEIGHT));
    column->AddChildToVerticalBox(MainMenuSized(*WidgetTree, WidgetTree->ConstructWidget<USpacer>(
        USpacer::StaticClass()), 0.0f, COLUMN_STACK_GAP));

    // The stack; each entry's hover trace runs from its right end (the point) to the hull's right edge
    const TCHAR* entryLabels[] = { SOLMenuText::PLAY, SOLMenuText::DEMO, SOLMenuText::START_LOCATION_FORMAT,
        SOLMenuText::CONTROLS, SOLMenuText::SETTINGS, SOLMenuText::ABOUT, SOLMenuText::QUIT };
    static_assert(UE_ARRAY_COUNT(entryLabels) == static_cast<int32>(EEntry::Count), "one label per entry");
    const float stackTop = inset + COLUMN_TITLE_HEIGHT + COLUMN_TAGLINE_HEIGHT + COLUMN_STACK_GAP;
    const float buttonRight = SOL::SolidMenu::COLUMN_WIDTH - inset;
    const float hullRight = SOL::SolidMenu::COLUMN_WIDTH - PANEL_INSET_PX;
    EntryButtons.Reset();
    for (int32 entry = 0; entry < static_cast<int32>(EEntry::Count); ++entry)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, entryLabels[entry], entry, BUTTON_FONT_SIZE, ESOLSolidButtonVariant::Stack);
        button->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleEntryClicked);
        column->AddChildToVerticalBox(MainMenuSized(*WidgetTree, button, 0.0f, BUTTON_HEIGHT))->SetPadding(
            FMargin(0.0f, 0.0f, 0.0f, BUTTON_GAP));
        EntryButtons.Add(button);
        const float y = stackTop + entry * (BUTTON_HEIGHT + BUTTON_GAP) + 0.5f * BUTTON_HEIGHT;
        mEntryTraces[entry] = MainMenuTrace({ FVector2f(buttonRight, y), FVector2f(hullRight, y) }, false);
        mEntryLeds[entry] = FVector2f(inset + BUTTON_LED_X_PX, y);
    }

    // The hazard stripe under Quit, then the key hint
    UImage* hazard = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HazardStripe"));
    hazard->SetBrush(MainMenuHazardBrush(solid));
    column->AddChildToVerticalBox(MainMenuSized(*WidgetTree, hazard, 0.0f, HAZARD_HEIGHT_PX));
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
    UVerticalBoxSlot* recessSlot = column->AddChildToVerticalBox(MainMenuSized(*WidgetTree, MainMenuRecess(*WidgetTree,
        VersionText, VERSION_RECESS_PADDING), recessWidth, VERSION_RECESS_HEIGHT));
    recessSlot->SetHorizontalAlignment(HAlign_Left);

    // Circuit traces along the lower edge, above the recess: two rows, lit from their ring terminals
    const float rowB = COLUMN_HEIGHT - (PANEL_INSET_PX + VERSION_RECESS_PADDING) - VERSION_RECESS_HEIGHT
        - COLUMN_TRACE_GAP;
    const float rowA = rowB - COLUMN_TRACE_GAP;
    mTraceRowA = rowA;
    const float left = inset + LED_RADIUS_PX * 2.0f;
    const float right = buttonRight - LED_RADIUS_PX * 2.0f;
    const float mid = 0.5f * (left + right);
    const float bend = COLUMN_TRACE_GAP;    // The bends are 45 degrees: one row apart over one gap
    const float dropX = mid - COLUMN_TRACE_DROP_BENDS * bend;
    const FSOLSolidTrace traces[] = {
        MainMenuTrace({ FVector2f(left, rowA), FVector2f(dropX, rowA), FVector2f(dropX + bend, rowB),
            FVector2f(right - COLUMN_TRACE_END_BENDS * bend, rowB) }, true),
        MainMenuTrace({ FVector2f(right, rowA), FVector2f(mid, rowA) }, true),
        MainMenuTrace({ FVector2f(left, rowB), FVector2f(mid - COLUMN_TRACE_SHORT_BENDS * bend, rowB) }, true),
    };
    ColumnPanel->SetTraces(traces);
}

//////////////////////////////////////////////////////////////////////////
// Builds the page shell: the hull with side notches, the content recess holding one switcher child per page (index =
// ESOLMainMenuPage) and the Back button in a bottom-right recess
void USOLMainMenuWidget::BuildShell(UHorizontalBox& row)
{
    using namespace SOL::MenuStyle;
    using namespace SOL::SolidMenu;
    ShellPanel = WidgetTree->ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass(), TEXT("ShellPanel"));
    FSOLSolidHull hull = MainMenuPanelHull();
    hull.NotchY = 0.5f * PAGE_HEIGHT;
    hull.NotchDepth = SHELL_NOTCH_DEPTH;
    hull.NotchHeight = SHELL_NOTCH_HEIGHT;
    ShellPanel->SetHull(ESOLSolidStyle::Panel, hull);
    ShellPanel->SetContentPadding(FMargin(PANEL_INSET_PX + SHELL_PADDING));
    ShellPanel->SetDecor(true, true);
    UHorizontalBoxSlot* shellSlot = row.AddChildToHorizontalBox(MainMenuSized(*WidgetTree, ShellPanel,
        SOL::SolidMenu::PAGE_WIDTH, SOL::SolidMenu::PAGE_HEIGHT));
    shellSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    shellSlot->SetVerticalAlignment(VAlign_Center);
    shellSlot->SetPadding(FMargin(PANEL_GAP, 0.0f, 0.0f, 0.0f));
    UVerticalBox* shell = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    ShellPanel->SetContent(shell);

    // The content recess with the pages
    PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    PageSwitcher->AddChild(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()));
    PageSwitcher->AddChild(BuildStartLocationPage());
    PageSwitcher->AddChild(SOLMenuContent::BuildControlsPage(*WidgetTree, ControlsKeyCells));
    SettingsPage = SOLMenuContent::BuildSettingsPage(*WidgetTree);
    PageSwitcher->AddChild(SettingsPage);
    PageSwitcher->AddChild(SOLMenuContent::BuildAboutPage(*WidgetTree));
    DemoPage = WidgetTree->ConstructWidget<USOLDemoPageWidget>(USOLDemoPageWidget::StaticClass());
    PageSwitcher->AddChild(DemoPage);
    shell->AddChildToVerticalBox(MainMenuRecess(*WidgetTree, PageSwitcher, SHELL_RECESS_PADDING))->SetSize(
        FSlateChildSize(ESlateSizeRule::Fill));

    // Back, bottom right: the same close as Esc
    BackButton = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
    BackButton->Setup(*WidgetTree, SOLMenuText::PAGE_BACK, 0, BODY_FONT_SIZE, ESOLSolidButtonVariant::Small);
    BackButton->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleBackClicked);
    UVerticalBoxSlot* backSlot = shell->AddChildToVerticalBox(MainMenuRecess(*WidgetTree, MainMenuSized(*WidgetTree,
        BackButton, BACK_BUTTON_WIDTH, SETTINGS_ROW_HEIGHT), SHELL_BACK_RECESS_PADDING));
    backSlot->SetHorizontalAlignment(HAlign_Right);
    backSlot->SetPadding(FMargin(0.0f, SHELL_BACK_GAP, 0.0f, 0.0f));
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
    page->AddChildToVerticalBox(MainMenuRecess(*WidgetTree, bodyList, SOL::SolidMenu::LIST_RECESS_PADDING))->SetSize(
        FSlateChildSize(ESlateSizeRule::Fill));
    BodyButtons.Reset();
    const int32 bodyCount = registry != nullptr ? registry->GetRegistry().Num() : 0;
    for (int32 body = 0; body < bodyCount; ++body)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, registry->GetRegistry().GetName(body).ToString(), body, BODY_FONT_SIZE,
            ESOLSolidButtonVariant::Small);
        button->OnMenuClicked.BindUObject(this, &USOLMainMenuWidget::HandleBodyClicked);
        Cast<UScrollBoxSlot>(bodyList->AddChild(MainMenuSized(*WidgetTree, button, 0.0f, LIST_BUTTON_HEIGHT)))
            ->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, ROW_GAP));
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
    const TSharedPtr<SSOLSolidTraceLayer> layer = ColumnPanel != nullptr ? ColumnPanel->GetTraceLayer() : nullptr;
    for (int32 entry = 0; entry < EntryButtons.Num(); ++entry)
    {
        EntryButtons[entry]->SetHoverTrace(layer, mEntryTraces[entry]);
    }
    if (layer.IsValid())
    {
        layer->SetLeds(mEntryLeds);
    }
    PlayShowAnimation();
}

//////////////////////////////////////////////////////////////////////////
// Starts the column's open, trace power-up and LED blink from now, and closes a shell that is not Closed at once
void USOLMainMenuWidget::PlayShowAnimation()
{
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    mColumnAnim = FSOLHoloMenuAnim();
    SOLSolidMenu::BeginPanelOpen(mColumnAnim, frame.NowS, frame.Strength);
    mColumnOpenS = frame.NowS;
    mColumnProgress = SOLSolidMenu::PanelProgress(mColumnAnim, frame.NowS, frame.Strength);
    mColumnTracePower = SOLSolidMenu::TracePower(0.0, frame.Strength);
    mIsPowerUpRunning = true;
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
    if (!layer.IsValid())
    {
        return;
    }

    // The blink: each entry's LED in stack order; one whose own LED already shows (hover, press, mark) is left alone
    if (mIsPowerUpRunning)
    {
        const double sinceOpenS = nowS - mColumnOpenS;
        uint32 mask = 0;
        for (int32 entry = 0; entry < EntryButtons.Num() && entry < SSOLSolidTraceLayer::MAX_LEDS; ++entry)
        {
            const FSOLSolidButtonLook& look = EntryButtons[entry]->GetAppliedLook();
            const bool bOwnLed = look.Led > 0.0f || look.bSelectedMark || look.bDisabled;
            if (!bOwnLed && SOLSolidMenu::PowerUpLedLit(entry, EntryButtons.Num(), sinceOpenS, s))
            {
                mask |= 1u << entry;
            }
        }
        layer->SetLitLeds(mask);
        mIsPowerUpRunning = SOLSolidMenu::IsPowerUpRunning(sinceOpenS, s);
    }
    layer->SetPulseDot(SOLSolidMenu::PulseDotFraction(nowS), SOLSolidMenu::PulseDotAlpha(s));
}

//////////////////////////////////////////////////////////////////////////
// Shows a page in the shell beside the column (None closes the shell, which animates out after the input is back)
void USOLMainMenuWidget::ShowPage(const ESOLMainMenuPage page)
{
    // Leaving Settings ends its visit (unapplied changes reverted, one write if owed); Controls and Settings re-read
    // their state when shown
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
    if (page == ESOLMainMenuPage::Controls)
    {
        SOLMenuContent::RefreshControlsPage(ControlsKeyCells, SOLMenuContent::ResolveLiveKeys(GetWorld()));
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
        SOLSolidMenu::BeginPanelOpen(mShellAnim, frame.NowS, frame.Strength);
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

    // The highlights: the column's entries always, the shell's Back and start-location rows only while it shows
    for (USOLMenuButton* button : EntryButtons)
    {
        button->RefreshHighlightAt(frame.NowS, frame.Strength);
    }
    const bool bShellShown = mShellAnim.Phase != ESOLHoloMenuPhase::Closed;
    if (bShellShown && BackButton != nullptr)
    {
        BackButton->RefreshHighlightAt(frame.NowS, frame.Strength);
    }
    if (bShellShown && GetShownPageIndex() == static_cast<int32>(ESOLMainMenuPage::StartLocation))
    {
        for (USOLMenuButton* button : BodyButtons)
        {
            button->RefreshHighlightAt(frame.NowS, frame.Strength);
        }
    }

    // The column: open look, then its traces' power-up (from the open's start) and pulse
    if (ColumnPanel != nullptr)
    {
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
