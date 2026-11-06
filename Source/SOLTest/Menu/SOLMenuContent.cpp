/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuContent.h"

#include "Flight/SOLFlight.h"
#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLInfoText.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidStyle.h"
#include "Ship/SOLShipPawn.h"
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
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/EngineVersion.h"
#include "Styling/CoreStyle.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Adds a section heading to a box, with space above it
    void MenuContentAddSection(UWidgetTree& tree, UPanelWidget& box, const FString& heading)
    {
        using namespace SOL::MenuStyle;
        UTextBlock* text = SOLMenuContent::MakeText(tree, heading, SECTION_FONT_SIZE, SOLSolidStyle::ValueColor(),
            true);
        if (UVerticalBox* vertical = Cast<UVerticalBox>(&box))
        {
            vertical->AddChildToVerticalBox(text)->SetPadding(FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f, ROW_GAP));
        }
        else if (UScrollBox* scroll = Cast<UScrollBox>(&box))
        {
            Cast<UScrollBoxSlot>(scroll->AddChild(text))->SetPadding(FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f,
                ROW_GAP));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a two-column row: a fixed-width left cell and a wrapping right cell (outLeft gets the left text block)
    UHorizontalBox* MenuContentMakeRow(UWidgetTree& tree, const FString& left, const FString& right,
        const float leftWidth, const FLinearColor& leftColor, UTextBlock** outLeft = nullptr)
    {
        using namespace SOL::MenuStyle;
        UHorizontalBox* row = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        USizeBox* leftBox = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        leftBox->SetWidthOverride(leftWidth);
        UTextBlock* leftText = SOLMenuContent::MakeText(tree, left, BODY_FONT_SIZE, leftColor, true, true);
        leftBox->SetContent(leftText);
        if (outLeft != nullptr)
        {
            *outLeft = leftText;
        }
        row->AddChildToHorizontalBox(leftBox)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
        UHorizontalBoxSlot* rightSlot = row->AddChildToHorizontalBox(SOLMenuContent::MakeText(tree, right,
            BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false, true));
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
    // Adds a text block to a scroll box with a gap below it
    void MenuContentAddText(UScrollBox& scroll, UTextBlock* text, const float gapBelow)
    {
        Cast<UScrollBoxSlot>(scroll.AddChild(text))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, gapBelow));
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
    // The solid menu font F_SOLMenu (Rajdhani; the engine font when it is missing): Bold or Medium
    UTextBlock* block = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    block->SetFont(SOLSolidStyle::MenuFont(USOLSolidMenuSubsystem::Get(&tree), bBold ? ESOLMenuFontWeight::Bold
        : ESOLMenuFontWeight::Medium, fontSize));
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
    page->AddChildToVerticalBox(MakeText(tree, title, HEADING_FONT_SIZE, SOLSolidStyle::BrightTextColor(), true))
        ->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Returns a key binding cell's text: '|' alternatives, each a rebindable action's live key or a '+' chord of fixed keys
