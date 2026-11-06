/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLMenuState.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidScrollStack.h"
#include "Menu/SOLSolidTraceLayer.h"
#include "UI/SOLHoloMenuMath.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLPauseMenuWidget.generated.h"

class UBorder;
class UHorizontalBox;
class USOLDemoPageWidget;
class USOLReadOnlyPageWidget;
class USOLMenuButton;
class USOLMenuSubsystem;
class USOLSettingsPageWidget;
class USOLSolidPanel;
class UTextBlock;
class UWidgetSwitcher;

/**
 * Esc pause menu (SDD 8, 8c; solid restyle SDD 46 section 6, part 46g), a UMG widget tree built entirely in C++: over
 * the live, still-running flight (only dimmed, Q3) a side panel on the left (USOLSolidPanel hull PAUSE_PANEL_WIDTH
 * wide, full height minus SCREEN_MARGIN, "PAUSED" on its stepped top edge, rivets, vents and circuit traces along its
 * lower edge) with the tabs (Resume, Controls, Info, Settings, Demo, About, Quit; button index =
 * SOLMenuState::EMenuTab) as stack buttons in a scrolling stack (FSOLSolidScrollStack, STACK_MAX_VISIBLE_ENTRIES,
 * D17), each with a hover trace to the hull's right edge and an LED for the power-up blink; under the stack the
 * paused text and key hint, or with Quit selected the Quit prompt and its two choices (to main menu, to desktop; Q4).
 * The page tabs (SOLMenuState::IsPageTab) open their page in the page shell right of the side panel (the main menu's
 * shell, SDD 46 section 6.1, with a Back button); Resume and Quit show no page (the shell closes). Mouse and keyboard:
 * hover or Tab / arrows move the highlight, Enter / Space or a click activates; Esc (or Ctrl+F1) and Back close an
 * open page (USOLMenuSubsystem::ClosePausePage), Esc with no page resumes (Q4). Actions go to USOLMenuSubsystem,
 * which calls ShowTab back with the state's tab. Showing the Controls or Info tab rewrites its key texts from the
 * ship's live mapping (so rebinds show); showing the Settings tab refreshes it and leaving it ends the settings visit
 * (8d). Animations at the HUD effects strength (SDD 46 sections 4.6 and 7, SOLSolidMenu::EvaluatePausePanel): PlayOpen
 * slides the side panel in from past the left edge over PAUSE_SLIDE_S (render translation only) while the dim fades
 * up, then its traces
 * power up and pulse; BeginClose gives the input back at once (the whole widget hit-test invisible) and slides it out
 * over CLOSE_S, the shell closing with it, then the widget removes itself from the viewport in its tick; a PlayOpen
 * during the close reverses it. The shell opens and closes like the main menu's. At strength 0 every change is instant.
 */
