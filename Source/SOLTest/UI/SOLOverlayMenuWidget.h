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
 * The hologram Overlay Menu (SDD 22 section 2.3): a checkbox list built from the overlay table (SOLOverlay), one row
 * per overlay (the icon categories indented under Icons), in the HUD palette. A row is toggled by a click, or by
 * Space / Enter on the highlighted row (Up / Down move the highlight, the mouse highlights the row under it); each
 * toggle goes to USOLSettingsSubsystem at once (applied live; written at the menu's close only when it changed).
 * Standalone in flight (Ctrl+O, closed by Ctrl+O or Esc through the owner's close delegate), or embedded in the
 * Settings page (no title, no close keys).
 */
UCLASS()
class SOLTEST_API USOLOverlayMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Shows the standalone menu with its title; close is called when Ctrl+<toggle key> or Esc is pressed
    void Open(const FSimpleDelegate& close, const FKey& toggleKey, const FString& title);

    // Makes the widget the Settings page's embedded list: no backing, title or close keys, not focusable
    void SetEmbedded(bool bEmbedded);

    // Re-reads every row from the settings
    void RefreshRows();

    // Toggles one row through the settings and refreshes (verification and clicks)
    void ToggleRow(int32 row);

    // Returns the number of rows (one per overlay)
    int32 GetRowCount() const { return RowTexts.Num(); }

    // Returns the highlighted row
    int32 GetHighlightedRow() const { return mHighlightedRow; }

    // Returns a row's centre in absolute (screen) Slate space as last laid out, or zero (verification clicks)
    FVector2D GetRowScreenCenter(int32 row) const;

protected:

    // Builds the widget tree (no widget blueprint)
    virtual void NativeOnInitialized() override;

    // Up / Down move the highlight, Space / Enter toggle, Esc and Ctrl+<toggle key> close (standalone only)
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

    // A left click on a row toggles it
    virtual FReply NativeOnMouseButtonDown(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

    // The row under the mouse is highlighted
    virtual FReply NativeOnMouseMove(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

private:

    // Returns the row under a screen-space point, or INDEX_NONE
    int32 FindRowAt(const FVector2D& screenPosition) const;

    // Moves the highlight (wrapping) and refreshes
    void MoveHighlight(int32 step);

    // Writes the key hint from the current bindings (click only when embedded)
    void RefreshHint();

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
    TArray<TObjectPtr<UBorder>> RowBorders;                 // One per overlay row (the click target)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> RowTexts;

    FSimpleDelegate mClose;                                 // Standalone: asks the owner to close the menu
    FKey mToggleKey;                                        // Standalone: Ctrl+<this> closes
    int32 mHighlightedRow = 0;
    bool mIsEmbedded = false;
};