FString SOLMenuContent::FormatBindings(const TCHAR* bindings, const TArray<FString>& liveKeys)
{
    TArray<FString> alternatives;
    FString(bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
    TArray<FString> labels;
    labels.Reserve(alternatives.Num());
    for (const FString& alternative : alternatives)
    {
        // '@Id': the action's current key (an unknown id or a missing key reads as unbound)
        if (!alternative.IsEmpty() && alternative[0] == SOLMenuText::BINDING_ID_PREFIX)
        {
            const int32 binding = SOLKeyBindings::FindById(alternative.RightChop(1));
            labels.Add(SOLKeyBindings::BindingDisplayName(binding, liveKeys));
            continue;
        }
        TArray<FString> chord;
        alternative.ParseIntoArray(chord, SOLMenuText::BINDING_CHORD_DELIMITER);
        labels.Add(SOLBindingLabel::JoinChord(chord));
    }
    return FString::Join(labels, SOLMenuText::BINDING_ALTERNATIVE_SEPARATOR);
}

//////////////////////////////////////////////////////////////////////////
// FormatBindings with the default keys
FString SOLMenuContent::FormatBindings(const TCHAR* bindings)
{
    return FormatBindings(bindings, SOLKeyBindings::DefaultKeys());
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship pawn's live keys, else the applied settings' keys, else the defaults
TArray<FString> SOLMenuContent::ResolveLiveKeys(const UWorld* world)
{
    const APlayerController* playerController = world != nullptr ? world->GetFirstPlayerController() : nullptr;
    const ASOLShipPawn* pawn = playerController != nullptr ? Cast<ASOLShipPawn>(playerController->GetPawn()) : nullptr;
    if (pawn != nullptr && pawn->GetBoundKeyNames().Num() == SOLKeyBindings::BINDING_COUNT)
    {
        return pawn->GetBoundKeyNames();
    }
    const UGameInstance* gameInstance = world != nullptr ? world->GetGameInstance() : nullptr;
    const USOLSettingsSubsystem* settings = gameInstance != nullptr
        ? gameInstance->GetSubsystem<USOLSettingsSubsystem>() : nullptr;
    return settings != nullptr ? settings->GetAppliedKeys() : SOLKeyBindings::DefaultKeys();
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
// Returns a Controls row's action text with its gameplay values filled in
FString SOLMenuContent::FormatControlAction(const TCHAR* action)
{
    FStringFormatNamedArguments arguments;
    arguments.Add(TEXT("DropDistance"), MenuContentWithUnit(SOLMenuText::INFO_METERS_FORMAT,
        SOL::COMBAT_TARGET_DROP_DISTANCE_M));
    arguments.Add(TEXT("SpeedCapNotches"), SOLInfoText::FormatPercentOrValue(SOLFlight::SpeedCapStepsPerDecade));
    return FString::Format(action, arguments);
}

//////////////////////////////////////////////////////////////////////////
// Builds the Controls page (the tab page; every binding grouped by area, one tab per area)
USOLReadOnlyPageWidget* SOLMenuContent::BuildControlsPage(UWidgetTree& tree)
{
    return BuildReadOnlyPage(tree, ESOLReadOnlyPage::Controls);
}

//////////////////////////////////////////////////////////////////////////
// Builds the Info page (the tab page; flight, time, combat and prose)
USOLReadOnlyPageWidget* SOLMenuContent::BuildInfoPage(UWidgetTree& tree)
{
    return BuildReadOnlyPage(tree, ESOLReadOnlyPage::Info);
}

//////////////////////////////////////////////////////////////////////////
// Builds the About page (the tab page; the game and the credits)
USOLReadOnlyPageWidget* SOLMenuContent::BuildAboutPage(UWidgetTree& tree)
{
    return BuildReadOnlyPage(tree, ESOLReadOnlyPage::About);
}

//////////////////////////////////////////////////////////////////////////
// Builds a read-only tab page of a kind
USOLReadOnlyPageWidget* SOLMenuContent::BuildReadOnlyPage(UWidgetTree& tree, const ESOLReadOnlyPage kind)
{
    USOLReadOnlyPageWidget* page = tree.ConstructWidget<USOLReadOnlyPageWidget>(USOLReadOnlyPageWidget::StaticClass());
    page->Setup(kind);
    return page;
}

//////////////////////////////////////////////////////////////////////////
// Fills a read-only page's tabs from today's entries
void SOLMenuContent::FillReadOnlyTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree)
{
    switch (page.GetPageKind())
    {
    case ESOLReadOnlyPage::Controls:
        FillControlsTabs(page, tree);
        break;
    case ESOLReadOnlyPage::Info:
        FillInfoTabs(page, tree);
        break;
    case ESOLReadOnlyPage::About:
        FillAboutTabs(page, tree);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills the Controls tabs: one per area, in table order (rows of one area are contiguous); the key cells start from
// the defaults and are rewritten from the live keys whenever the page is shown
void SOLMenuContent::FillControlsTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    page.KeyCells.Reset(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS));
    const TArray<FString> defaultKeys = SOLKeyBindings::DefaultKeys();
    int32 lastTab = INDEX_NONE;
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)); ++index)
    {
        const SOLMenuText::FControlRow& row = SOLMenuText::CONTROL_ROWS[index];
        const int32 tab = SOLPageTabs::ControlsTabOfRow(index);
        UScrollBox& scroll = page.GetFillScroll(tab);
        if (tab != lastTab)
        {
            lastTab = tab;
            MenuContentAddSection(tree, scroll, row.Area);
        }
        UTextBlock* keyCell = nullptr;
        MenuContentAddRow(scroll, MenuContentMakeRow(tree, FormatBindings(row.Bindings, defaultKeys),
            FormatControlAction(row.Action), KEY_COLUMN_WIDTH, SOLSolidStyle::ValueColor(), &keyCell));
        page.KeyCells.Add(keyCell);
        page.RegisterColumnText(keyCell, tab, KEY_COLUMN_WIDTH);
    }
}

