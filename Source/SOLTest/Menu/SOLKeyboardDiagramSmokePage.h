/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLKeyboardDiagramSmokePage.generated.h"

class USOLKeyboardDiagram;
class USOLSolidPanel;

/**
 * Verification-only test page of the keyboard diagram (-SOLSmokeSolidMenu, SDD 33 section 16, plan 33c item 3; the
 * Help page that hosts the diagram comes in 33d-ii): a hull panel centred on the viewport holding one
 * USOLKeyboardDiagram laid out in a given content width. It is the diagram's owner as the Help page will be: it sets
 * the live keys on construct and binds USOLSettingsSubsystem::OnKeyBindingsChanged to set them again (unbound on
 * destruct). Built in C++, no widget blueprint. A C++-only UUserWidget may still be ticked by Slate (UpdateCanTick);
 * this page overrides no tick, so its tick does nothing, and the diagram leaf itself never ticks.
 */
UCLASS()
class SOLTEST_API USOLKeyboardDiagramSmokePage : public UUserWidget
{
    GENERATED_BODY()

public:

    // Sets the content width (menu units) the diagram is laid out in
    void SetContentWidth(float width);

    // Returns the diagram (null before the page is initialized)
    USOLKeyboardDiagram* GetDiagram() const { return Diagram; }

    // Returns how often the key bindings changed handler ran (verification: the live rebuild came from the event)
    int32 GetBindingsChangedCount() const { return mBindingsChanged; }

protected:

    // Builds the page: the transparent root, the hull panel and the diagram
    virtual void NativeOnInitialized() override;

    // Sets the live keys and binds the key bindings changed event
    virtual void NativeConstruct() override;

    // Unbinds the key bindings changed event
    virtual void NativeDestruct() override;

private:

    // Sets the diagram's keys to the live ones (the event handler)
    void HandleKeyBindingsChanged();

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> Panel;

    UPROPERTY(Transient)
    TObjectPtr<USOLKeyboardDiagram> Diagram;

    FDelegateHandle mBindingsHandle;
    int32 mBindingsChanged = 0;
};
