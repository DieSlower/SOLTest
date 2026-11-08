/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLMenuState.h"
#include "Menu/SOLSolidScrollStack.h"
#include "Menu/SOLSolidTraceLayer.h"
#include "UI/SOLHoloMenuMath.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLMainMenuWidget.generated.h"

class UHorizontalBox;
class UScrollBox;
class USOLKeyCapRow;
class USOLMenuButton;
class USOLDemoPageWidget;
class USOLReadOnlyPageWidget;
class USOLMenuSubsystem;
class USOLSettingsPageWidget;
class USOLSolidPanel;
class UTextBlock;
class UWidgetSwitcher;

// Pages the main menu shows beside its button column
enum class ESOLMainMenuPage : uint8
{
    None,
    StartLocation,
    Controls,
    Help,           // (33d-ii, SDD 33 D16) after Controls
    Settings,
    About,
    Demo,
};

/**
 * Main menu (SDD 8, 8c; solid restyle SDD 46 section 6, part 46d), a UMG widget tree built entirely in C++: a left hull
 * column (USOLSolidPanel with a title step, a chin, rivets, vents and circuit traces along its lower edge) with the
 * title and tagline, the Play, Demo (SDD 11 Part 10e), Start location, Controls, Help (33d-ii), Settings, About and
 * Quit stack buttons (SOLMenuState::EMainMenuEntry), the hazard stripe under Quit, the key hint and the chin recess
 * with the build version line (SOLBuildVersion), over the live solar system; and the page shell to its right (SDD 46
 * section 6.1: a hull panel, the content recess with the page, a Back button in a bottom-right recess) for the
 * start-location picker (every registry body in a recess list; the start body, the one Play uses as -SOLStart does, is
 * the selected row), Controls (keys from the applied settings, so rebinds show), Help (the keyboard diagram and the HUD
 * glossary, SDD 33), Settings (8d; leaving the page ends the settings visit), About and Demo. Mouse and keyboard: hover
 * or Tab / arrows move the highlight, Enter / Space or a click activates, Esc or Back closes the open page (one code
 * path). The hovered or focused entry's circuit trace extends from its right end to the column hull's right edge (Q15).
 * Animations at the HUD effects strength (SDD 46 section 7): the column opens (fade and slide) each time the menu is
 * added to the viewport, then its traces power up and pulse; the shell opens with a page and closes over CLOSE_S after
 * the input is already back (a reopen during the close reverses). Actions go to USOLMenuSubsystem. (46g, D17) The stack
 * scrolls when it has more entries than STACK_MAX_VISIBLE_ENTRIES (FSOLSolidScrollStack: the focused entry scrolls
 * whole into view, its hover trace and LED move with it); with the 8 entries of 33d-ii it scrolls (Quit below the fold,
 * SDD 33 D31). (46g, Q11) The #42 open cue plays when the column opens and when the shell opens from closed (the
 * buttons play the hover and select cues themselves).
 */
UCLASS()
class SOLTEST_API USOLMainMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Shows a page beside the column (None hides the panel)
    void ShowPage(ESOLMainMenuPage page);

    // Returns the page shown
    ESOLMainMenuPage GetPage() const { return mPage; }

    // Updates the start-location button label and the picker's selected row from the menu subsystem
    void RefreshStartBody();

    // Gives keyboard focus to the Play button
    void FocusDefault();

    // Returns the Play button, the widget the input mode focuses when the menu shows
    USOLMenuButton* GetDefaultButton() const;

    // Returns the Settings page
    USOLSettingsPageWidget* GetSettingsPage() const { return SettingsPage; }

    // Returns the Demo page
    USOLDemoPageWidget* GetDemoPage() const { return DemoPage; }

    // Returns the Controls, Help and About pages (verification)
    USOLReadOnlyPageWidget* GetControlsPage() const { return ControlsPage; }
    USOLReadOnlyPageWidget* GetHelpPage() const { return HelpPage; }
    USOLReadOnlyPageWidget* GetAboutPage() const { return AboutPage; }

    // Returns how many entry buttons the column has
    int32 GetEntryButtonCount() const { return EntryButtons.Num(); }

    // Returns the column's entry button at an index (Play first), or null (verification)
    USOLMenuButton* GetEntryButton(int32 index) const;

    // Closes the open page and gives the focus back to Play: what Esc and the shell's Back button do
    void ClosePage();

    // Starts the column's open (fade and slide), trace power-up and LED blink, and snaps the shell shut when it is not
    // Closed (a page left open at Play, or a close still running: page None, the shell hidden at once, the selection
    // marks cleared); runs each time the menu is added to the viewport (verification scripts replay it)
    void PlayShowAnimation();

    // Returns the hull column (verification)
    USOLSolidPanel* GetColumnPanel() const { return ColumnPanel; }

    // Returns the page shell's hull panel (verification)
    USOLSolidPanel* GetShellPanel() const { return ShellPanel; }

    // Returns the shell's Back button (verification)
    USOLMenuButton* GetBackButton() const { return BackButton; }

    // Returns the chin recess's version line as shown (verification)
    FString GetVersionText() const;

    // Returns the column's open state (verification)
    const FSOLHoloMenuAnim& GetColumnAnim() const { return mColumnAnim; }

    // Returns the shell's open / close state (verification)
    const FSOLHoloMenuAnim& GetShellAnim() const { return mShellAnim; }

    // Returns the column's open progress as last applied (verification)
    float GetColumnProgress() const { return mColumnProgress; }

    // Returns the shell's open progress as last applied (verification)
    float GetShellProgress() const { return mShellProgress; }

    // Returns the column traces' power-up fraction as last applied (verification)
    float GetColumnTracePower() const { return mColumnTracePower; }

    // Returns whether the column's trace layer shows an entry's hover trace at more than half its amount
    // (verification)
    bool IsEntryHoverTraceShown(int32 index) const;

    // Returns the page index the shell's switcher shows (it keeps a closing page until the shell is closed)
    int32 GetShownPageIndex() const;

    // Returns the key hint's bottom edge in column-local units from the last layout, or -1 before one (verification)
    float GetHintBottom() const;

    // Returns the y of the column's upper trace row (column local; verification)
    float GetTraceRowA() const { return mTraceRowA; }

    // Returns how many start-location rows there are (verification)
    int32 GetBodyButtonCount() const { return BodyButtons.Num(); }

    // Returns a start-location row (body index order), or null (verification)
    USOLMenuButton* GetBodyButton(int32 index) const;

    // Re-applies the stack's visible-entry cap with this many entries (STACK_MAX_VISIBLE_ENTRIES by default; fewer
    // forces scrolling; verification)
    void SetStackMaxVisibleEntriesForVerification(int32 maxVisibleEntries);

    // Returns the stack's scroll box (verification)
    UScrollBox* GetStackScrollBox() const { return EntryStack.Scroll; }

    // Returns whether the stack needs scrolling at the last layout (verification)
    bool IsStackScrollNeeded() const { return EntryStack.NeedsScroll(); }

    // Returns the stack's scroll offset (0 = top; verification)
    float GetStackScrollOffset() const { return EntryStack.GetScrollOffset(); }

