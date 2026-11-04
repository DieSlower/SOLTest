/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlayMenuWidget.h"

#include "HudEvents/SOLHudBus.h"
#include "HudEvents/SOLHudEventSubsystem.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "SOLConstants.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloFrameWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Fonts and sizes (Slate's default family; the tabs and rows in mono so the marks line up)
    const TCHAR* const OVERLAY_MENU_FONT_REGULAR = TEXT("Regular");
    const TCHAR* const OVERLAY_MENU_FONT_MONO = TEXT("Mono");
    constexpr int32 OVERLAY_MENU_TITLE_SIZE = 16;
    constexpr int32 OVERLAY_MENU_ROW_SIZE = 14;
    constexpr int32 OVERLAY_MENU_HINT_SIZE = 11;
    constexpr float OVERLAY_MENU_PADDING = 18.0f;
    constexpr float OVERLAY_MENU_ROW_PADDING = 3.0f;
    constexpr float OVERLAY_MENU_LEFT_INSET = 60.0f;        // Standalone panel inset from the screen's left edge

    // Hologram palette (like the F3 panel): Primary-tinted text on a dark translucent panel, the highlight in
    // Secondary, an unchecked or gated row dimmer
    const FLinearColor OVERLAY_MENU_BACKGROUND(0.01f, 0.03f, 0.06f, 0.78f);
    const FLinearColor OVERLAY_MENU_TEXT(0.75f, 0.9f, 1.0f, 1.0f);
    const FLinearColor OVERLAY_MENU_DIM_TEXT(0.35f, 0.47f, 0.58f, 1.0f);
    const FLinearColor OVERLAY_MENU_ROW_HIGHLIGHT(0.2f, 0.7f, 1.0f, 0.12f);

    // Frames the hologram frame's layout is re-read after a content change: the change is arranged in the paint of
    // the frame it happened in, so the next tick reads it (the second frame is a margin)
    constexpr int32 OVERLAY_MENU_LAYOUT_SETTLE_FRAMES = 2;

    // Room the hologram frame needs inside the standalone panel (local units; the frame's pixel sizes are about 1.5
    // local units at 720-1440 px high views, where the UMG scale is 2/3 of the frame's UI scale): below the title, so
    // the tabs start under the frame's title underline (34 px), and on the right, clear of the bottom-right step-in
    constexpr float OVERLAY_MENU_FRAMED_TITLE_GAP = 28.0f;
    constexpr float OVERLAY_MENU_FRAMED_RIGHT_ROOM = 16.0f;

    // The "Orbit Lines" row sits above the four orbit rows, which follow one another in the table
    using SOLOverlay::EOverlay;
    static_assert(static_cast<int32>(EOverlay::AsteroidOrbits) == static_cast<int32>(EOverlay::PlanetOrbits) + 1
        && static_cast<int32>(EOverlay::MoonOrbits) == static_cast<int32>(EOverlay::PlanetOrbits) + 2
        && static_cast<int32>(EOverlay::OtherOrbits) == static_cast<int32>(EOverlay::PlanetOrbits) + 3,
        "The Overlay Menu indents the orbit rows under Orbit Lines: they must follow PlanetOrbits");
    static_assert(USOLOverlayMenuWidget::ROW_WIDGET_COUNT <= 32, "mIndentedRows holds one bit per display row");

    //////////////////////////////////////////////////////////////////////////
    // Creates a text block in a font face, size and colour
    UTextBlock* OverlayMenuMakeText(UWidgetTree& tree, const FString& text, const TCHAR* face, const int32 size,
        const FLinearColor& color)
    {
        UTextBlock* block = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        block->SetFont(FCoreStyle::GetDefaultFontStyle(face, size));
        block->SetColorAndOpacity(FSlateColor(color));
        block->SetText(FText::FromString(text));
        return block;
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a transparent padded border holding a mono text block (a tab or a row)
    UBorder* OverlayMenuMakeEntry(UWidgetTree& tree, UTextBlock*& outText)
    {
        UBorder* border = tree.ConstructWidget<UBorder>(UBorder::StaticClass());
        border->SetBrushColor(FLinearColor::Transparent);
        border->SetPadding(FMargin(OVERLAY_MENU_ROW_PADDING * 2.0f, OVERLAY_MENU_ROW_PADDING));
        outText = OverlayMenuMakeText(tree, FString(), OVERLAY_MENU_FONT_MONO, OVERLAY_MENU_ROW_SIZE,
            OVERLAY_MENU_TEXT);
        border->SetContent(outText);
        return border;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a format with the profile number token replaced by a 0-based profile's number
    FString OverlayMenuProfileText(const TCHAR* format, const int32 profile)
    {
        return FString(format).Replace(SOLMenuText::OVERLAY_PROFILE_NUMBER_TOKEN, *FString::FromInt(profile + 1),
            ESearchCase::CaseSensitive);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the prefs to show: the settings' applied prefs, or the defaults without settings
    const SOLOverlay::FOverlayPrefs& OverlayMenuPrefs(const USOLSettingsSubsystem* settings)
    {
        static const SOLOverlay::FOverlayPrefs defaults = SOLOverlay::MakeDefaultPrefs();
        return settings != nullptr ? settings->GetOverlayPrefs() : defaults;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree: a panel with the title, the tab column beside the row list, and the key hint
void USOLOverlayMenuWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    Settings = USOLSettingsSubsystem::Get(this);

    // A transparent full-screen root holding the panel at the left, so the world stays visible and the toggles
    // preview live (SetEmbedded strips the root's inset and the panel's backing for the Settings page)
    RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    RootBorder->SetBrushColor(FLinearColor::Transparent);
    RootBorder->SetHorizontalAlignment(HAlign_Left);
    RootBorder->SetVerticalAlignment(VAlign_Center);
    RootBorder->SetPadding(FMargin(OVERLAY_MENU_LEFT_INSET, 0.0f, 0.0f, 0.0f));
    WidgetTree->RootWidget = RootBorder;

    // The panel sits in an overlay so the standalone menu's hologram frame can go behind it (EnsureHoloFrame)
    PanelStack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PanelStack"));
    RootBorder->SetContent(PanelStack);
    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    PanelBorder->SetBrushColor(OVERLAY_MENU_BACKGROUND);
    PanelBorder->SetPadding(FMargin(OVERLAY_MENU_PADDING));
    UOverlaySlot* panelSlot = PanelStack->AddChildToOverlay(PanelBorder);
    panelSlot->SetHorizontalAlignment(HAlign_Fill);
    panelSlot->SetVerticalAlignment(VAlign_Fill);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    PanelBorder->SetContent(column);
    TitleText = OverlayMenuMakeText(*WidgetTree, FString(), OVERLAY_MENU_FONT_REGULAR, OVERLAY_MENU_TITLE_SIZE,
        OVERLAY_MENU_TEXT);
    column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, OVERLAY_MENU_PADDING * 0.5f));

    // The tab column on the left of the row list
    UHorizontalBox* body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Body"));
    column->AddChildToVerticalBox(body);
    UVerticalBox* tabColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Tabs"));
    body->AddChildToHorizontalBox(tabColumn)->SetPadding(FMargin(0.0f, 0.0f, OVERLAY_MENU_PADDING, 0.0f));
    UVerticalBox* rowColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Rows"));
    body->AddChildToHorizontalBox(rowColumn);
    TabColumn = tabColumn;
    RowColumn = rowColumn;

    // One tab per profile
    TabBorders.Reset(SOL::Overlay::MAX_PROFILES);
    TabTexts.Reset(SOL::Overlay::MAX_PROFILES);
    for (int32 profile = 0; profile < SOL::Overlay::MAX_PROFILES; ++profile)
    {
        UTextBlock* text = nullptr;
        UBorder* border = OverlayMenuMakeEntry(*WidgetTree, text);
        tabColumn->AddChildToVerticalBox(border);
        TabBorders.Add(border);
        TabTexts.Add(text);
    }

    // Every display row a page can show (each page collapses the ones it does not use)
    RowBorders.Reset(ROW_WIDGET_COUNT);
    RowTexts.Reset(ROW_WIDGET_COUNT);
    for (int32 row = 0; row < ROW_WIDGET_COUNT; ++row)
    {
        UTextBlock* text = nullptr;
        UBorder* border = OverlayMenuMakeEntry(*WidgetTree, text);
        rowColumn->AddChildToVerticalBox(border);
        RowBorders.Add(border);
        RowTexts.Add(text);
    }
    HintText = OverlayMenuMakeText(*WidgetTree, FString(), OVERLAY_MENU_FONT_REGULAR,
        OVERLAY_MENU_HINT_SIZE, OVERLAY_MENU_DIM_TEXT);
    column->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.0f, OVERLAY_MENU_PADDING * 0.5f, 0.0f, 0.0f));
    mViewedProfile = OverlayMenuPrefs(Settings).ActiveProfile;
    mOpenVisibility = GetVisibility();
    RefreshHint();
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Creates the hologram frame behind the panel once (standalone only, D17); the frame draws the tint and the marks, so
// the panel, the viewed tab and the highlighted row lose their own fills
void USOLOverlayMenuWidget::EnsureHoloFrame()
{
    if (HoloFrame != nullptr || mIsEmbedded || PanelStack == nullptr)
    {
        return;
    }
    HoloFrame = WidgetTree->ConstructWidget<USOLHoloFrameWidget>(USOLHoloFrameWidget::StaticClass(),
        TEXT("HoloFrame"));
    UOverlaySlot* frameSlot = Cast<UOverlaySlot>(PanelStack->InsertChildAt(0, HoloFrame));
    if (frameSlot != nullptr)
    {
        frameSlot->SetHorizontalAlignment(HAlign_Fill);
        frameSlot->SetVerticalAlignment(VAlign_Fill);
    }
    PanelBorder->SetBrushColor(FLinearColor::Transparent);
    PanelBorder->SetPadding(FMargin(OVERLAY_MENU_PADDING, OVERLAY_MENU_PADDING,
        OVERLAY_MENU_PADDING + OVERLAY_MENU_FRAMED_RIGHT_ROOM, OVERLAY_MENU_PADDING));
    if (UVerticalBoxSlot* titleSlot = Cast<UVerticalBoxSlot>(TitleText->Slot))
    {
        titleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, OVERLAY_MENU_FRAMED_TITLE_GAP));
    }
    mTabsViewedProfile = INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the panel's backing colour as set
