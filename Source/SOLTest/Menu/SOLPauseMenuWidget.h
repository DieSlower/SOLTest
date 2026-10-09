/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLMenuState.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLPauseMenuWidget.generated.h"

class USOLMenuButton;
class USOLMenuSubsystem;
class UWidgetSwitcher;

/**
 * Esc pause menu (SDD 8, 8c), a UMG widget tree built entirely in C++: a centred dark panel over the still-running
 * flight with a tab bar (Resume, Controls, Info, Settings, About, Quit) and the selected tab's page. Resume closes the
 * menu; Quit shows "Quit to main menu" and "Quit to desktop". Mouse and keyboard: hover or Tab / arrows move the
 * highlight, Enter / Space or a click activates, Esc resumes. Actions go to USOLMenuSubsystem, which calls ShowTab back
 * with the state's tab.
 */
UCLASS()
class SOLTEST_API USOLPauseMenuWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Shows a tab's page and marks its tab button selected
    void ShowTab(SOLMenuState::EMenuTab tab);

    // Gives keyboard focus to the selected tab's button
    void FocusDefault();

    // Returns how many key presses arrived through Slate (verification)
    int32 GetSlateKeyCount() const { return mSlateKeyCount; }

protected:

    // Builds the widget tree (no widget blueprint) and makes the menu focusable
    virtual void NativeOnInitialized() override;

    // Refreshes the button highlights (focus follows the keyboard and the mouse)
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

    // Esc resumes; every other key is left to the focused button and Slate navigation
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

private:

    // Quit page buttons (their indices)
    enum class EQuitChoice : uint8
    {
        ToMainMenu,
        ToDesktop,
    };

    // A tab button was clicked (index = SOLMenuState::EMenuTab)
    void HandleTabClicked(int32 tab);

    // A Quit page button was clicked
    void HandleQuitClicked(int32 choice);

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> TabButtons;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> QuitButtons;

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> PageSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    SOLMenuState::EMenuTab mTab = SOLMenuState::EMenuTab::Resume;   // Tab shown
    int32 mSlateKeyCount = 0;                                       // Key presses received through Slate
};
