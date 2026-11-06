/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

enum class ESOLReadOnlyPage : uint8;
class USOLReadOnlyPageWidget;
class USOLSettingsPageWidget;
class UTextBlock;
class UVerticalBox;
class UWorld;
class UWidget;
class UWidgetTree;

// Builders for the menu pages the main and pause menus share (SDD 8, 8c/8d): Controls, Info, About and Settings. Each
// builds its widgets once into the calling widget's tree; nothing here runs per frame (the Controls and Info pages'
// key texts are refreshed when the page is shown).
namespace SOLMenuContent
{
    // Creates a text block with the menu font, a size and a colour (wrapping when bWrap is set)
    SOLTEST_API UTextBlock* MakeText(UWidgetTree& tree, const FString& text, int32 fontSize, const FLinearColor& color,
        bool bBold = false, bool bWrap = false);

    // Creates a vertical page with a heading, ready for rows
    SOLTEST_API UVerticalBox* MakePage(UWidgetTree& tree, const TCHAR* title);

    // Returns a key binding cell's text: each '|' alternative is either '@' + a rebindable action id (shown as that
    // action's key in liveKeys, indexed by SOLKeyBindings::EBinding) or a '+' chord of engine key names, each shown
    // through SOLBindingLabel; the alternatives are joined by " / "
    SOLTEST_API FString FormatBindings(const TCHAR* bindings, const TArray<FString>& liveKeys);

    // FormatBindings with the default keys
    SOLTEST_API FString FormatBindings(const TCHAR* bindings);

    // Returns the keys the Controls page shows: the ship pawn's live mapping when the player controls one, else the
    // applied settings (the main menu has no ship), else the defaults
    SOLTEST_API TArray<FString> ResolveLiveKeys(const UWorld* world);

    // Returns a time-warp factor as "10x", "1 h/s" or "30 d/s"
    SOLTEST_API FString FormatWarpFactor(double factor);

    // Returns a Controls row's action text with its '{DropDistance}' (COMBAT_TARGET_DROP_DISTANCE_M, e.g. "200 m") and
    // '{SpeedCapNotches}' (SOLFlight::SpeedCapStepsPerDecade, e.g. "10") filled in
    SOLTEST_API FString FormatControlAction(const TCHAR* action);

    // Builds the Controls page (a tab page, SDD 46 section 6.5: one tab per area, every binding in key / action rows);
    // its key cells (USOLReadOnlyPageWidget::GetKeyCells) are one text block per SOLMenuText::CONTROL_ROWS row, for
    // RefreshControlsPage
    SOLTEST_API USOLReadOnlyPageWidget* BuildControlsPage(UWidgetTree& tree);

    // Rewrites the Controls page's key cells from the live keys (called when the page is shown)
    SOLTEST_API void RefreshControlsPage(const TArray<TObjectPtr<UTextBlock>>& keyCells,
        const TArray<FString>& liveKeys);

    // Builds the Info page (a tab page: flight, time and combat values generated from the real constants, plus
    // prose); its key texts (USOLReadOnlyPageWidget::GetKeyTexts) are the text blocks that name a key (the manual
    // surface-lock label, then each prose paragraph), for RefreshInfoPage
    SOLTEST_API USOLReadOnlyPageWidget* BuildInfoPage(UWidgetTree& tree);

    // Rewrites the Info page's key-naming texts from the live keys (called when the page is shown)
    SOLTEST_API void RefreshInfoPage(const TArray<TObjectPtr<UTextBlock>>& keyTexts, const TArray<FString>& liveKeys);

    // Builds the About page (a tab page: the game with the engine version, and the credits)
    SOLTEST_API USOLReadOnlyPageWidget* BuildAboutPage(UWidgetTree& tree);

    // Builds a read-only tab page of a kind and fills its tabs
    SOLTEST_API USOLReadOnlyPageWidget* BuildReadOnlyPage(UWidgetTree& tree, ESOLReadOnlyPage kind);

    // Fills a read-only page's tabs (called by USOLReadOnlyPageWidget::Setup once its scroll boxes exist)
    SOLTEST_API void FillReadOnlyTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree);

    // The per-page fillers FillReadOnlyTabs calls
    SOLTEST_API void FillControlsTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree);
    SOLTEST_API void FillInfoTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree);
    SOLTEST_API void FillAboutTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree);

    // Builds the Settings page (USOLSettingsPageWidget, a child user widget in the tree)
    SOLTEST_API USOLSettingsPageWidget* BuildSettingsPage(UWidgetTree& tree);
}