FLinearColor USOLOverlayMenuWidget::GetPanelColor() const
{
    return PanelBorder != nullptr ? PanelBorder->GetBrushColor() : FLinearColor::Transparent;
}

//////////////////////////////////////////////////////////////////////////
// Makes the widget the Settings page's embedded list (no inset, backing, title or keys, not focusable)
void USOLOverlayMenuWidget::SetEmbedded(const bool bEmbedded)
{
    mIsEmbedded = bEmbedded;
    SetIsFocusable(!bEmbedded);
    if (RootBorder != nullptr)
    {
        RootBorder->SetPadding(FMargin(bEmbedded ? 0.0f : OVERLAY_MENU_LEFT_INSET, 0.0f, 0.0f, 0.0f));
        PanelBorder->SetBrushColor(bEmbedded ? FLinearColor::Transparent : OVERLAY_MENU_BACKGROUND);
        PanelBorder->SetPadding(FMargin(bEmbedded ? 0.0f : OVERLAY_MENU_PADDING));
        TitleText->SetVisibility(bEmbedded ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
    RefreshHint();
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Writes the key hint from the current bindings (click only in the embedded list, which takes no keys)
void USOLOverlayMenuWidget::RefreshHint()
{
    if (HintText != nullptr)
    {
        HintText->SetText(FText::FromString(SOLKeyBindings::FormatKeyText(mIsEmbedded
            ? SOLMenuText::OVERLAY_MENU_HINT_EMBEDDED : SOLMenuText::OVERLAY_MENU_HINT,
            Settings != nullptr ? Settings->GetAppliedKeys() : SOLKeyBindings::DefaultKeys())));
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes the title with the viewed profile's number (standalone; only when that number changed)
void USOLOverlayMenuWidget::RefreshTitle()
{
    if (mIsEmbedded || TitleText == nullptr || mTitleFormat.IsEmpty() || mTitleProfile == mViewedProfile)
    {
        return;
    }
    mTitleProfile = mViewedProfile;
    TitleText->SetText(FText::FromString(OverlayMenuProfileText(*mTitleFormat, mViewedProfile)));
}

//////////////////////////////////////////////////////////////////////////
// Shows the standalone menu on the active profile and remembers the owner's close delegate, toggle key and title
void USOLOverlayMenuWidget::Open(const FSimpleDelegate& close, const FKey& toggleKey, const FString& titleFormat)
{
    mClose = close;
    mToggleKey = toggleKey;
    mTitleFormat = titleFormat;
    mTitleProfile = INDEX_NONE;
    mViewedProfile = OverlayMenuPrefs(Settings).ActiveProfile;
    mHoveredTab = INDEX_NONE;
    mLastRowHoverItem = INDEX_NONE;
    EnsureHoloFrame();
    RefreshHint();
    RefreshRows();
    if (HoloFrame == nullptr)
    {
        return;
    }

    // Takes input again (a reopen during the close) and opens the frame (or reverses its close from where it is); a
    // frame not built yet (the first open: its Slate widget is made when the menu is first arranged) opens from the
    // first tick, which runs before the content's first paint, so the content waits hidden until then
    mIsClosing = false;
    SetVisibility(mOpenVisibility);
    SetIsFocusable(true);
    mIsOpenPending = true;
    if (HoloFrame->GetFrame().IsValid())
    {
        StartFrameOpen();
    }
    else
    {
        ApplyContentOpacity(0.0f);
    }
    MarkLayoutDirty();
}

//////////////////////////////////////////////////////////////////////////
// Opens the built frame (or reverses its close) with the open cue; the content follows the frame's fill
void USOLOverlayMenuWidget::StartFrameOpen()
{
    mIsOpenPending = false;
    HoloFrame->BeginOpen();

    // The initial highlight goes to the frame while it is Opening, so the open plays no hover cue (at strength 0 too,
    // where the next advance already reads Open)
    UpdateFrameHover();
    mIsAnimating = true;
    ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Starts the non-blocking close: the owner gives input back at once, the frame's close plays, then the widget removes
// itself
void USOLOverlayMenuWidget::BeginClose()
{
    if (mIsEmbedded)
    {
        return;
    }
    mIsClosing = true;
    mHoveredTab = INDEX_NONE;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetIsFocusable(false);

    // The keyboard goes back to the game viewport now (the owner's input mode change lands later), so no key reaches
    // the menu while it closes
    if (HasAnyUserFocus() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
    if (HoloFrame == nullptr || !IsInViewport() || mIsOpenPending)
    {
        FinishClose();
        return;
    }
    HoloFrame->BeginClose();
    if (HoloFrame->AdvanceAnim() == ESOLHoloMenuPhase::Closed)
    {
        FinishClose();
        return;
    }
    mIsAnimating = true;
    ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Ends the close: removes the widget (the next Open adds it again)
void USOLOverlayMenuWidget::FinishClose()
{
    mIsAnimating = false;
    mIsOpenPending = false;
    ApplyContentOpacity(0.0f);
    RemoveFromParent();
}

//////////////////////////////////////////////////////////////////////////
// Removes the menu at once, without the close (teardown); the frame drops to Closed, so the next Open draws in again
// with its open cue
void USOLOverlayMenuWidget::Dismiss()
{
    mIsClosing = true;
    mHoveredTab = INDEX_NONE;
    if (HoloFrame != nullptr)
    {
        HoloFrame->ResetClosed();
    }
    FinishClose();
}

//////////////////////////////////////////////////////////////////////////
// While the frame animates: advances it and follows its fill, removing the widget once closed; after a change: lays
// the frame out again. Returns at once while the menu is open and idle (and always when embedded)
void USOLOverlayMenuWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    if (HoloFrame == nullptr)
    {
        return;
    }
    if (mIsOpenPending && HoloFrame->GetFrame().IsValid())
    {
        StartFrameOpen();
    }
    if (mIsAnimating)
    {
        const ESOLHoloMenuPhase phase = HoloFrame->AdvanceAnim();
        if (phase == ESOLHoloMenuPhase::Closed)
        {
            FinishClose();
            return;
        }
        ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
        mIsAnimating = phase != ESOLHoloMenuPhase::Open;
    }
    if (mLayoutDirtyFrames > 0 && !mIsClosing)
    {
        UpdateFrameLayout();
    }
}

//////////////////////////////////////////////////////////////////////////
// Asks the frame layout to be rebuilt over the next frames (standalone only)
void USOLOverlayMenuWidget::MarkLayoutDirty()
{
    if (HoloFrame != nullptr)
    {
        mLayoutDirtyFrames = OVERLAY_MENU_LAYOUT_SETTLE_FRAMES;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the frame's layout and hover from the content as last arranged, in the frame's local units (the frame fills
// the panel, so the panel's top-left is the origin); waits while the frame has not been arranged yet
void USOLOverlayMenuWidget::UpdateFrameLayout()
{
    const FVector2D frameSize = HoloFrame->GetCachedGeometry().GetLocalSize();
    if (frameSize.X <= 0.0 || frameSize.Y <= 0.0)
    {
        return;
    }
    FSOLHoloOverlayLayoutInput input;
    input.TitleWidth = static_cast<float>(TitleText->GetDesiredSize().X);
    input.TabColumn = HoloFrame->ToFrameLocal(TabColumn->GetCachedGeometry());
    input.RowColumn = HoloFrame->ToFrameLocal(RowColumn->GetCachedGeometry());
    input.TabCount = FMath::Min(TabBorders.Num(), SOL::HoloMenu::FRAME_MAX_TABS);
    for (int32 tab = 0; tab < input.TabCount; ++tab)
    {
        input.TabRects[tab] = HoloFrame->ToFrameLocal(TabBorders[tab]->GetCachedGeometry());
    }
    input.SelectedTab = mViewedProfile;
    input.HintTopY = HoloFrame->ToFrameLocal(HintText->GetCachedGeometry()).Min.Y;
    FSOLHoloFrameSpec layout;
    SOLHoloFrameBuild::MakeOverlayLayout(input, layout);
    HoloFrame->SetLayout(layout);
    UpdateFrameHover();
    --mLayoutDirtyFrames;
}

//////////////////////////////////////////////////////////////////////////
// Sets the frame's hovered item: the tab under the mouse (brackets only), else the highlighted row (brackets and a
// pointer line), each around its text; the frame asks for the hover cue when the item changed while open
void USOLOverlayMenuWidget::UpdateFrameHover()
{
    // The child indent in the row font, measured once (the hover mark goes around the indented text, not the indent)
    if (mIndentWidth < 0.0f && FSlateApplication::IsInitialized())
    {
        const TSharedRef<FSlateFontMeasure> measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        mIndentWidth = static_cast<float>(measure->Measure(FString(SOLMenuText::OVERLAY_MENU_CHILD_INDENT),
            FCoreStyle::GetDefaultFontStyle(OVERLAY_MENU_FONT_MONO, OVERLAY_MENU_ROW_SIZE)).X);
    }
    int32 item = INDEX_NONE;
    FBox2f rect(ForceInit);
    ESOLHoloHoverMark mark = ESOLHoloHoverMark::RowPointer;
    if (TabTexts.IsValidIndex(mHoveredTab))
    {
        item = ROW_WIDGET_COUNT + mHoveredTab;
        rect = SOLHoloFrameBuild::TextItemRect(HoloFrame->ToFrameLocal(TabTexts[mHoveredTab]->GetCachedGeometry()),
            static_cast<float>(TabTexts[mHoveredTab]->GetDesiredSize().X), 0.0f);
        mark = ESOLHoloHoverMark::BracketsOnly;
    }
    else if (RowTexts.IsValidIndex(mHighlightedRow) && GetRowKind(mHighlightedRow) != ESOLOverlayRowKind::Hidden)
    {
        item = mHighlightedRow;
        const bool bIsIndented = (mIndentedRows & (1u << mHighlightedRow)) != 0;
        rect = SOLHoloFrameBuild::TextItemRect(HoloFrame->ToFrameLocal(RowTexts[mHighlightedRow]->GetCachedGeometry()),
            static_cast<float>(RowTexts[mHighlightedRow]->GetDesiredSize().X),
            bIsIndented ? FMath::Max(mIndentWidth, 0.0f) : 0.0f);
    }

    // The mark going back from a tab to the row that was highlighted before is not a highlight change: no hover cue
    const bool bIsTabItem = item >= ROW_WIDGET_COUNT;
    const bool bAllowCue = bIsTabItem || item != mLastRowHoverItem;
    if (!bIsTabItem)
    {
        mLastRowHoverItem = item;
    }
    HoloFrame->SetHoveredItem(item, rect, mark, bAllowCue);
}

//////////////////////////////////////////////////////////////////////////
// Sets the content's render opacity when it changed (the panel holds the title, the tabs, the rows and the hint)
void USOLOverlayMenuWidget::ApplyContentOpacity(const float opacity)
{
    if (opacity != mContentOpacity && PanelBorder != nullptr)
    {
        mContentOpacity = opacity;
        PanelBorder->SetRenderOpacity(opacity);
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests the select cue (standalone, not while closing; at any strength, D21)
void USOLOverlayMenuWidget::RequestSelectCue()
{
    if (HoloFrame != nullptr && !mIsClosing)
    {
        HoloFrame->PlaySelectCue();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the table row of a display row on an enabled page, or INDEX_NONE
int32 USOLOverlayMenuWidget::GetRowOverlay(const int32 row)
{
    if (row < 0 || row == ORBIT_LINES_ROW || row > SOLOverlay::OVERLAY_COUNT)
    {
        return INDEX_NONE;
    }
    return row < ORBIT_LINES_ROW ? row : row - 1;
}

//////////////////////////////////////////////////////////////////////////
// Returns the display row of a table row on an enabled profile's page
int32 USOLOverlayMenuWidget::GetOverlayRow(const SOLOverlay::EOverlay overlay)
{
    const int32 index = static_cast<int32>(overlay);
    return index < ORBIT_LINES_ROW ? index : index + 1;
}

//////////////////////////////////////////////////////////////////////////
// Returns what a display row is on the viewed page
USOLOverlayMenuWidget::ESOLOverlayRowKind USOLOverlayMenuWidget::GetRowKind(const int32 row) const
{
    if (row < 0 || row >= ROW_WIDGET_COUNT)
    {
        return ESOLOverlayRowKind::Hidden;
    }
    if (!SOLOverlay::IsProfileEnabled(OverlayMenuPrefs(Settings), mViewedProfile))
    {
        return row == 0 ? ESOLOverlayRowKind::EnableProfile : ESOLOverlayRowKind::Hidden;
    }
    if (row == ORBIT_LINES_ROW)
    {
        return ESOLOverlayRowKind::OrbitLines;
    }
    if (row == PROFILE_ENABLED_ROW)
    {
        return mViewedProfile != 0 ? ESOLOverlayRowKind::ProfileEnabled : ESOLOverlayRowKind::Hidden;
    }
    return ESOLOverlayRowKind::Overlay;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of rows on the viewed page
int32 USOLOverlayMenuWidget::GetRowCount() const
{
    if (!SOLOverlay::IsProfileEnabled(OverlayMenuPrefs(Settings), mViewedProfile))
    {
        return 1;
    }
    return mViewedProfile == 0 ? PROFILE_ENABLED_ROW : ROW_WIDGET_COUNT;
}

//////////////////////////////////////////////////////////////////////////
// Re-reads the five tabs (the active mark and colour, the off suffix, the viewed tab's backing) when they changed
void USOLOverlayMenuWidget::RefreshTabs(const SOLOverlay::FOverlayPrefs& prefs)
{
    // A row toggle or a hover change leaves the tabs as they are: their texts are rebuilt only when what they show
    // (the enabled profiles, the active one, the viewed one) changed
    const int32 enabledProfiles = static_cast<int32>(prefs.EnabledProfiles);
    if (enabledProfiles == mTabsEnabledProfiles && prefs.ActiveProfile == mTabsActiveProfile
        && mViewedProfile == mTabsViewedProfile)
    {
        return;
    }
    mTabsEnabledProfiles = enabledProfiles;
    mTabsActiveProfile = prefs.ActiveProfile;
    mTabsViewedProfile = mViewedProfile;
    for (int32 profile = 0; profile < TabTexts.Num(); ++profile)
    {
        const bool bEnabled = SOLOverlay::IsProfileEnabled(prefs, profile);
        const bool bActive = profile == prefs.ActiveProfile;
        FString text = bActive ? SOLMenuText::OVERLAY_MENU_TAB_ACTIVE_MARK
            : SOLMenuText::OVERLAY_MENU_TAB_INACTIVE_MARK;
        text += OverlayMenuProfileText(SOLMenuText::OVERLAY_MENU_TAB_FORMAT, profile);
        if (!bEnabled)
        {
            text += SOLMenuText::OVERLAY_MENU_TAB_OFF_SUFFIX;
        }
        TabTexts[profile]->SetText(FText::FromString(text));
        TabTexts[profile]->SetColorAndOpacity(FSlateColor(bActive ? SOL::HudPalette::SECONDARY
            : (bEnabled ? OVERLAY_MENU_TEXT : OVERLAY_MENU_DIM_TEXT)));
        TabBorders[profile]->SetBrushColor(profile == mViewedProfile && HoloFrame == nullptr
            ? OVERLAY_MENU_ROW_HIGHLIGHT : FLinearColor::Transparent);
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-reads the tabs, the title and every row: its check mark, its indent and its colour (highlight, unchecked, gated)
void USOLOverlayMenuWidget::RefreshRows()
{
    const SOLOverlay::FOverlayPrefs& prefs = OverlayMenuPrefs(Settings);

    // The view follows the active profile while it shows an enabled one (viewing an enabled tab activates it, D11);
    // a disabled profile's enable page stays in view
    mViewedProfile = FMath::Clamp(mViewedProfile, 0, SOL::Overlay::MAX_PROFILES - 1);
    if (SOLOverlay::IsProfileEnabled(prefs, mViewedProfile) && mViewedProfile != prefs.ActiveProfile)
    {
        mViewedProfile = prefs.ActiveProfile;
    }
    mHighlightedRow = FMath::Clamp(mHighlightedRow, 0, GetRowCount() - 1);
    RefreshTabs(prefs);
    RefreshTitle();

    // On an enabled page the viewed profile is the active one, so the active-profile reads describe it
    const bool bOrbitLinesOn = SOLOverlay::AreOrbitLinesOn(prefs);
    mIndentedRows = 0;
    for (int32 row = 0; row < RowTexts.Num(); ++row)
    {
        const ESOLOverlayRowKind kind = GetRowKind(row);
        if (kind == ESOLOverlayRowKind::Hidden)
        {
            RowBorders[row]->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }
        RowBorders[row]->SetVisibility(ESlateVisibility::Visible);

        // Each kind's check mark, label, indent and gate
        bool bChecked = false;
        bool bGated = false;
        bool bIndented = false;
        const TCHAR* label = TEXT("");
        switch (kind)
        {
        case ESOLOverlayRowKind::Overlay:
        {
            const EOverlay overlay = static_cast<EOverlay>(GetRowOverlay(row));
            const SOLOverlay::FOverlayDef& def = SOLOverlay::GetDef(static_cast<int32>(overlay));
            bChecked = SOLOverlay::IsEnabled(prefs, overlay);
            if (def.Parent != INDEX_NONE)
            {
                bIndented = true;
                bGated = !SOLOverlay::IsEnabled(prefs, static_cast<EOverlay>(def.Parent));
            }
            else if (SOLOverlay::IsOrbitOverlay(overlay))
            {
                // Shown under "Orbit Lines" like the icon types under "Icons" (display only, D19)
                bIndented = true;
                bGated = !bOrbitLinesOn;
            }
            label = def.Label;
            break;
        }
        case ESOLOverlayRowKind::OrbitLines:
            bChecked = bOrbitLinesOn;
            label = SOLMenuText::OVERLAY_MENU_ORBIT_LINES_LABEL;
            break;
        case ESOLOverlayRowKind::ProfileEnabled:
            bChecked = true;
            label = SOLMenuText::OVERLAY_MENU_PROFILE_ENABLED_LABEL;
            break;
        default:
            // The enable row tells a first enable (a copy of Profile 1) from a re-enable (its own last values, D12)
            label = SOLOverlay::IsProfileUsed(prefs, mViewedProfile) ? SOLMenuText::OVERLAY_MENU_ENABLE_RESTORE_LABEL
                : SOLMenuText::OVERLAY_MENU_ENABLE_NEW_LABEL;
            break;
        }
        mIndentedRows |= bIndented ? 1u << row : 0u;
        FString text = bIndented ? SOLMenuText::OVERLAY_MENU_CHILD_INDENT : TEXT("");
        text += bChecked ? SOLMenuText::OVERLAY_MENU_CHECKED : SOLMenuText::OVERLAY_MENU_UNCHECKED;
        text += label;
        RowTexts[row]->SetText(FText::FromString(text));
        const bool bHighlighted = row == mHighlightedRow && !mIsEmbedded;
        RowTexts[row]->SetColorAndOpacity(FSlateColor(bHighlighted ? SOL::HudPalette::SECONDARY
            : (bChecked && !bGated ? OVERLAY_MENU_TEXT : OVERLAY_MENU_DIM_TEXT)));
        RowBorders[row]->SetBrushColor(bHighlighted && HoloFrame == nullptr ? OVERLAY_MENU_ROW_HIGHLIGHT
            : FLinearColor::Transparent);
    }
    MarkLayoutDirty();
}

//////////////////////////////////////////////////////////////////////////
// Acts on one display row of the viewed page through the settings (applied live) and refreshes
void USOLOverlayMenuWidget::ToggleRow(const int32 row)
{
    if (Settings == nullptr)
    {
        return;
    }
    switch (GetRowKind(row))
    {
    case ESOLOverlayRowKind::Overlay:
        Settings->ToggleOverlay(static_cast<EOverlay>(GetRowOverlay(row)));
        break;
    case ESOLOverlayRowKind::OrbitLines:
        // The same switch as Alt+O (the orbit-line visuals follow the settings)
        Settings->SetOrbitLinesShown(!Settings->AreOrbitLinesShown());
        break;
    case ESOLOverlayRowKind::ProfileEnabled:
        // Disabling keeps the profile's content for a re-enable; the view and the active profile go back to Profile 1
        Settings->SetOverlayProfileEnabled(mViewedProfile, false);
        Settings->SetActiveOverlayProfile(0);
        mViewedProfile = 0;
        break;
    case ESOLOverlayRowKind::EnableProfile:
        Settings->SetOverlayProfileEnabled(mViewedProfile, true);
        Settings->SetActiveOverlayProfile(mViewedProfile);
        break;
    default:
        return;
    }
    mHighlightedRow = row;
    RefreshRows();
    RequestSelectCue();
}

//////////////////////////////////////////////////////////////////////////
// Views a profile as a tab click does: an enabled one becomes active, a disabled one shows its enable page
void USOLOverlayMenuWidget::ClickTab(const int32 profile)
{
    if (profile < 0 || profile >= SOL::Overlay::MAX_PROFILES)
    {
        return;
    }
    const int32 viewedBefore = mViewedProfile;
    mViewedProfile = profile;
    if (Settings != nullptr && SOLOverlay::IsProfileEnabled(Settings->GetOverlayPrefs(), profile))
    {
        Settings->SetActiveOverlayProfile(profile);
    }
    RefreshRows();
    if (mViewedProfile != viewedBefore)
    {
        RequestSelectCue();
    }
}

//////////////////////////////////////////////////////////////////////////
// Views the next (step 1) or previous (step -1) tab, wrapping like the row highlight
void USOLOverlayMenuWidget::StepTab(const int32 step)
{
    ClickTab((mViewedProfile + step + SOL::Overlay::MAX_PROFILES) % SOL::Overlay::MAX_PROFILES);
}

//////////////////////////////////////////////////////////////////////////
// Cycles the active profile (the cycle key), views it and shows the HUD tag (every press, D13)
void USOLOverlayMenuWidget::CycleProfile()
{
    if (Settings != nullptr)
    {
        mViewedProfile = Settings->CycleOverlayProfile();
    }
    RefreshRows();
    RequestSelectCue();
    if (USOLHudEventSubsystem* hudEvents = USOLHudEventSubsystem::Get(this))
    {
        hudEvents->GetBus().SendCommand(ESOLHudCommand::ShowOverlayProfileTag);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true if a key event is the profile cycle key: Right Shift held and the binding's key
bool USOLOverlayMenuWidget::IsCycleKey(const FKeyEvent& keyEvent) const
{
    if (!keyEvent.IsRightShiftDown() || Settings == nullptr)
    {
        return false;
    }
    const int32 binding = static_cast<int32>(SOLKeyBindings::EBinding::CycleOverlayProfile);
    const TArray<FString>& keys = Settings->GetAppliedKeys();
    return keys.IsValidIndex(binding) && keyEvent.GetKey() == FKey(*keys[binding]);
}

//////////////////////////////////////////////////////////////////////////
// Moves the highlight (wrapping over the viewed page's rows) and refreshes
void USOLOverlayMenuWidget::MoveHighlight(const int32 step)
{
    const int32 count = FMath::Max(GetRowCount(), 1);
    mHighlightedRow = (mHighlightedRow + step + count) % count;
    mHoveredTab = INDEX_NONE;
    RefreshRows();
}

//////////////////////////////////////////////////////////////////////////
// Returns a display row's text as shown
FString USOLOverlayMenuWidget::GetRowText(const int32 row) const
{
    return RowTexts.IsValidIndex(row) && GetRowKind(row) != ESOLOverlayRowKind::Hidden
        ? RowTexts[row]->GetText().ToString() : FString();
}

//////////////////////////////////////////////////////////////////////////
// Returns a display row's centre in absolute (screen) Slate space as last laid out, or zero
FVector2D USOLOverlayMenuWidget::GetRowScreenCenter(const int32 row) const
{
    if (!RowBorders.IsValidIndex(row))
    {
        return FVector2D::ZeroVector;
    }
    const FGeometry& geometry = RowBorders[row]->GetCachedGeometry();
    return FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * 0.5f));
}

//////////////////////////////////////////////////////////////////////////
// Returns a profile tab's centre in absolute (screen) Slate space as last laid out, or zero
FVector2D USOLOverlayMenuWidget::GetTabScreenCenter(const int32 profile) const
{
    if (!TabBorders.IsValidIndex(profile))
    {
        return FVector2D::ZeroVector;
    }
    const FGeometry& geometry = TabBorders[profile]->GetCachedGeometry();
    return FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * 0.5f));
}

//////////////////////////////////////////////////////////////////////////
// Returns the row under a screen-space point, or INDEX_NONE (a collapsed row keeps a stale geometry, so it is skipped)
int32 USOLOverlayMenuWidget::FindRowAt(const FVector2D& screenPosition) const
{
    for (int32 row = 0; row < RowBorders.Num(); ++row)
    {
        if (GetRowKind(row) != ESOLOverlayRowKind::Hidden
            && RowBorders[row]->GetCachedGeometry().IsUnderLocation(screenPosition))
        {
            return row;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the tab under a screen-space point, or INDEX_NONE
int32 USOLOverlayMenuWidget::FindTabAt(const FVector2D& screenPosition) const
{
    for (int32 profile = 0; profile < TabBorders.Num(); ++profile)
    {
        if (TabBorders[profile]->GetCachedGeometry().IsUnderLocation(screenPosition))
        {
            return profile;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Up / Down move the highlight, Space / Enter toggle, PageUp / PageDown switch tabs, the cycle key cycles, Esc and
// Ctrl+<toggle key> close (the embedded list takes no keys, D17)
FReply USOLOverlayMenuWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    if (mIsEmbedded || mIsClosing)
    {
        return Super::NativeOnKeyDown(geometry, keyEvent);
    }
    const FKey key = keyEvent.GetKey();
    if (IsCycleKey(keyEvent))
    {
        CycleProfile();
        return FReply::Handled();
    }
    if (key == EKeys::Up || key == EKeys::Down)
    {
        MoveHighlight(key == EKeys::Up ? -1 : 1);
        return FReply::Handled();
    }
    if (key == EKeys::PageUp || key == EKeys::PageDown)
    {
        StepTab(key == EKeys::PageUp ? -1 : 1);
        return FReply::Handled();
    }
    if (key == EKeys::SpaceBar || key == EKeys::Enter)
    {
        ToggleRow(mHighlightedRow);
        return FReply::Handled();
    }
    if (key == EKeys::Escape || (key == mToggleKey && keyEvent.IsControlDown()))
    {
        mClose.ExecuteIfBound();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(geometry, keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// A left click on a tab views it, on a row toggles it
FReply USOLOverlayMenuWidget::NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    if (mouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        const FVector2D at = mouseEvent.GetScreenSpacePosition();
        const int32 tab = FindTabAt(at);
        if (tab != INDEX_NONE)
        {
            ClickTab(tab);
            return FReply::Handled();
        }
        const int32 row = FindRowAt(at);
        if (row != INDEX_NONE)
        {
            ToggleRow(row);
            return FReply::Handled();
        }
    }
    return Super::NativeOnMouseButtonDown(geometry, mouseEvent);
}

//////////////////////////////////////////////////////////////////////////
// The tab under the mouse gets the hover mark (standalone), on every move, so it follows the cursor when the panel
// re-centres; the row under the mouse is highlighted only on a real move: a move that did not move the cursor
// (Slate's synthetic move after a key or a layout change) leaves the row highlight, so Up / Down keep it while the
// mouse rests on another row
FReply USOLOverlayMenuWidget::NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    const FVector2D at = mouseEvent.GetScreenSpacePosition();
    const int32 tab = HoloFrame != nullptr ? FindTabAt(at) : INDEX_NONE;
    if (tab != mHoveredTab)
    {
        mHoveredTab = tab;
        MarkLayoutDirty();
    }
    if (FVector2D(mouseEvent.GetCursorDelta()).IsZero())
    {
        return Super::NativeOnMouseMove(geometry, mouseEvent);
    }
    const int32 row = FindRowAt(at);
    if (row != INDEX_NONE && row != mHighlightedRow)
    {
        mHighlightedRow = row;
        RefreshRows();
    }
    return Super::NativeOnMouseMove(geometry, mouseEvent);
}

//////////////////////////////////////////////////////////////////////////
// The mouse left the menu: the hover mark goes back to the highlighted row
void USOLOverlayMenuWidget::NativeOnMouseLeave(const FPointerEvent& mouseEvent)
{
    Super::NativeOnMouseLeave(mouseEvent);
    if (mHoveredTab != INDEX_NONE)
    {
        mHoveredTab = INDEX_NONE;
        MarkLayoutDirty();
    }
}
