/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLDemoPageWidget.h"

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"

namespace
{
    using SOLNpcScale::EDemoOption;

    // Marks the end of a section's row list
    constexpr int32 DEMO_SECTION_END = -1;

    // Stepper buttons per row ('<' and '>')
    constexpr int32 DEMO_STEP_BUTTONS_PER_ROW = 2;

    // Returns an option row's index
    constexpr int32 DemoOptionRow(const EDemoOption option)
    {
        return static_cast<int32>(option);
    }

    // Returns an extra row's index (SOLMenuState::DemoExtraRow, usable in constant tables)
    constexpr int32 DemoExtraRowIndex(const SOLMenuState::EDemoExtraRow extra)
    {
        return static_cast<int32>(EDemoOption::Count) + static_cast<int32>(extra);
    }

    // Rows of each section (SOLMenuText::DEMO_SECTION_TITLES order), each list ended by DEMO_SECTION_END
    const int32 DEMO_SECTION_ROWS[] =
    {
        DemoOptionRow(EDemoOption::NpcCount), DemoOptionRow(EDemoOption::LaneFraction),
        DemoExtraRowIndex(SOLMenuState::EDemoExtraRow::Seed), DEMO_SECTION_END,
        DemoOptionRow(EDemoOption::BattleShipCount), DemoOptionRow(EDemoOption::BattleCount),
        DemoOptionRow(EDemoOption::BattleRadiusM), DemoOptionRow(EDemoOption::BattleMinDistanceM),
        DemoOptionRow(EDemoOption::BattleMaxDistanceM), DEMO_SECTION_END,
        DemoOptionRow(EDemoOption::FullAiRadiusM), DemoOptionRow(EDemoOption::FullAiBudget),
        DemoOptionRow(EDemoOption::MidAiRadiusM), DemoOptionRow(EDemoOption::MidAiBudget),
        DemoOptionRow(EDemoOption::AiHysteresis), DEMO_SECTION_END,
        DemoOptionRow(EDemoOption::MeshRadiusM), DemoOptionRow(EDemoOption::MeshBudget),
        DemoOptionRow(EDemoOption::InstancedRadiusM), DemoOptionRow(EDemoOption::InstancedBudget),
        DemoOptionRow(EDemoOption::RenderHysteresis), DEMO_SECTION_END,
        DemoOptionRow(EDemoOption::NpcCombat), DemoExtraRowIndex(SOLMenuState::EDemoExtraRow::CombatModel),
        DEMO_SECTION_END,
        DemoOptionRow(EDemoOption::ShowStats), DemoExtraRowIndex(SOLMenuState::EDemoExtraRow::Ramp),
        DemoOptionRow(EDemoOption::RampSettleS), DemoOptionRow(EDemoOption::RampMeasureS), DEMO_SECTION_END,
    };

    //////////////////////////////////////////////////////////////////////////
    // Wraps a widget in a box of a fixed width (0 = free) and height (0 = free)
    USizeBox* DemoSized(UWidgetTree& tree, UWidget* content, const float width, const float height)
    {
        USizeBox* box = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        if (width > 0.0f)
        {
            box->SetWidthOverride(width);
        }
        if (height > 0.0f)
        {
            box->SetHeightOverride(height);
        }
        box->SetContent(content);
        return box;
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a widget to a row, filling the remaining width when bFill, vertically centred
    UHorizontalBoxSlot* DemoAddToRow(UHorizontalBox& row, UWidget* widget, const bool bFill)
    {
        UHorizontalBoxSlot* rowSlot = row.AddChildToHorizontalBox(widget);
        rowSlot->SetVerticalAlignment(VAlign_Center);
        if (bFill)
        {
            rowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
        return rowSlot;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint)
void USOLDemoPageWidget::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    Menu = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLMenuSubsystem>() : nullptr;
    UVerticalBox* page = SOLMenuContent::MakePage(*WidgetTree, SOLMenuText::DEMO_TITLE);
    WidgetTree->RootWidget = page;
    HintText = SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::DEMO_HINT, SMALL_FONT_SIZE, DIM_TEXT_COLOR, false,
        true);
    page->AddChildToVerticalBox(HintText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    const int32 rowCount = SOLMenuState::DemoRowCount();
    const int32 actionBase = rowCount * DEMO_STEP_BUTTONS_PER_ROW;
    Buttons.Reset();

    // Creates a preset or footer button in a fixed-size box
    const auto makeAction = [this, actionBase](const TCHAR* label, const EAction action)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, label, actionBase + static_cast<int32>(action), BODY_FONT_SIZE);
        button->OnMenuClicked.BindUObject(this, &USOLDemoPageWidget::HandleActionClicked);
        Buttons.Add(button);
        return button;
    };

