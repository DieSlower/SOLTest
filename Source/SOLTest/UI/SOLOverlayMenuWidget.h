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
class UOverlay;
class UTextBlock;
class UVerticalBox;
class USOLHoloFrameWidget;
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
 *
 * Standalone, it is a hologram menu (SDD 42 part 42e): a USOLHoloFrameWidget behind the content draws the option (c)
 * frame with the faint tint (D20): the title step sized to the title, the dashed column divider open beside the viewed
 * tab, the tab markers, the hint divider, and the hover mark around the highlighted row (brackets and pointer) or the
 * tab under the mouse (brackets only); the content's render opacity follows the frame's fill. It opens with a draw-in
 * and plays the open, hover and select cues. BeginClose gives input back at once: the widget turns hit-test invisible
 * and unfocusable, plays the close and removes itself from its NativeTick (SDD 4.5); Open during the close reverses
 * it. The NativeTick does nothing while the menu is open and idle. The embedded copy gets no frame (D17).
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

    // Starts the non-blocking close (SDD 4.5): hit-test invisible and unfocusable at once, the frame's close plays,
    // then the widget removes itself (at once when nothing is left to draw, e.g. at strength 0)
    void BeginClose();

    // Removes the menu at once without the close (teardown); the next Open draws in again with its open cue
    void Dismiss();

    // Returns whether the close is playing or done and the menu not reopened since
    bool IsClosing() const { return mIsClosing; }

    // Returns whether this is the Settings page's embedded list
    bool IsEmbedded() const { return mIsEmbedded; }

    // Returns the hologram frame, or null (embedded, or never opened standalone)
    USOLHoloFrameWidget* GetHoloFrame() const { return HoloFrame; }

    // Returns the panel's backing colour as set (the embedded list's look, verification)
    FLinearColor GetPanelColor() const;

    // Returns the content's render opacity (the frame's fill alpha, verification)
    float GetContentOpacity() const { return mContentOpacity; }

    // Returns the profile tab under the mouse (its hover mark), or INDEX_NONE
    int32 GetHoveredTab() const { return mHoveredTab; }

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

    // While the frame animates: advances it, sets the content opacity, removes the widget once closed; after a
    // content or hover change: lays the frame out from the arranged content. Nothing while open and idle
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

    // Up / Down move the highlight, Space / Enter toggle, PageUp / PageDown switch tabs, the cycle key cycles, Esc and
    // Ctrl+<toggle key> close (standalone only)
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

    // A left click on a tab views it, on a row toggles it
    virtual FReply NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

    // The row under the moving mouse is highlighted; the tab under it gets the hover mark (a synthetic move that did
    // not move the cursor changes nothing)
    virtual FReply NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

    // The mouse left the menu: no tab is hovered
    virtual void NativeOnMouseLeave(const FPointerEvent& mouseEvent) override;

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

    // Creates the hologram frame behind the panel once (standalone only) and clears the panel's own backing
    void EnsureHoloFrame();

    // Asks the frame layout to be rebuilt from the arranged content over the next frames
    void MarkLayoutDirty();

    // Sets the frame's layout and hover mark from the content's arranged geometry (frame-local units)
    void UpdateFrameLayout();

    // Sets the frame's hovered item: the tab under the mouse, else the highlighted row
    void UpdateFrameHover();

    // Sets the content's render opacity when it changed
    void ApplyContentOpacity(float opacity);

    // Opens the built frame (or reverses its close) with the open cue
    void StartFrameOpen();

    // Ends the close: removes the widget
    void FinishClose();

    // Requests the select cue (standalone, not while closing)
    void RequestSelectCue();

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> RootBorder;                         // Full-screen transparent root (standalone layout)

    UPROPERTY(Transient)
    TObjectPtr<UOverlay> PanelStack;                        // The frame (standalone) behind the panel

    UPROPERTY(Transient)
    TObjectPtr<UBorder> PanelBorder;                        // The panel: dark when embedded, clear over the frame

    UPROPERTY(Transient)
    TObjectPtr<USOLHoloFrameWidget> HoloFrame;              // The hologram frame (standalone only, D17)

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> TabColumn;

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> RowColumn;

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
    int32 mHoveredTab = INDEX_NONE;                         // The tab under the mouse (standalone), or none
    int32 mLayoutDirtyFrames = 0;                           // Frames left to re-lay the frame out after a change
    uint32 mIndentedRows = 0;                               // Bit per display row shown indented
    int32 mLastRowHoverItem = INDEX_NONE;                   // The row last given to the frame as its hover item
    float mIndentWidth = -1.0f;                             // The child indent's width in the row font (<0: not yet)
    float mContentOpacity = 1.0f;                           // The content's render opacity as last set
    ESlateVisibility mOpenVisibility = ESlateVisibility::Visible;  // The widget's visibility while open
    bool mIsEmbedded = false;
    bool mIsClosing = false;                                // BeginClose ran and Open did not since
    bool mIsAnimating = false;                              // The frame is opening or closing
    bool mIsOpenPending = false;                            // Opened before the frame's Slate widget was built
};
