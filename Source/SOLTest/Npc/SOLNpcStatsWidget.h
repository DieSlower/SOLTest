/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLNpcStatsWidget.generated.h"

class UTextBlock;

/**
 * NPC stats overlay (SDD 11 section 3.8, Part 10e): a small translucent panel in the top-right corner with the lines
 * SOLNpcStatsText::FormatOverlayLines builds. Display only (never focusable, never hit-testable); USOLNpcSubsystem
 * creates it, shows or hides it (sol.Npc.Stats, Ctrl+F2) and sets its text at 4 Hz, not per frame.
 */
UCLASS()
class SOLTEST_API USOLNpcStatsWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Replaces the shown text (one line per entry)
    void SetLines(const TArray<FString>& lines);

    // Returns the shown text (verification)
    FString GetText() const;

protected:

    // Builds the widget tree (no widget blueprint)
    virtual void NativeOnInitialized() override;

private:

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Text;
};