//////////////////////////////////////////////////////////////////////////
// Rewrites the Controls page's key cells from the live keys
void SOLMenuContent::RefreshControlsPage(const TArray<TObjectPtr<UTextBlock>>& keyCells,
    const TArray<FString>& liveKeys)
{
    const int32 count = FMath::Min(keyCells.Num(), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)));
    for (int32 row = 0; row < count; ++row)
    {
        if (keyCells[row] != nullptr)
        {
            keyCells[row]->SetText(FText::FromString(FormatBindings(SOLMenuText::CONTROL_ROWS[row].Bindings,
                liveKeys)));
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills the Info tabs (flight, time and combat values generated from the real constants, plus prose)
void SOLMenuContent::FillInfoTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    using namespace SOLMenuText;
    const FSOLFlightParams flight;
    const TArray<FString> defaultKeys = SOLKeyBindings::DefaultKeys();
    page.KeyTexts.Reset(UE_ARRAY_COUNT(INFO_PROSE) + 1);

    // Flight, from the speed-cap constants and the default flight parameters the player's ship uses; addLine adds a
    // label / value row to the current tab and returns the label's text block
    int32 tab = 0;
    UScrollBox* scroll = &page.GetFillScroll(tab);
    MenuContentAddSection(tree, *scroll, INFO_FLIGHT);
    const auto addLine = [&tree, &page, &scroll, &tab](const FString& label, const FString& value)
    {
        UTextBlock* labelText = nullptr;
        MenuContentAddRow(*scroll, MenuContentMakeRow(tree, label, value, LABEL_COLUMN_WIDTH,
            SOLSolidStyle::TextColor(), &labelText));
        page.RegisterColumnText(labelText, tab, LABEL_COLUMN_WIDTH);
        return labelText;
    };
    addLine(INFO_SPEED_CAP_RANGE, FString::Format(INFO_RANGE_FORMAT, { SOLInfoText::FormatSpeed(SOL::MIN_SPEED_CAP_MPS),
        SOLInfoText::FormatSpeed(SOL::MAX_SPEED_CAP_MPS) }));
    addLine(INFO_START_SPEED_CAP, SOLInfoText::FormatSpeed(SOL::SHIP_START_SPEED_CAP_MPS));
    addLine(INFO_ASSIST_RESPONSE, MenuContentWithUnit(INFO_SECONDS_FORMAT, flight.AssistTimeConstantS));
    // Thrust and the pilot's limits in G (SDD 18), from the envelope and the pilot limits the player's ship uses
    const FSOLThrustEnvelope& envelope = flight.Envelope;
    const FSOLPilotGLimits& pilot = flight.PilotLimits;
    // Returns an acceleration formatted in G
    const auto inG = [](const double accelMps2)
    {
        return MenuContentWithUnit(INFO_G_FORMAT, accelMps2 / SOLNewtonian::STANDARD_GRAVITY_MPS2);
    };
    addLine(INFO_THRUST, FString::Format(INFO_QUAD_FORMAT, { inG(envelope.ForwardAccelMps2),
        inG(envelope.ReverseAccelMps2), inG(envelope.LateralAccelMps2), inG(envelope.VerticalAccelMps2) }));
    addLine(INFO_THRUST_CAP, inG(envelope.MaxTotalAccelMps2));
    addLine(INFO_BOOST, FString::Format(INFO_BOOST_FORMAT, { SOLInfoText::FormatPercentOrValue(
        flight.BoostSpeedMultiplier), SOLInfoText::FormatPercentOrValue(envelope.BoostMultiplier) }));
    addLine(INFO_TURN_RATES, FString::Format(INFO_PAIR_FORMAT, {
        MenuContentWithUnit(INFO_DEG_PER_S_FORMAT, FMath::RadiansToDegrees(envelope.MaxAngularRateRadPerS.Y)),
        MenuContentWithUnit(INFO_DEG_PER_S_FORMAT, FMath::RadiansToDegrees(envelope.MaxAngularRateRadPerS.X)) }));
    addLine(INFO_TURN_ACCEL, FString::Format(INFO_PAIR_FORMAT, {
        MenuContentWithUnit(INFO_RAD_PER_S2_FORMAT, envelope.MaxAngularAccelRadPerS2.Y),
        MenuContentWithUnit(INFO_RAD_PER_S2_FORMAT, envelope.MaxAngularAccelRadPerS2.X) }));
    addLine(INFO_PILOT_G, FString::Format(INFO_PAIR_FORMAT, { MenuContentWithUnit(INFO_G_FORMAT, pilot.SustainedG),
        MenuContentWithUnit(INFO_G_FORMAT, pilot.MaxG) }));
    addLine(INFO_G_TOLERANCE, MenuContentWithUnit(INFO_SECONDS_FORMAT, pilot.TimeToEmptyAtMaxGS));
    addLine(INFO_BLACKOUT, MenuContentWithUnit(INFO_SECONDS_FORMAT, pilot.BlackoutDurationS));
    addLine(INFO_SHIP_RADIUS, MenuContentWithUnit(INFO_METERS_FORMAT, flight.ShipRadiusM));

    // Time and travel: the warp ladder, the jump and surface-lock ranges
    scroll = &page.GetFillScroll(++tab);
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
    page.KeyTexts.Add(addLine(SOLInfoText::FormatSurfaceLockManualLabel(defaultKeys), FString::Format(
        INFO_MANUAL_RANGE_FORMAT, { SOLInfoText::FormatPercentOrValue(SOL::SURFACE_LOCK_MANUAL_RANGE_MIN_M
        / SOL::METERS_PER_KM) })));

    // Combat, through the tested formatter
    scroll = &page.GetFillScroll(++tab);
    MenuContentAddSection(tree, *scroll, INFO_COMBAT);
    for (const SOLInfoText::FInfoLine& line : SOLInfoText::BuildCombatLines(SOL::COMBAT_MUZZLE_SPEED_MPS,
        SOL::COMBAT_BOLT_DAMAGE, SOL::COMBAT_FIRE_RATE_PER_S, SOL::COMBAT_TARGET_SHIELD, SOL::COMBAT_TARGET_HEALTH,
        SOL::COMBAT_TARGET_SHIELD_REGEN_PER_S, SOL::COMBAT_TARGET_SHIELD_REGEN_DELAY_S))
    {
        addLine(line.Label, line.Value);
    }

    // Prose on how the mechanics behave (the keys it names follow rebinds, through RefreshInfoPage)
    scroll = &page.GetFillScroll(++tab);
    MenuContentAddSection(tree, *scroll, INFO_HOW_IT_WORKS);
    for (const FString& paragraph : SOLInfoText::BuildProse(defaultKeys))
    {
        UTextBlock* text = MakeText(tree, paragraph, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false, true);
        Cast<UScrollBoxSlot>(scroll->AddChild(text))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 3.0f));
        page.KeyTexts.Add(text);
    }
}

