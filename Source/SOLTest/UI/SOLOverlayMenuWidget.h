/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLOverlayModel.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "SOLOverlayMenuWidget.generated.h"

class UBorder;
class UTextBlock;
class USOLSettingsSubsystem;

/**
 * The Overlay Menu (SDD 22 section 2.3, SDD 34 section 7): a column of profile tabs (Profile 1-5) left of a checkbox
 * list built from the overlay table (SOLOverlay), in the HUD palette. The list shows the viewed profile's page: every
 * table row (the icon categories indented under Icons), an "Orbit Lines" row (the profile's Alt+O switch) with the
 * four orbit rows indented under it, and on profiles 2-5 a "Profile enabled" row; a disabled profile's page holds
 * only its enable row. Viewing an enabled profile makes it the active one (D11). A row is toggled by a click, or by
 * Space / Enter on the highlighted row (Up / Down move the highlight, the mouse highlights the row under it); a tab
 * by a click or PageUp / PageDown; the profile cycle key (Right Shift + its key) cycles the active profile and the
 * view follows. Each change goes to USOLSettingsSubsystem at once (applied live; written at the menu's close only
 * when it changed). Standalone in flight (Ctrl+O, closed by Ctrl+O or Esc through the owner's close delegate), or
 * embedded in the Settings page (no title, no keys: tabs and rows are clicked, D17).
 */
UCLASS()
class SOLTEST_API USOLOverlayMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Display rows of an enabled profile's page: the table rows with "Orbit Lines" inserted above the orbit rows,
    // then "Profile enabled" (profiles 2-5 only). A disabled profile's page has only row 0 (its enable row)
    static constexpr int32 ORBIT_LINES_ROW = static_cast<int32>(SOLOverlay::EOverlay::PlanetOrbits);
    static constexpr int32 PROFILE_ENABLED_ROW = SOLOverlay::OVERLAY_COUNT + 1;
    static constexpr int32 ROW_WIDGET_COUNT = SOLOverlay::OVERLAY_COUNT + 2;

    // Shows the standalone menu; close is called when Ctrl+<toggle key> or Esc is pressed. The title format's
    // SOLMenuText::OVERLAY_PROFILE_NUMBER_TOKEN is replaced by the viewed profile's number
    void Open(const FSimpleDelegate& close, const FKey& toggleKey, const FString& titleFormat);

    // Makes the widget the Settings page's embedded list: no backing, title or keys, not focusable
    void SetEmbedded(bool bEmbedded);

    // Re-reads the tabs, the title and every row from the settings (the view follows the active profile)
    void RefreshRows();

    // Acts on one display row of the viewed page through the settings and refreshes (verification and clicks)
    void ToggleRow(int32 row);

    // Views a profile as a tab click does: an enabled one becomes active, a disabled one shows its enable page
    void ClickTab(int32 profile);

    // Returns the number of rows on the viewed page (16 on Profile 1, 17 on profiles 2-5, 1 on a disabled one)
    int32 GetRowCount() const;

    // Returns the display row of a table row on an enabled profile's page
    static int32 GetOverlayRow(SOLOverlay::EOverlay overlay);

    // Returns the highlighted display row
    int32 GetHighlightedRow() const { return mHighlightedRow; }

    // Returns the profile whose page is shown (0-based)
    int32 GetViewedProfile() const { return mViewedProfile; }

    // Returns the number of profile tabs
    int32 GetTabCount() const { return TabTexts.Num(); }

    // Returns a display row's text as shown (verification)
    FString GetRowText(int32 row) const;

    // Returns a display row's centre in absolute (screen) Slate space as last laid out, or zero (verification clicks)
    FVector2D GetRowScreenCenter(int32 row) const;

    // Returns a profile tab's centre in absolute (screen) Slate space as last laid out, or zero (verification clicks)
    FVector2D GetTabScreenCenter(int32 profile) const;

protected:

    // Builds the widget tree (no widget blueprint)
    virtual void NativeOnInitialized() override;

    // Up / Down move the highlight, Space / Enter toggle, PageUp / PageDown switch tabs, the cycle key cycles, Esc and
    // Ctrl+<toggle key> close (standalone only)
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

    // A left click on a tab views it, on a row toggles it
    virtual FReply NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

    // The row under the mouse is highlighted
    virtual FReply NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

private:

    // What a display row of the viewed page is
    enum class ESOLOverlayRowKind : uint8
    {
        Overlay,            // A table row
        OrbitLines,         // The profile's orbit-lines switch (D19)
        ProfileEnabled,     // "[x] Profile enabled" (profiles 2-5)
        EnableProfile,      // A disabled profile's enable row
        Hidden,             // Not on this page
    };

    // Returns what a display row is on the viewed page
    ESOLOverlayRowKind GetRowKind(int32 row) const;

    // Returns the table row of a display row on an enabled page, or INDEX_NONE ("Orbit Lines", "Profile enabled")
    static int32 GetRowOverlay(int32 row);

    // Returns the row under a screen-space point, or INDEX_NONE
    int32 FindRowAt(const FVector2D& screenPosition) const;

    // Returns the tab under a screen-space point, or INDEX_NONE
    int32 FindTabAt(const FVector2D& screenPosition) const;

    // Moves the highlight (wrapping over the viewed page's rows) and refreshes
    void MoveHighlight(int32 step);

    // Views the next (step 1) or previous (step -1) tab, wrapping (PageDown / PageUp)
    void StepTab(int32 step);

    // Cycles the active profile (the cycle key), views it and shows the HUD tag
    void CycleProfile();

    // Returns true if a key event is the profile cycle key: Right Shift held and the binding's key
    bool IsCycleKey(const FKeyEvent& keyEvent) const;

    // Writes the title with the viewed profile's number (standalone)
    void RefreshTitle();

    // Writes the key hint from the current bindings (click only when embedded)
    void RefreshHint();

    // Re-reads the five tabs (the active mark and colour, the off suffix, the viewed tab's backing), only when the
    // enabled profiles, the active one or the viewed one changed since the last call
    void RefreshTabs(const SOLOverlay::FOverlayPrefs& prefs);

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> RootBorder;                         // Full-screen transparent root (standalone layout)

    UPROPERTY(Transient)
    TObjectPtr<UBorder> PanelBorder;                        // The dark panel behind the list

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> TitleText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HintText;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UBorder>> TabBorders;                 // One per profile (the click target)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> TabTexts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UBorder>> RowBorders;                 // One per display row (the click target)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> RowTexts;

    FSimpleDelegate mClose;                                 // Standalone: asks the owner to close the menu
    FKey mToggleKey;                                        // Standalone: Ctrl+<this> closes
    FString mTitleFormat;                                   // Standalone: the title with the profile number token
    int32 mHighlightedRow = 0;
    int32 mViewedProfile = 0;
    int32 mTitleProfile = INDEX_NONE;                       // The profile the title was last written for
    int32 mTabsEnabledProfiles = INDEX_NONE;                // The tab state last written (INDEX_NONE: never)
    int32 mTabsActiveProfile = INDEX_NONE;
    int32 mTabsViewedProfile = INDEX_NONE;
    bool mIsEmbedded = false;
};