    // Presets row
    UHorizontalBox* presets = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    page->AddChildToVerticalBox(presets)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    USizeBox* presetLabel = DemoSized(*WidgetTree, SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::DEMO_PRESETS,
        BODY_FONT_SIZE, TEXT_COLOR), SETTINGS_LABEL_WIDTH, 0.0f);
    DemoAddToRow(*presets, presetLabel, false)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
    static_assert(UE_ARRAY_COUNT(SOLMenuText::DEMO_PRESET_LABELS)
        == static_cast<int32>(SOLMenuState::EDemoPreset::Count), "one label per preset");
    for (int32 preset = 0; preset < static_cast<int32>(SOLMenuState::EDemoPreset::Count); ++preset)
    {
        USOLMenuButton* button = makeAction(SOLMenuText::DEMO_PRESET_LABELS[preset],
            static_cast<EAction>(static_cast<int32>(EAction::PresetShowcase) + preset));
        DemoAddToRow(*presets, DemoSized(*WidgetTree, button, 0.0f, SETTINGS_ROW_HEIGHT), true)->SetPadding(
            FMargin(preset == 0 ? 0.0f : BUTTON_GAP, 0.0f, 0.0f, 0.0f));
    }

    // Option rows by section: label, '<', value, '>'
    UScrollBox* scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
    page->AddChildToVerticalBox(scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    static_assert(UE_ARRAY_COUNT(SOLMenuText::DEMO_ROW_LABELS)
        == static_cast<int32>(EDemoOption::Count) + static_cast<int32>(SOLMenuState::EDemoExtraRow::Count),
        "one label per Demo row");
    ValueTexts.SetNum(rowCount);
    int32 section = 0;
    bool bSectionStart = true;
    for (const int32 row : DEMO_SECTION_ROWS)
    {
        if (row == DEMO_SECTION_END)
        {
            ++section;
            bSectionStart = true;
            continue;
        }
        if (bSectionStart && section < UE_ARRAY_COUNT(SOLMenuText::DEMO_SECTION_TITLES))
        {
            Cast<UScrollBoxSlot>(scroll->AddChild(SOLMenuContent::MakeText(*WidgetTree,
                SOLMenuText::DEMO_SECTION_TITLES[section], SECTION_FONT_SIZE, ACCENT_COLOR, true)))->SetPadding(
                FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f, ROW_GAP * 2.0f));
            bSectionStart = false;
        }
        UHorizontalBox* rowBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        DemoAddToRow(*rowBox, DemoSized(*WidgetTree, SOLMenuContent::MakeText(*WidgetTree,
            SOLMenuText::DEMO_ROW_LABELS[row], BODY_FONT_SIZE, TEXT_COLOR), SETTINGS_LABEL_WIDTH, 0.0f), false)
            ->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
        const TCHAR* labels[] = { SOLMenuText::SETTINGS_STEP_BACK, SOLMenuText::SETTINGS_STEP_FORWARD };
        for (int32 side = 0; side < UE_ARRAY_COUNT(labels); ++side)
        {
            USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
            button->Setup(*WidgetTree, labels[side], row * DEMO_STEP_BUTTONS_PER_ROW + side, BODY_FONT_SIZE);
            button->OnMenuClicked.BindUObject(this, &USOLDemoPageWidget::HandleStepClicked);
            Buttons.Add(button);
            if (side == 1)
            {
                UTextBlock* value = SOLMenuContent::MakeText(*WidgetTree, FString(), BODY_FONT_SIZE, ACCENT_COLOR);
                value->SetJustification(ETextJustify::Center);
                ValueTexts[row] = value;
                DemoAddToRow(*rowBox, value, true);
            }
            DemoAddToRow(*rowBox, DemoSized(*WidgetTree, button, SETTINGS_STEP_BUTTON_WIDTH, 0.0f), false);
        }
        Cast<UScrollBoxSlot>(scroll->AddChild(DemoSized(*WidgetTree, rowBox, 0.0f, SETTINGS_ROW_HEIGHT)))->SetPadding(
            FMargin(0.0f, 0.0f, BUTTON_GAP, ROW_GAP));
    }

    // Warnings, then the footer: Reset, Stop NPCs (in flight only) and Start demo / Apply
    WarningsText = SOLMenuContent::MakeText(*WidgetTree, FString(), SMALL_FONT_SIZE, WARNING_COLOR, false, true);
    page->AddChildToVerticalBox(WarningsText)->SetPadding(FMargin(0.0f, ROW_GAP * 2.0f, 0.0f, ROW_GAP * 2.0f));
    UHorizontalBox* footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    page->AddChildToVerticalBox(footer);
    DemoAddToRow(*footer, DemoSized(*WidgetTree, makeAction(SOLMenuText::DEMO_RESET, EAction::Reset),
        DEMO_ACTION_WIDTH, SETTINGS_ROW_HEIGHT), false);
    DemoAddToRow(*footer, WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()), true);
    StopButton = makeAction(SOLMenuText::DEMO_STOP, EAction::Stop);
    DemoAddToRow(*footer, DemoSized(*WidgetTree, StopButton, DEMO_ACTION_WIDTH, SETTINGS_ROW_HEIGHT), false)
        ->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
    StartButton = makeAction(SOLMenuText::DEMO_START, EAction::Start);
    DemoAddToRow(*footer, DemoSized(*WidgetTree, StartButton, DEMO_ACTION_WIDTH, SETTINGS_ROW_HEIGHT), false);
    SetInFlight(false);
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Sets the page's mode: in flight (Stop NPCs and Apply) or not (Start demo)
void USOLDemoPageWidget::SetInFlight(const bool bInFlight)
{
    mIsInFlight = bInFlight;
    if (StopButton != nullptr)
    {
        StopButton->SetVisibility(bInFlight ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
    }
    if (StartButton != nullptr)
    {
        StartButton->SetLabel(bInFlight ? SOLMenuText::DEMO_APPLY : SOLMenuText::DEMO_START);
    }
    if (HintText != nullptr)
    {
        HintText->SetText(FText::FromString(bInFlight ? SOLMenuText::DEMO_PAUSE_HINT : SOLMenuText::DEMO_HINT));
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-reads every value from the menu subsystem (the page was shown)
void USOLDemoPageWidget::RefreshFromMenu()
{
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Steps a row back or forward as its buttons would
void USOLDemoPageWidget::Step(const int32 row, const int32 direction)
{
    if (Menu != nullptr)
    {
        SetPage(SOLMenuState::StepDemoRow(Menu->GetDemoPage(), row, direction));
    }
}

//////////////////////////////////////////////////////////////////////////
// Applies a preset as its button would
void USOLDemoPageWidget::ApplyPreset(const SOLMenuState::EDemoPreset preset)
{
    if (Menu != nullptr)
    {
        SetPage(SOLMenuState::ApplyDemoPreset(Menu->GetDemoPage(), preset));
    }
}

//////////////////////////////////////////////////////////////////////////
// Puts every option back to its default (the seed included)
void USOLDemoPageWidget::Reset()
{
    SetPage(SOLMenuState::DefaultDemoPage());
}

//////////////////////////////////////////////////////////////////////////
// Starts the demo (Start demo / Apply button)
void USOLDemoPageWidget::Start()
{
    if (Menu != nullptr)
    {
        Menu->RequestStartDemo();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a row's value text (verification)
FString USOLDemoPageWidget::GetValueText(const int32 row) const
{
    return ValueTexts.IsValidIndex(row) && ValueTexts[row] != nullptr ? ValueTexts[row]->GetText().ToString()
        : FString();
}

//////////////////////////////////////////////////////////////////////////
// Returns the warnings text (verification)
FString USOLDemoPageWidget::GetWarningsText() const
{
    return WarningsText != nullptr ? WarningsText->GetText().ToString() : FString();
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the button highlights (focus follows the keyboard and the mouse)
void USOLDemoPageWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    for (USOLMenuButton* button : Buttons)
    {
        button->RefreshHighlight();
    }
}

//////////////////////////////////////////////////////////////////////////
// Rewrites every value text and the warnings from the menu subsystem's Demo page
void USOLDemoPageWidget::RefreshTexts()
{
    if (Menu == nullptr)
    {
        return;
    }
    const SOLMenuState::FDemoPage& page = Menu->GetDemoPage();
    for (int32 row = 0; row < ValueTexts.Num(); ++row)
    {
        if (ValueTexts[row] != nullptr)
        {
            ValueTexts[row]->SetText(FText::FromString(SOLMenuState::FormatDemoValue(page, row)));
        }
    }

    // Warnings only: the corrections (radii, battle ships, distances) are already applied by every edit
    const SOLNpcScale::EOptionIssue issues = SOLNpcScale::SanitizeOptions(page.Options).Issues;
    TArray<FString> warnings;
    if (EnumHasAnyFlags(issues, SOLNpcScale::EOptionIssue::BattleShipsOverFullBudget))
    {
        warnings.Add(SOLMenuText::DEMO_WARNING_FULL_BUDGET);
    }
    if (EnumHasAnyFlags(issues, SOLNpcScale::EOptionIssue::BattlesBeyondFullAi))
    {
        warnings.Add(SOLMenuText::DEMO_WARNING_BEYOND_FULL);
    }
    if (WarningsText != nullptr)
    {
        WarningsText->SetText(FText::FromString(warnings.IsEmpty() ? FString(SOLMenuText::DEMO_NO_WARNINGS)
            : FString::Join(warnings, TEXT(" "))));
        WarningsText->SetColorAndOpacity(FSlateColor(warnings.IsEmpty() ? SOL::MenuStyle::DIM_TEXT_COLOR
            : SOL::MenuStyle::WARNING_COLOR));
    }
}

//////////////////////////////////////////////////////////////////////////
// Stores an edited page in the menu subsystem and refreshes the texts
void USOLDemoPageWidget::SetPage(const SOLMenuState::FDemoPage& page)
{
    if (Menu != nullptr)
    {
        Menu->SetDemoPage(page);
        RefreshTexts();
    }
}

//////////////////////////////////////////////////////////////////////////
// A stepper button was clicked (index = row * 2 + 0 back / 1 forward)
void USOLDemoPageWidget::HandleStepClicked(const int32 index)
{
    Step(index / DEMO_STEP_BUTTONS_PER_ROW, index % DEMO_STEP_BUTTONS_PER_ROW == 0 ? -1 : 1);
}

//////////////////////////////////////////////////////////////////////////
// A preset or footer button was clicked (index = stepper button count + EAction)
void USOLDemoPageWidget::HandleActionClicked(const int32 index)
{
    const EAction action = static_cast<EAction>(index - SOLMenuState::DemoRowCount() * DEMO_STEP_BUTTONS_PER_ROW);
    switch (action)
    {
    case EAction::PresetShowcase:
        ApplyPreset(SOLMenuState::EDemoPreset::Showcase);
        break;
    case EAction::PresetBenchmark:
        ApplyPreset(SOLMenuState::EDemoPreset::Benchmark);
        break;
    case EAction::PresetRamp:
        ApplyPreset(SOLMenuState::EDemoPreset::Ramp);
        break;
    case EAction::Reset:
        Reset();
        break;
    case EAction::Stop:
        if (Menu != nullptr && mIsInFlight)
        {
            Menu->RequestStopNpcs();
        }
        break;
    case EAction::Start:
        Start();
        break;
    default:
        break;
    }
}
