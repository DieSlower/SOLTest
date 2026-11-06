/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLSolidTraceLayer.h"

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"

#include "SOLSolidTabRail.generated.h"

class USizeBox;
class USOLSolidPanel;
struct FSOLSolidMenuFrame;
class UWidgetTree;

/**
 * Pure geometry of the tab rail (SDD 46 section 6.3, shared by Settings, Controls, Info and About), widget-free.
 */
namespace SOLSolidTabRail
{
    // A tab's hover trace in rail local units: a straight run at the tab's centre line from the tab's right end
    // (TAB_COLUMN_WIDTH - SETTINGS_TAB_RAIL_PADDING - SETTINGS_TAB_TRACE_PX) to the rail's edge
    // (TAB_COLUMN_WIDTH - SETTINGS_TAB_TRACE_END_PX); two points, no ring terminal. A negative index reads as tab 0
    SOLTEST_API FSOLSolidTrace TraceForTab(int32 index);

    // The height a rail of this many tabs needs: the rail padding on both sides plus each tab's height and gap
    SOLTEST_API float RailHeight(int32 tabCount);
}

/**
 * The solid tab column (SDD 46 section 6.3, 46f: promoted from the Settings page): a recess holding one Tab button per
 * tab, top-aligned, each leaving room on its right for its hover trace, which the recess's trace layer draws from the
 * tab's end to the rail's edge. The owning page builds it once (Build), binds the buttons' hover traces to the trace
 * layer when its Slate widget exists (Bind, from NativeConstruct), shows the selected mark (Select), and ticks it
 * once per frame (Tick: the trace layer's effects strength when it changed, and each button's highlight). A plain
 * data holder for the page; it owns no Slate state of its own.
 */
USTRUCT()
struct SOLTEST_API FSOLSolidTabRail
{
    GENERATED_BODY()

    // Builds the rail for a list of tab labels (button index = position) and returns the column widget (the recess
    // in a box TAB_COLUMN_WIDTH wide); every button reports a click through onClicked
    USizeBox* Build(UWidgetTree& tree, const TArray<FString>& labels, const FSOLMenuButtonClicked& onClicked);

    // Binds each button's hover trace to the rail's trace layer (the layer exists once the Slate widget is built)
    void Bind();

    // Shows a tab as selected (the others not)
    void Select(int32 index);

    // Pushes the effects strength to the trace layer when it changed and refreshes every button's highlight
    void Tick(const FSOLSolidMenuFrame& frame, bool bHoverElsewhere);

    // Clears the hover trace left from the last visit, unless a tab still has the keyboard focus
    void ClearStaleHover();

    // Returns a tab's button, or null
    USOLMenuButton* GetButton(int32 index) const;

    // Returns whether the rail draws a tab's hover trace
    bool IsTraceShown(int32 index) const;

    // Returns whether the rail draws any hover trace
    bool IsAnyTraceShown() const;

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> Panel;       // The rail recess; its trace layer draws the hovered tab's trace

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> Buttons;

    TArray<FSOLSolidTrace> Traces;          // Each tab's hover trace (rail local)
    float Strength = -1.0f;                 // Effects strength last pushed to the trace layer
};