protected:

    // Builds the widget tree (no widget blueprint) and makes the menu focusable
    virtual void NativeOnInitialized() override;

    // Added to the viewport (also after a return from flight): binds the entries' hover traces to the rebuilt column
    // trace layer and plays the show animation
    virtual void NativeConstruct() override;

    // Refreshes the button highlights (focus follows the keyboard and the mouse) and runs the column's and the shell's
    // animations
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

    // Esc (or Ctrl+F1) closes the open page; every other key is left to the focused button and Slate navigation
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

private:

    // Column entries, in order (also the entry buttons' indices; 33d-ii: the public SOLMenuState::EMainMenuEntry)
    using EEntry = SOLMenuState::EMainMenuEntry;

    // A column button was clicked
    void HandleEntryClicked(int32 entry);

    // A start-location row was clicked: makes that body the start body
    void HandleBodyClicked(int32 bodyIndex);

    // The shell's Back button was clicked: closes the page as Esc does
    void HandleBackClicked(int32 index);

    // Builds the hull column into the row: title, tagline, stack, hazard stripe, hint, traces and the version recess
    void BuildColumn(UHorizontalBox& row);

    // Builds the page shell into the row: the hull, the content recess with the page switcher and the Back recess
    void BuildShell(UHorizontalBox& row);

    // Builds the start-location page: the body text and the body rows in a recess list
    UWidget* BuildStartLocationPage();

    // Runs the shell's open / close at a frame; once closed it hides the shell and drops the closed page
    void TickShell(double nowS, float s);

    // Closes the shell at once with no animation: ends a Settings visit, page None, the shell hidden and the selection
    // marks cleared
    void SnapShellClosed(double nowS, float s);

    // Runs the column's power-up LED blink (stack order, the entries whose own LED is lit skipped) and the idle pulse
    // dot on its trace layer at a frame
    void TickColumnLights(double nowS, float s);

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> EntryButtons;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> BodyButtons;

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> ColumnPanel;

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> ShellPanel;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuButton> BackButton;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> VersionText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HintText;

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> PageSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsPageWidget> SettingsPage;

    UPROPERTY(Transient)
    TObjectPtr<USOLDemoPageWidget> DemoPage;

    UPROPERTY(Transient)
    TObjectPtr<USOLReadOnlyPageWidget> ControlsPage;

    UPROPERTY(Transient)
    TObjectPtr<USOLReadOnlyPageWidget> HelpPage;        // (33d-ii) The Help page (SDD 33 D16, D22)

    UPROPERTY(Transient)
    TObjectPtr<USOLReadOnlyPageWidget> AboutPage;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLKeyCapRow>> ControlsKeyCells;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    UPROPERTY(Transient)
    FSOLSolidScrollStack EntryStack;                    // The column's stack entries (scroll when they do not fit)

    FSOLSolidTrace mEntryTraces[static_cast<int32>(EEntry::Count)];  // Each entry's hover trace (column local)
    FVector2f mEntryLeds[static_cast<int32>(EEntry::Count)];         // Each entry's LED centre (column local)
    FSOLHoloMenuAnim mColumnAnim;                       // The column's open (SOLSolidMenu::PanelProgress)
    FSOLHoloMenuAnim mShellAnim;                        // The page shell's open / close
    double mColumnOpenS = 0.0;                          // When the column's open began (the trace power-up clock)
    float mColumnProgress = 1.0f;                       // Progress last applied
    float mShellProgress = 0.0f;
    float mColumnTracePower = 1.0f;                     // Trace power-up fraction last applied
    float mTraceRowA = 0.0f;                            // The upper trace row's y (column local)
    bool mIsPowerUpRunning = false;                     // The LED blink still runs (TickColumnLights)
    ESOLMainMenuPage mPage = ESOLMainMenuPage::None;    // Page shown beside the column (None while the shell closes)
};
