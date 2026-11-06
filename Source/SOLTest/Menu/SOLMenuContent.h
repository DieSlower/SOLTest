/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSolidTraceLayer.h"

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

enum class ESOLReadOnlyPage : uint8;
struct FSOLHoloMenuAnim;
struct FSOLSolidScrollStack;
class UObject;
class USizeBox;
class USOLMenuButton;
class USOLReadOnlyPageWidget;
class USOLSettingsPageWidget;
class USOLSolidPanel;
class UTextBlock;
class UVerticalBox;
class UWorld;
class UWidget;
class UWidgetSwitcher;
class UWidgetTree;

// Builders for the menu pages and the solid pieces the main and pause menus share (SDD 8, 8c/8d; SDD 46): Controls,
// Info, About and Settings, the page shell, the hull panels' lower-edge traces, the stacks' hover traces, LEDs and
// power-up lights and the panels' open with its cue. Each builder builds its widgets once into the calling widget's
// tree; nothing here runs per frame except TickStackLights, which allocates nothing (the Controls and Info pages' key
// texts are refreshed when the page is shown).
namespace SOLMenuContent
{
    // The lower-edge circuit traces MakeEdgeTraces returns (SDD 46 section 6.2)
    inline constexpr int32 EDGE_TRACE_COUNT = 3;

    // Creates a text block with the menu font, a size and a colour (wrapping when bWrap is set)
    SOLTEST_API UTextBlock* MakeText(UWidgetTree& tree, const FString& text, int32 fontSize, const FLinearColor& color,
        bool bBold = false, bool bWrap = false);

    // (46g) Wraps a widget in a fixed-size box (0 leaves that axis free)
    SOLTEST_API USizeBox* MakeSized(UWidgetTree& tree, UWidget* content, float width, float height);

    // (46g) Builds the page shell (SDD 46 sections 6.1 and 6.2): a Panel hull with its own material (the panel
    // chamfers, a notch in each side at half PAGE_HEIGHT), rivets and vents; inside, the content recess holding the
    // page switcher (it fills the shell) and, under it, the Back button (Small, PAGE_BACK, index 0) in a bottom-right
    // recess. The caller sizes and slots the shell and binds Back's click
    SOLTEST_API USOLSolidPanel* BuildPageShell(UWidgetTree& tree, UWidgetSwitcher* pageSwitcher,
        USOLMenuButton*& outBackButton);

    // (46g) Returns a hull panel's lower-edge circuit traces between left and right (panel local): two rows,
    // COLUMN_TRACE_GAP apart from rowA down, with 45-degree bends, each lit from its ring terminal (the main-menu
    // column's three traces, SDD 46 section 6.2)
    SOLTEST_API void MakeEdgeTraces(float left, float right, float rowA, FSOLSolidTrace (&outTraces)[EDGE_TRACE_COUNT]);

    // (46g) Runs a stack's power-up LED blink on its panel's trace layer while bPowerUpRunning (stack order; an entry
    // whose own LED already shows, hovered, pressed, selected or disabled, is skipped; SOLSolidMenu::PowerUpLedLit) and
    // the idle pulse dot; returns whether the blink still runs (false once it ended, so the caller stops asking)
    SOLTEST_API bool TickStackLights(SSOLSolidTraceLayer& layer, const TArray<TObjectPtr<USOLMenuButton>>& buttons,
        double sinceOpenS, double nowS, float s, bool bPowerUpRunning);

    // (46g) Returns each entry's hover trace (from traceLeft, the entry's right end, to traceRight, the hull's right
    // edge) and LED centre (ledX) of a stack built in a hull panel (FSOLSolidScrollStack::Build; its ViewTop in the
    // panel's local units), on each entry's unscrolled centre line (SOLSolidScroll::StackItemTop); one per slot
    SOLTEST_API void MakeStackLights(const FSOLSolidScrollStack& stack, float ledX, float traceLeft, float traceRight,
        TArrayView<FSOLSolidTrace> outTraces, TArrayView<FVector2f> outLeds);

    // (46g) Binds a stack's buttons to their panel's (new) trace layer: each button's hover trace, the LED centres,
    // and the stack's pushed scroll forgotten so the next tick pushes it to the layer (from the owner's
    // NativeConstruct, when the panel's Slate widget, and so its layer, was rebuilt)
    SOLTEST_API void BindStackLights(const TSharedPtr<SSOLSolidTraceLayer>& layer,
        const TArray<TObjectPtr<USOLMenuButton>>& buttons, TConstArrayView<FSOLSolidTrace> traces,
        TConstArrayView<FVector2f> leds, FSOLSolidScrollStack& stack);

    // (46g) Starts a panel's open (SOLSolidMenu::BeginPanelOpen) and plays the open cue (Q11) only when it opens from
    // Closed: a reopen during a close reverses it from where it is, silently (SDD 46 section 4.6). Returns whether it
    // opened from Closed (the caller restarts what a fresh open restarts)
    SOLTEST_API bool BeginPanelOpenWithCue(const UObject& context, FSOLHoloMenuAnim& anim, double nowS, float s);

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
