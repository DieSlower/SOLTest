/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLMainMenuWidget.generated.h"

class UBorder;
class USOLMenuButton;
class USOLMenuSubsystem;
class UWidgetSwitcher;

// Pages the main menu shows beside its button column
enum class ESOLMainMenuPage : uint8
{
    None,
    StartLocation,
    Controls,
    Settings,
    About,
};

/**
 * Main menu (SDD 8, 8c), a UMG widget tree built entirely in C++: a left column with the title and the Play, Start
 * location, Controls, Settings, About and Quit buttons over the live solar system, and a page panel beside it for the
 * start-location picker (every registry body; the choice is the start body Play uses, the same mechanism as -SOLStart),
 * Controls, Settings (placeholder until 8d) and About. Mouse and keyboard: hover or Tab / arrows move the highlight,
 * Enter / Space or a click activates, Esc closes the open page. Actions go to USOLMenuSubsystem.
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

protected:

    // Builds the widget tree (no widget blueprint) and makes the menu focusable
    virtual void NativeOnInitialized() override;

    // Refreshes the button highlights (focus follows the keyboard and the mouse)
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

    // Esc closes the open page; every other key is left to the focused button and Slate navigation
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

private:

    // Column entries, in order (also the entry buttons' indices)
    enum class EEntry : uint8
    {
        Play,
        StartLocation,
        Controls,
        Settings,
        About,
        Quit,
        Count,
    };

    // A column button was clicked
    void HandleEntryClicked(int32 entry);

    // A start-location row was clicked: makes that body the start body
    void HandleBodyClicked(int32 bodyIndex);

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> EntryButtons;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> BodyButtons;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> PagePanel;

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> PageSwitcher;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    ESOLMainMenuPage mPage = ESOLMainMenuPage::None;    // Page shown beside the column
};
