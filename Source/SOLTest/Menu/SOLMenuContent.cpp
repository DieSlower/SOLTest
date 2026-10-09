/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuContent.h"

#include "Flight/SOLFlight.h"
#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLInfoText.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "Universe/SOLSimClock.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Misc/EngineVersion.h"
#include "Styling/CoreStyle.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Adds a section heading to a box, with space above it
    void MenuContentAddSection(UWidgetTree& tree, UPanelWidget& box, const FString& heading)
    {
        using namespace SOL::MenuStyle;
        UTextBlock* text = SOLMenuContent::MakeText(tree, heading, SECTION_FONT_SIZE, ACCENT_COLOR, true);
        if (UVerticalBox* vertical = Cast<UVerticalBox>(&box))
        {
            vertical->AddChildToVerticalBox(text)->SetPadding(FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f, ROW_GAP));
        }
        else if (UScrollBox* scroll = Cast<UScrollBox>(&box))
        {
            Cast<UScrollBoxSlot>(scroll->AddChild(text))->SetPadding(FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f, ROW_GAP));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a two-column row: a fixed-width left cell and a wrapping right cell
    UHorizontalBox* MenuContentMakeRow(UWidgetTree& tree, const FString& left, const FString& right,
        const float leftWidth, const FLinearColor& leftColor)
    {
        using namespace SOL::MenuStyle;
        UHorizontalBox* row = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        USizeBox* leftBox = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        leftBox->SetWidthOverride(leftWidth);
        leftBox->SetContent(SOLMenuContent::MakeText(tree, left, BODY_FONT_SIZE, leftColor, true, true));
        row->AddChildToHorizontalBox(leftBox)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
        UHorizontalBoxSlot* rightSlot = row->AddChildToHorizontalBox(SOLMenuContent::MakeText(tree, right,
            BODY_FONT_SIZE, TEXT_COLOR, false, true));
        rightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        return row;
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a row to a scroll box with the row gap below it
    void MenuContentAddRow(UScrollBox& scroll, UWidget* row)
    {
        Cast<UScrollBoxSlot>(scroll.AddChild(row))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, SOL::MenuStyle::ROW_GAP));
    }

    //////////////////////////////////////////////////////////////////////////
    // Wraps a page body in a scroll box under the page heading and returns the scroll box
    UScrollBox* MenuContentAddScroll(UWidgetTree& tree, UVerticalBox& page)
    {
        UScrollBox* scroll = tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
        page.AddChildToVerticalBox(scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        return scroll;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a value with its unit format applied
    FString MenuContentWithUnit(const TCHAR* format, const double value)
    {
        return FString::Format(format, { SOLInfoText::FormatPercentOrValue(value) });
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates a text block with the menu font, a size and a colour (wrapping when bWrap is set)
UTextBlock* SOLMenuContent::MakeText(UWidgetTree& tree, const FString& text, const int32 fontSize,
    const FLinearColor& color, const bool bBold, const bool bWrap)
{
    using namespace SOL::MenuStyle;
    UTextBlock* block = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? FONT_BOLD : FONT_REGULAR, fontSize));
    block->SetColorAndOpacity(FSlateColor(color));
    block->SetText(FText::FromString(text));
    block->SetAutoWrapText(bWrap);
    return block;
}

//////////////////////////////////////////////////////////////////////////
// Creates a vertical page with a heading, ready for rows
UVerticalBox* SOLMenuContent::MakePage(UWidgetTree& tree, const TCHAR* title)
{
    using namespace SOL::MenuStyle;
    UVerticalBox* page = tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    page->AddChildToVerticalBox(MakeText(tree, title, HEADING_FONT_SIZE, TEXT_COLOR, true))->SetPadding(
        FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Returns a key binding cell's text: '|' alternatives of '+' chords, each key through SOLBindingLabel
FString SOLMenuContent::FormatBindings(const TCHAR* bindings)
{
    TArray<FString> alternatives;
    FString(bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
    TArray<FString> labels;
    labels.Reserve(alternatives.Num());
    for (const FString& alternative : alternatives)
    {
        TArray<FString> chord;
        alternative.ParseIntoArray(chord, SOLMenuText::BINDING_CHORD_DELIMITER);
        labels.Add(SOLBindingLabel::JoinChord(chord));
    }
    return FString::Join(labels, SOLMenuText::BINDING_ALTERNATIVE_SEPARATOR);
}

//////////////////////////////////////////////////////////////////////////
// Returns a time-warp factor as "10x", "1 h/s" or "30 d/s"
FString SOLMenuContent::FormatWarpFactor(const double factor)
{
    if (factor >= SOL::SECONDS_PER_DAY)
    {
        return MenuContentWithUnit(SOLMenuText::WARP_DAYS_FORMAT, factor / SOL::SECONDS_PER_DAY);
    }
    if (factor >= SOL::SECONDS_PER_HOUR)
    {
        return MenuContentWithUnit(SOLMenuText::WARP_HOURS_FORMAT, factor / SOL::SECONDS_PER_HOUR);
    }
    return MenuContentWithUnit(SOLMenuText::WARP_TIMES_FORMAT, factor);
}

//////////////////////////////////////////////////////////////////////////
// Builds the Controls page (every binding grouped by area, in a scroll box)
UWidget* SOLMenuContent::BuildControlsPage(UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    UVerticalBox* page = MakePage(tree, SOLMenuText::CONTROLS_TITLE);
    UScrollBox* scroll = MenuContentAddScroll(tree, *page);

    // One section per area, in table order (rows of one area are contiguous)
    const TCHAR* currentArea = nullptr;
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        if (currentArea == nullptr || FCString::Strcmp(currentArea, row.Area) != 0)
        {
            currentArea = row.Area;
            MenuContentAddSection(tree, *scroll, currentArea);
        }
        MenuContentAddRow(*scroll, MenuContentMakeRow(tree, FormatBindings(row.Bindings), row.Action,
            KEY_COLUMN_WIDTH, ACCENT_COLOR));
    }
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Builds the Info page (flight, time and combat values generated from the real constants, plus prose)
UWidget* SOLMenuContent::BuildInfoPage(UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    using namespace SOLMenuText;
    UVerticalBox* page = MakePage(tree, INFO_TITLE);
    UScrollBox* scroll = MenuContentAddScroll(tree, *page);
    const FSOLFlightParams flight;

    // Flight, from the speed-cap constants and the default flight parameters the player's ship uses
    MenuContentAddSection(tree, *scroll, INFO_FLIGHT);
    const auto addLine = [&tree, scroll](const FString& label, const FString& value)
    {
        MenuContentAddRow(*scroll, MenuContentMakeRow(tree, label, value, LABEL_COLUMN_WIDTH, DIM_TEXT_COLOR));
    };
    addLine(INFO_SPEED_CAP_RANGE, FString::Format(INFO_RANGE_FORMAT, { SOLInfoText::FormatSpeed(SOL::MIN_SPEED_CAP_MPS),
        SOLInfoText::FormatSpeed(SOL::MAX_SPEED_CAP_MPS) }));
    addLine(INFO_START_SPEED_CAP, SOLInfoText::FormatSpeed(SOL::SHIP_START_SPEED_CAP_MPS));
    addLine(INFO_ASSIST_RESPONSE, MenuContentWithUnit(INFO_SECONDS_FORMAT, flight.AssistTimeConstantS));
    addLine(INFO_NEWTONIAN_ACCEL, FString::Format(INFO_PAIR_FORMAT, {
        MenuContentWithUnit(INFO_ACCEL_FORMAT, flight.NewtonianMainAccelMps2),
        MenuContentWithUnit(INFO_ACCEL_FORMAT, flight.NewtonianStrafeAccelMps2) }));
    addLine(INFO_BOOST, FString::Format(INFO_BOOST_FORMAT, { SOLInfoText::FormatPercentOrValue(
        flight.BoostSpeedMultiplier), SOLInfoText::FormatPercentOrValue(flight.BoostAccelMultiplier) }));
    addLine(INFO_TURN_RATES, FString::Format(INFO_PAIR_FORMAT, {
        MenuContentWithUnit(INFO_DEG_PER_S_FORMAT, FMath::RadiansToDegrees(flight.MaxPitchYawRateRadPerS)),
        MenuContentWithUnit(INFO_DEG_PER_S_FORMAT, FMath::RadiansToDegrees(flight.MaxRollRateRadPerS)) }));
    addLine(INFO_SHIP_RADIUS, MenuContentWithUnit(INFO_METERS_FORMAT, flight.ShipRadiusM));

    // Time and travel: the warp ladder, the jump and surface-lock ranges
    MenuContentAddSection(tree, *scroll, INFO_TIME);
    TArray<FString> warpSteps;
    for (const double factor : FSOLSimClock::WarpFactors())
    {
        warpSteps.Add(FormatWarpFactor(factor));
    }
    addLine(INFO_WARP_STEPS, FString::Join(warpSteps, LIST_SEPARATOR));
    addLine(INFO_JUMP_DURATION, MenuContentWithUnit(INFO_SECONDS_FORMAT, SOL::WARP_DURATION_S));
    addLine(INFO_SURFACE_LOCK_AUTO, MenuContentWithUnit(INFO_KM_FORMAT, SOL::SURFACE_LOCK_AUTO_RANGE_M
        / SOL::METERS_PER_KM));
    addLine(INFO_SURFACE_LOCK_MANUAL, FString::Format(INFO_MANUAL_RANGE_FORMAT, { SOLInfoText::FormatPercentOrValue(
        SOL::SURFACE_LOCK_MANUAL_RANGE_MIN_M / SOL::METERS_PER_KM) }));

    // Combat, through the tested formatter
    MenuContentAddSection(tree, *scroll, INFO_COMBAT);
    for (const SOLInfoText::FInfoLine& line : SOLInfoText::BuildCombatLines(SOL::COMBAT_MUZZLE_SPEED_MPS,
        SOL::COMBAT_BOLT_DAMAGE, SOL::COMBAT_FIRE_RATE_PER_S, SOL::COMBAT_TARGET_SHIELD, SOL::COMBAT_TARGET_HEALTH,
        SOL::COMBAT_TARGET_SHIELD_REGEN_PER_S, SOL::COMBAT_TARGET_SHIELD_REGEN_DELAY_S))
    {
        addLine(line.Label, line.Value);
    }

    // Static prose on how the mechanics behave
    MenuContentAddSection(tree, *scroll, INFO_HOW_IT_WORKS);
    for (const TCHAR* paragraph : INFO_PROSE)
    {
        Cast<UScrollBoxSlot>(scroll->AddChild(MakeText(tree, paragraph, BODY_FONT_SIZE, TEXT_COLOR, false, true)))
            ->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 3.0f));
    }
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Builds the About page (game, studio, engine version, credits)
UWidget* SOLMenuContent::BuildAboutPage(UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    using namespace SOLMenuText;
    UVerticalBox* page = MakePage(tree, ABOUT_TITLE);
    const FMargin gap(0.0f, 0.0f, 0.0f, ROW_GAP * 3.0f);
    page->AddChildToVerticalBox(MakeText(tree, GAME_TITLE, TITLE_FONT_SIZE, TEXT_COLOR, true));
    page->AddChildToVerticalBox(MakeText(tree, STUDIO_NAME, SUBTITLE_FONT_SIZE, ACCENT_COLOR, true))->SetPadding(gap);
    page->AddChildToVerticalBox(MakeText(tree, ABOUT_DESCRIPTION, BODY_FONT_SIZE, TEXT_COLOR, false, true))
        ->SetPadding(gap);
    page->AddChildToVerticalBox(MakeText(tree, FString::Format(ABOUT_ENGINE_FORMAT,
        { FEngineVersion::Current().ToString(EVersionComponent::Patch) }), BODY_FONT_SIZE, TEXT_COLOR))->SetPadding(gap);
    page->AddChildToVerticalBox(MakeText(tree, ABOUT_CREDITS_TITLE, SECTION_FONT_SIZE, ACCENT_COLOR, true));
    page->AddChildToVerticalBox(MakeText(tree, ABOUT_CREDITS, BODY_FONT_SIZE, TEXT_COLOR, false, true))
        ->SetPadding(gap);
    page->AddChildToVerticalBox(MakeText(tree, ABOUT_COPYRIGHT, SMALL_FONT_SIZE, DIM_TEXT_COLOR, false, true));
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Builds the Settings page placeholder (filled by part 8d)
UWidget* SOLMenuContent::BuildSettingsPage(UWidgetTree& tree)
{
    return MakePage(tree, SOLMenuText::SETTINGS_TITLE);
}