//////////////////////////////////////////////////////////////////////////
// Rewrites the Info page's key-naming texts from the live keys
void SOLMenuContent::RefreshInfoPage(const TArray<TObjectPtr<UTextBlock>>& keyTexts, const TArray<FString>& liveKeys)
{
    // Same order as FillInfoTabs: the manual surface-lock label, then the prose paragraphs
    const TArray<FString> prose = SOLInfoText::BuildProse(liveKeys);
    for (int32 index = 0; index < keyTexts.Num(); ++index)
    {
        if (keyTexts[index] == nullptr)
        {
            continue;
        }
        const FString text = index == 0 ? SOLInfoText::FormatSurfaceLockManualLabel(liveKeys)
            : (prose.IsValidIndex(index - 1) ? prose[index - 1] : FString());
        keyTexts[index]->SetText(FText::FromString(text));
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills the About tabs: the game (title, studio, description, engine version, copyright) and the credits
void SOLMenuContent::FillAboutTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    using namespace SOLMenuText;
    const float gap = ROW_GAP * 3.0f;
    UScrollBox& game = page.GetFillScroll(0);
    MenuContentAddText(game, MakeText(tree, GAME_TITLE, TITLE_FONT_SIZE, SOLSolidStyle::BrightTextColor(), true), 0.0f);
    MenuContentAddText(game, MakeText(tree, STUDIO_NAME, SUBTITLE_FONT_SIZE, SOLSolidStyle::ValueColor(), true), gap);
    MenuContentAddText(game, MakeText(tree, ABOUT_DESCRIPTION, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false,
        true), gap);
    MenuContentAddText(game, MakeText(tree, FString::Format(ABOUT_ENGINE_FORMAT,
        { FEngineVersion::Current().ToString(EVersionComponent::Patch) }), BODY_FONT_SIZE, SOLSolidStyle::TextColor()),
        gap);
    MenuContentAddText(game, MakeText(tree, ABOUT_COPYRIGHT, SMALL_FONT_SIZE, SOLSolidStyle::DimTextColor(), false,
        true), 0.0f);
    UScrollBox& credits = page.GetFillScroll(1);
    MenuContentAddSection(tree, credits, ABOUT_CREDITS_TITLE);
    MenuContentAddText(credits, MakeText(tree, ABOUT_CREDITS, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false,
        true), gap);
}

//////////////////////////////////////////////////////////////////////////
// Builds the Settings page (a child user widget that builds its own controls)
USOLSettingsPageWidget* SOLMenuContent::BuildSettingsPage(UWidgetTree& tree)
{
    return tree.ConstructWidget<USOLSettingsPageWidget>(USOLSettingsPageWidget::StaticClass());
}
