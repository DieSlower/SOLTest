/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetTree;

// Builders for the menu pages the main and pause menus share (SDD 8, 8c): Controls, Info, About and the Settings
// placeholder. Each builds its widgets once into the calling widget's tree; nothing here runs per frame.
namespace SOLMenuContent
{
    // Creates a text block with the menu font, a size and a colour (wrapping when bWrap is set)
    SOLTEST_API UTextBlock* MakeText(UWidgetTree& tree, const FString& text, int32 fontSize, const FLinearColor& color,
        bool bBold = false, bool bWrap = false);

    // Creates a vertical page with a heading, ready for rows
    SOLTEST_API UVerticalBox* MakePage(UWidgetTree& tree, const TCHAR* title);

    // Returns a key binding cell's text: each '|' alternative is a '+' chord of engine key names shown through
    // SOLBindingLabel, the alternatives joined by " / "
    SOLTEST_API FString FormatBindings(const TCHAR* bindings);

    // Returns a time-warp factor as "10x", "1 h/s" or "30 d/s"
    SOLTEST_API FString FormatWarpFactor(double factor);

    // Builds the Controls page (every binding grouped by area, in a scroll box)
    SOLTEST_API UWidget* BuildControlsPage(UWidgetTree& tree);

    // Builds the Info page (flight, time and combat values generated from the real constants, plus prose)
    SOLTEST_API UWidget* BuildInfoPage(UWidgetTree& tree);

    // Builds the About page (game, studio, engine version, credits)
    SOLTEST_API UWidget* BuildAboutPage(UWidgetTree& tree);

    // Builds the Settings page placeholder (filled by part 8d)
    SOLTEST_API UWidget* BuildSettingsPage(UWidgetTree& tree);
}
