/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Components/Button.h"
#include "CoreMinimal.h"

#include "SOLMenuButton.generated.h"

class UTextBlock;
class UWidgetTree;

// Fired when a menu button is clicked (mouse, or Enter / Space while it has keyboard focus), with its index
DECLARE_DELEGATE_OneParam(FSOLMenuButtonClicked, int32 /*index*/);

/**
 * Menu button (SDD 8, 8c): a UButton with the menu's dark translucent look, a text label and an index, whose clicks go
 * to one native delegate so a menu can route a whole list of buttons through one handler. Hovering it with the mouse
 * gives it keyboard focus, so the mouse and the keyboard share one highlight; the owning menu refreshes the highlight
 * (focused, selected or normal) from its tick through RefreshHighlight.
 */
UCLASS()
class SOLTEST_API USOLMenuButton : public UButton
{
    GENERATED_BODY()

public:

    // Styles the button, creates its label in the owner's widget tree and routes clicks and hovers
    void Setup(UWidgetTree& tree, const FString& label, int32 index, int32 fontSize);

    // Replaces the label text
    void SetLabel(const FString& label);

    // Marks the button as the selected entry (active tab, page or start body)
    void SetSelected(bool bSelected) { mIsSelected = bSelected; }

    // Returns whether the button is the selected entry
    bool IsSelected() const { return mIsSelected; }

    // Returns the button's index
    int32 GetIndex() const { return mIndex; }

    // Recolours the button for keyboard focus, selection or neither (cheap; skips unchanged states)
    void RefreshHighlight();

    // Clicks, with the button's index
    FSOLMenuButtonClicked OnMenuClicked;

private:

    // UButton click: forwards the index to OnMenuClicked
    UFUNCTION()
    void HandleClicked();

    // UButton hover: takes keyboard focus so the highlight follows the mouse
    UFUNCTION()
    void HandleHovered();

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Label;

    int32 mIndex = INDEX_NONE;          // Index handed to OnMenuClicked
    int32 mLastHighlight = INDEX_NONE;  // Highlight state last applied (0 normal, 1 selected, 2 focused)
    bool mIsSelected = false;           // Selected entry of its menu
};