UCLASS()
class SOLTEST_API USOLPauseMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Shows a tab: a page tab opens its page in the shell (a reopen during the shell's close reverses it), Resume and
    // Quit close the shell; Quit shows its choices in the side panel; the tab of the shown content reads as selected
    void ShowTab(SOLMenuState::EMenuTab tab);

    // Starts the side panel's slide-in and takes input again; from Closed it also restarts the traces' power-up and
    // LED blink, scrolls the stack to its top and plays the open cue, while a reopen during the slide-out only reverses
    // it from where it is (SDD 46 section 4.6, no pop); the subsystem calls it each time the pause menu opens
    void PlayOpen();

    // Starts the slide-out (SDD 46 section 4.6: the input is already back): the whole widget hit-test invisible at
    // once, the shell closing with the panel; once Closed the widget removes itself from the viewport (at once at
    // strength 0)
    void BeginClose();

    // Closes at once with no animation: the panel and shell Closed and the widget removed from the viewport (Quit to
    // main menu)
    void SnapClosed();

    // Gives keyboard focus to the selected tab's button
    void FocusDefault();

    // Returns the selected tab's button, the widget the input mode focuses when the menu shows
    USOLMenuButton* GetDefaultButton() const;

    // Returns the tab shown
    SOLMenuState::EMenuTab GetTab() const { return mTab; }

    // Returns the Settings tab's page
    USOLSettingsPageWidget* GetSettingsPage() const { return SettingsPage; }

    // Returns the Demo tab's page
    USOLDemoPageWidget* GetDemoPage() const { return DemoPage; }

    // Returns the Controls, Info and About pages (verification)
    USOLReadOnlyPageWidget* GetControlsPage() const { return ControlsPage; }
    USOLReadOnlyPageWidget* GetInfoPage() const { return InfoPage; }
    USOLReadOnlyPageWidget* GetAboutPage() const { return AboutPage; }

    // Returns a Controls row's key text (row = SOLMenuText::CONTROL_ROWS index; verification)
    FString GetControlsRowText(int32 row) const;

    // Returns how many key presses arrived through Slate (verification)
    int32 GetSlateKeyCount() const { return mSlateKeyCount; }

    // Returns an Info page key-naming text (0 the manual surface-lock label, then the prose paragraphs; verification)
    FString GetInfoKeyText(int32 index) const;

    // Returns a tab's button, or nullptr (verification)
    USOLMenuButton* GetTabButton(SOLMenuState::EMenuTab tab) const;

    // Returns how many tab buttons the side panel has (verification)
    int32 GetTabButtonCount() const { return TabButtons.Num(); }

    // Returns a Quit choice's button (0 to main menu, 1 to desktop), or nullptr (verification)
    USOLMenuButton* GetQuitButton(int32 choice) const;

    // Returns whether the side panel shows the Quit prompt and choices instead of the paused text (verification)
    bool AreQuitChoicesShown() const;

    // Returns the side panel (verification)
    USOLSolidPanel* GetSidePanel() const { return SidePanel; }

    // Returns the page shell's hull panel (verification)
    USOLSolidPanel* GetShellPanel() const { return ShellPanel; }

    // Returns the shell's Back button (verification)
    USOLMenuButton* GetBackButton() const { return BackButton; }

    // Returns the side panel's open / close state (verification)
    const FSOLHoloMenuAnim& GetPanelAnim() const { return mPanelAnim; }

    // Returns the side panel's look as last applied: progress, slide, dim, shown, interactive (verification)
    const FSOLPausePanelLook& GetPanelLook() const { return mPanelLook; }

    // Returns the side panel's open progress as last applied (verification)
    float GetPanelProgress() const { return mPanelLook.Progress; }

    // Returns the dim's alpha as last written (verification)
    float GetDimAlpha() const { return mAppliedDimAlpha; }

    // Returns the side panel traces' power-up fraction as last applied (verification)
    float GetTracePower() const { return mTracePower; }

    // Returns the shell's open / close state (verification)
    const FSOLHoloMenuAnim& GetShellAnim() const { return mShellAnim; }

    // Returns the shell's open progress as last applied (verification)
    float GetShellProgress() const { return mShellProgress; }

    // Returns the page index the shell's switcher shows (the page tab's EMenuTab value, 0 for none; it keeps a closing
    // page until the shell is closed)
    int32 GetShownPageIndex() const;

    // Returns whether the side panel's trace layer shows a tab's hover trace at more than half its amount
    // (verification)
    bool IsTabHoverTraceShown(SOLMenuState::EMenuTab tab) const;

    // Returns the tab stack (verification: its scroll box, cap and scroll state)
    FSOLSolidScrollStack& GetTabStack() { return TabStack; }

protected:

    // Builds the widget tree (no widget blueprint) and makes the menu focusable
    virtual void NativeOnInitialized() override;

    // Added to the viewport: the rebuilt side panel has a new trace layer, so the tabs' hover traces and the LEDs bind
    // to it (the lower-edge traces reach it from the panel's pending traces, USOLSolidPanel::RebuildWidget)
    virtual void NativeConstruct() override;

    // Refreshes the button highlights (focus follows the keyboard and the mouse), runs the side panel's slide, dim,
    // traces and stack scroll and the shell's open / close; removes the widget once a close has finished
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

    // Esc (or Ctrl+F1) closes an open page, else resumes (an auto-repeat of a held Esc does neither); every other key
    // is left to the focused button and Slate navigation
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

private:

    static constexpr int32 TAB_COUNT = static_cast<int32>(SOLMenuState::EMenuTab::Count);

    // Quit choice buttons (their indices)
    enum class EQuitChoice : uint8
    {
        ToMainMenu,
        ToDesktop,
    };

    // A tab button was clicked (index = SOLMenuState::EMenuTab)
    void HandleTabClicked(int32 tab);

    // A Quit choice button was clicked
    void HandleQuitClicked(int32 choice);

    // The shell's Back button was clicked: closes the page as Esc does
    void HandleBackClicked(int32 index);

    // Builds the side panel into the row: title step, the tab stack, the paused text / Quit choices
    void BuildSidePanel(UHorizontalBox& row);

    // Builds the page shell into the row (SOLMenuContent::BuildPageShell) with one switcher child per page tab
    void BuildShell(UHorizontalBox& row);

    // Applies the side panel's look at a frame (slide, dim; each written only on a change)
    void ApplyPanelLook(const FSOLPausePanelLook& look);

    // Runs the shell's open / close at a frame, faded with the side panel; once closed it hides the shell and drops
    // the closed page
    void TickShell(double nowS, float s);

    // Runs the side panel's trace power-up, pulse, LED blink and idle pulse dot at a frame
    void TickPanelLights(double nowS, float s);

    // Sets the lower-edge traces for the side panel's laid-out height when it changed (it is the view's height)
    void RefreshEdgeTraces();

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> TabButtons;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> QuitButtons;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> DimBorder;

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> SidePanel;

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> ShellPanel;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuButton> BackButton;

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> DetailSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> PageSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsPageWidget> SettingsPage;

    UPROPERTY(Transient)
    TObjectPtr<USOLDemoPageWidget> DemoPage;

    UPROPERTY(Transient)
    TObjectPtr<USOLReadOnlyPageWidget> ControlsPage;

    UPROPERTY(Transient)
    TObjectPtr<USOLReadOnlyPageWidget> InfoPage;

    UPROPERTY(Transient)
    TObjectPtr<USOLReadOnlyPageWidget> AboutPage;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> ControlsKeyCells;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> InfoKeyTexts;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    UPROPERTY(Transient)
    FSOLSolidScrollStack TabStack;                      // The side panel's tabs (scroll when they do not fit)

    FSOLSolidTrace mTabTraces[TAB_COUNT];               // Each tab's hover trace (side panel local)
    FVector2f mTabLeds[TAB_COUNT];                      // Each tab's LED centre (side panel local)
    FSOLHoloMenuAnim mPanelAnim;                        // The side panel's slide-in / slide-out
    FSOLHoloMenuAnim mShellAnim;                        // The page shell's open / close
    FSOLPausePanelLook mPanelLook;                      // The side panel's look last applied
    double mPanelOpenS = 0.0;                           // When the last open began (the trace power-up clock)
    float mAppliedSlideX = 0.0f;                        // Side panel translation last written
    float mAppliedDimAlpha = -1.0f;                     // Dim alpha last written (-1: none yet)
    float mShellProgress = 0.0f;                        // Shell progress last applied
    float mTracePower = 1.0f;                           // Trace power-up fraction last applied
    float mEdgeTraceHeight = -1.0f;                     // The side panel height the edge traces were made for
    ESlateVisibility mOpenVisibility = ESlateVisibility::SelfHitTestInvisible;  // Visibility while open (input)
    SOLMenuState::EMenuTab mTab = SOLMenuState::EMenuTab::Resume;   // Tab shown
    int32 mSlateKeyCount = 0;                           // Key presses received through Slate
    bool mIsPowerUpRunning = false;                     // The LED blink still runs (TickPanelLights)
    bool mIsClosing = false;                            // BeginClose ran and the widget leaves once Closed
};
