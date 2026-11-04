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

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Input/Events.h"
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

    // The "Orbit Lines" row sits above the four orbit rows, which follow one another in the table
    using SOLOverlay::EOverlay;
    static_assert(static_cast<int32>(EOverlay::AsteroidOrbits) == static_cast<int32>(EOverlay::PlanetOrbits) + 1
        && static_cast<int32>(EOverlay::MoonOrbits) == static_cast<int32>(EOverlay::PlanetOrbits) + 2
        && static_cast<int32>(EOverlay::OtherOrbits) == static_cast<int32>(EOverlay::PlanetOrbits) + 3,
        "The Overlay Menu indents the orbit rows under Orbit Lines: they must follow PlanetOrbits");

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
    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    PanelBorder->SetBrushColor(OVERLAY_MENU_BACKGROUND);
    PanelBorder->SetPadding(FMargin(OVERLAY_MENU_PADDING));
    RootBorder->SetContent(PanelBorder);
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
    RefreshHint();
    RefreshRows();
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
    RefreshHint();
    RefreshRows();
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
        TabBorders[profile]->SetBrushColor(profile == mViewedProfile ? OVERLAY_MENU_ROW_HIGHLIGHT
            : FLinearColor::Transparent);
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
        FString text = bIndented ? SOLMenuText::OVERLAY_MENU_CHILD_INDENT : TEXT("");
        text += bChecked ? SOLMenuText::OVERLAY_MENU_CHECKED : SOLMenuText::OVERLAY_MENU_UNCHECKED;
        text += label;
        RowTexts[row]->SetText(FText::FromString(text));
        const bool bHighlighted = row == mHighlightedRow && !mIsEmbedded;
        RowTexts[row]->SetColorAndOpacity(FSlateColor(bHighlighted ? SOL::HudPalette::SECONDARY
            : (bChecked && !bGated ? OVERLAY_MENU_TEXT : OVERLAY_MENU_DIM_TEXT)));
        RowBorders[row]->SetBrushColor(bHighlighted ? OVERLAY_MENU_ROW_HIGHLIGHT : FLinearColor::Transparent);
    }
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
}

//////////////////////////////////////////////////////////////////////////
// Views a profile as a tab click does: an enabled one becomes active, a disabled one shows its enable page
void USOLOverlayMenuWidget::ClickTab(const int32 profile)
{
    if (profile < 0 || profile >= SOL::Overlay::MAX_PROFILES)
    {
        return;
    }
    mViewedProfile = profile;
    if (Settings != nullptr && SOLOverlay::IsProfileEnabled(Settings->GetOverlayPrefs(), profile))
    {
        Settings->SetActiveOverlayProfile(profile);
    }
    RefreshRows();
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
    if (mIsEmbedded)
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
// The row under the mouse is highlighted
FReply USOLOverlayMenuWidget::NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    const int32 row = FindRowAt(mouseEvent.GetScreenSpacePosition());
    if (row != INDEX_NONE && row != mHighlightedRow)
    {
        mHighlightedRow = row;
        RefreshRows();
    }
    return Super::NativeOnMouseMove(geometry, mouseEvent);
}
