/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuSmokeBoard.h"

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidSlider.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
    constexpr float BOARD_LABEL_WIDTH = 200.0f;         // The row label column
    constexpr float BOARD_CELL_WIDTH = 300.0f;          // Each of the enabled and disabled columns
    constexpr float BOARD_CELL_GAP = 24.0f;
    constexpr float BOARD_ROW_GAP = 12.0f;
    constexpr float BOARD_PADDING = 40.0f;              // Panel edge to the rows
    constexpr float BOARD_ROW_LABEL_WIDTH = 110.0f;     // The settings row's own label
    constexpr float BOARD_ROW_VALUE_WIDTH = 60.0f;      // ... and its value between the steppers
    constexpr float BOARD_SLIDER_VALUE = 0.6f;
    constexpr float BOARD_TITLE_STEP_WIDTH = 260.0f;    // The hull's title step (a unique material)
    constexpr float BOARD_TITLE_STEP_HEIGHT = 14.0f;

    // Every button's label, variant and whether it is disabled / selected, in ESOLSolidSmokeButton order
    struct FBoardButtonSpec
    {
        const TCHAR* Label;
        ESOLSolidButtonVariant Variant;
        bool bDisabled;
        bool bSelected;
    };
    const FBoardButtonSpec BOARD_BUTTONS[] = {
        { TEXT("Settings"), ESOLSolidButtonVariant::Stack, false, false },
        { TEXT("Settings"), ESOLSolidButtonVariant::Stack, true, false },
        { TEXT("Graphics"), ESOLSolidButtonVariant::Tab, false, true },
        { TEXT("Graphics"), ESOLSolidButtonVariant::Tab, true, true },
        { TEXT("Audio"), ESOLSolidButtonVariant::Tab, false, false },
        { TEXT("Rebind"), ESOLSolidButtonVariant::Small, false, false },
        { TEXT("Rebind"), ESOLSolidButtonVariant::Small, true, false },
        { TEXT("<"), ESOLSolidButtonVariant::Small, false, false },
        { TEXT(">"), ESOLSolidButtonVariant::Small, false, false },
        { TEXT("<"), ESOLSolidButtonVariant::Small, true, false },
        { TEXT(">"), ESOLSolidButtonVariant::Small, true, false },
    };
    static_assert(UE_ARRAY_COUNT(BOARD_BUTTONS) == static_cast<int32>(ESOLSolidSmokeButton::Count),
        "one spec per board button");

    //////////////////////////////////////////////////////////////////////////
    // Wraps a widget in a size box (0 leaves that axis free)
    USizeBox* BoardSized(UWidgetTree& tree, UWidget* content, const float width, const float height)
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
}

//////////////////////////////////////////////////////////////////////////
// Returns one of the board's buttons, or null
USOLMenuButton* USOLSolidMenuSmokeBoard::GetButton(const ESOLSolidSmokeButton button) const
{
    const int32 index = static_cast<int32>(button);
    return Buttons.IsValidIndex(index) ? Buttons[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Gives the stack and tab buttons hover traces from their right ends to the panel's right edge (board coordinates)
void USOLSolidMenuSmokeBoard::BindHoverTraces()
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = Panel != nullptr ? Panel->GetTraceLayer() : nullptr;
    if (!layer.IsValid())
    {
        return;
    }
    const float right = BOARD_PADDING + BOARD_LABEL_WIDTH + BOARD_CELL_WIDTH;
    const float end = BOARD_PADDING + BOARD_LABEL_WIDTH + 2.0f * BOARD_CELL_WIDTH + 2.0f * BOARD_CELL_GAP;
    const ESOLSolidSmokeButton traced[] = { ESOLSolidSmokeButton::StackOn, ESOLSolidSmokeButton::TabA,
        ESOLSolidSmokeButton::TabB };
    const int32 rows[] = { 0, 1, 2 };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(traced)); ++i)
    {
        USOLMenuButton* button = GetButton(traced[i]);
        if (button == nullptr)
        {
            continue;
        }
        const float y = BOARD_PADDING + rows[i] * (SOL::MenuStyle::BUTTON_HEIGHT + BOARD_ROW_GAP)
            + 0.5f * SOL::MenuStyle::BUTTON_HEIGHT;
        FSOLSolidTrace trace;
        trace.Points[0] = FVector2f(right, y);
        trace.Points[1] = FVector2f(right + BOARD_CELL_GAP, y);
        trace.Points[2] = FVector2f(right + 2.0f * BOARD_CELL_GAP, y + BOARD_ROW_GAP);
        trace.Points[3] = FVector2f(end, y + BOARD_ROW_GAP);
        trace.Count = 4;
        trace.bRingTerminal = false;
        button->SetHoverTrace(layer, trace);
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the board: the hull panel, then one row per control type with its enabled and disabled instances
void USOLSolidMenuSmokeBoard::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(this);

    UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    root->SetBrushColor(FLinearColor::Transparent);
    root->SetHorizontalAlignment(HAlign_Center);
    root->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = root;

    // The hull panel with a title step (its own material), rivets, vents and one static trace along its lower edge
    Panel = WidgetTree->ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass());
    FSOLSolidHull hull;
    hull.ChamferTL = SOL::SolidMenu::PANEL_CHAMFER_TL;
    hull.ChamferTR = SOL::SolidMenu::PANEL_CHAMFER_TR;
    hull.ChamferBR = SOL::SolidMenu::PANEL_CHAMFER_BR;
    hull.ChamferBL = SOL::SolidMenu::PANEL_CHAMFER_BL;
    hull.TitleStepWidth = BOARD_TITLE_STEP_WIDTH;
    hull.TitleStepHeight = BOARD_TITLE_STEP_HEIGHT;
    Panel->SetHull(ESOLSolidStyle::Panel, hull);
    Panel->SetContentPadding(FMargin(BOARD_PADDING));
    Panel->SetDecor(true, true);
    root->SetContent(Panel);
    UVerticalBox* rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    Panel->AddChild(rows);

    // Adds a row: its label, then the enabled and disabled cells
    const auto addRow = [this, rows](const TCHAR* label, UWidget* enabled, UWidget* disabled, const float height)
    {
        UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        row->AddChildToHorizontalBox(BoardSized(*WidgetTree, SOLMenuContent::MakeText(*WidgetTree, label,
            BODY_FONT_SIZE, SOLSolidStyle::TextColor()), BOARD_LABEL_WIDTH, 0.0f))->SetVerticalAlignment(VAlign_Center);
        UWidget* cells[] = { enabled, disabled };
        for (UWidget* cell : cells)
        {
            UWidget* content = cell != nullptr ? cell : WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
            row->AddChildToHorizontalBox(BoardSized(*WidgetTree, content, BOARD_CELL_WIDTH, height))->SetPadding(
                FMargin(0.0f, 0.0f, BOARD_CELL_GAP, 0.0f));
        }
        rows->AddChildToVerticalBox(row)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, BOARD_ROW_GAP));
    };

    // The buttons, set up as their screens set them up
    Buttons.Reset();
    for (int32 index = 0; index < static_cast<int32>(ESOLSolidSmokeButton::Count); ++index)
    {
        const FBoardButtonSpec& spec = BOARD_BUTTONS[index];
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        const int32 fontSize = spec.Variant == ESOLSolidButtonVariant::Stack ? BUTTON_FONT_SIZE
            : spec.Variant == ESOLSolidButtonVariant::Tab ? TAB_FONT_SIZE : BODY_FONT_SIZE;
        button->Setup(*WidgetTree, spec.Label, index, fontSize, spec.Variant);
        button->SetSelected(spec.bSelected);
        button->SetSolidDisabled(spec.bDisabled);
        Buttons.Add(button);
    }

    // Makes a settings row: its label, '<', the value, '>'
    const auto makeSettingsRow = [this](USOLMenuButton* prev, USOLMenuButton* next, const bool bDisabled)
    {
        UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        const FLinearColor labelColor = bDisabled ? SOLSolidStyle::Linear(SOL::SolidMenu::DISABLED_TEXT_COLOR)
            : SOLSolidStyle::TextColor();
        const FLinearColor valueColor = bDisabled ? labelColor : SOLSolidStyle::ValueColor();
        row->AddChildToHorizontalBox(BoardSized(*WidgetTree, SOLMenuContent::MakeText(*WidgetTree, TEXT("Quality"),
            BODY_FONT_SIZE, labelColor), BOARD_ROW_LABEL_WIDTH, 0.0f))->SetVerticalAlignment(VAlign_Center);
        row->AddChildToHorizontalBox(BoardSized(*WidgetTree, prev, SETTINGS_STEP_BUTTON_WIDTH, 0.0f));
        UTextBlock* value = SOLMenuContent::MakeText(*WidgetTree, TEXT("High"), BODY_FONT_SIZE, valueColor);
        value->SetJustification(ETextJustify::Center);
        row->AddChildToHorizontalBox(BoardSized(*WidgetTree, value, BOARD_ROW_VALUE_WIDTH, 0.0f))
            ->SetVerticalAlignment(VAlign_Center);
        row->AddChildToHorizontalBox(BoardSized(*WidgetTree, next, SETTINGS_STEP_BUTTON_WIDTH, 0.0f));
        return row;
    };

    // The slider and toggle pairs; the disabled ones are Slate-disabled with the disabled styles
    Sliders.Reset();
    Toggles.Reset();
    for (int32 disabled = 0; disabled < 2; ++disabled)
    {
        USOLSolidSlider* slider = WidgetTree->ConstructWidget<USOLSolidSlider>(USOLSolidSlider::StaticClass());
        slider->SetSolidStyle(solid);
        slider->SetValue(BOARD_SLIDER_VALUE);
        slider->SetIsEnabled(disabled == 0);
        Sliders.Add(slider);
        UCheckBox* toggle = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
        toggle->SetWidgetStyle(SOLSolidStyle::MakeCheckBoxStyle(solid, disabled != 0));
        toggle->SetIsChecked(true);
        toggle->SetIsEnabled(disabled == 0);
        Toggles.Add(toggle);
    }

    using EButton = ESOLSolidSmokeButton;
    addRow(TEXT("Stack button"), GetButton(EButton::StackOn), GetButton(EButton::StackOff), BUTTON_HEIGHT);
    addRow(TEXT("Tab (selected)"), GetButton(EButton::TabA), GetButton(EButton::TabOff), TAB_HEIGHT);
    addRow(TEXT("Tab"), GetButton(EButton::TabB), nullptr, TAB_HEIGHT);
    addRow(TEXT("Small button"), GetButton(EButton::SmallOn), GetButton(EButton::SmallOff), SETTINGS_ROW_HEIGHT);
    addRow(TEXT("Settings row"), makeSettingsRow(GetButton(EButton::RowOnPrev), GetButton(EButton::RowOnNext), false),
        makeSettingsRow(GetButton(EButton::RowOffPrev), GetButton(EButton::RowOffNext), true), SETTINGS_ROW_HEIGHT);
    addRow(TEXT("Slider"), Sliders[0], Sliders[1], SETTINGS_ROW_HEIGHT);
    UHorizontalBox* toggleCells[2];
    for (int32 i = 0; i < 2; ++i)
    {
        toggleCells[i] = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        toggleCells[i]->AddChildToHorizontalBox(Toggles[i])->SetVerticalAlignment(VAlign_Center);
    }
    addRow(TEXT("Toggle (on)"), toggleCells[0], toggleCells[1], SETTINGS_ROW_HEIGHT);

    // The static trace under the rows
    const float traceY = BOARD_PADDING + 7.0f * BOARD_ROW_GAP + 3.0f * BUTTON_HEIGHT + 4.0f * SETTINGS_ROW_HEIGHT;
    FSOLSolidTrace trace;
    trace.Points[0] = FVector2f(BOARD_PADDING, traceY);
    trace.Points[1] = FVector2f(BOARD_PADDING + BOARD_LABEL_WIDTH, traceY);
    trace.Points[2] = FVector2f(BOARD_PADDING + BOARD_LABEL_WIDTH + BOARD_ROW_GAP, traceY + BOARD_ROW_GAP);
    trace.Points[3] = FVector2f(BOARD_PADDING + BOARD_LABEL_WIDTH + 2.0f * BOARD_CELL_WIDTH, traceY + BOARD_ROW_GAP);
    trace.Count = 4;
    const FSOLSolidTrace traces[] = { trace };
    Panel->SetTraces(traces);
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the buttons' highlights and the panel's trace look (the pulse on real time)
void USOLSolidMenuSmokeBoard::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    for (USOLMenuButton* button : Buttons)
    {
        button->RefreshHighlightAt(frame.NowS, frame.Strength);
    }
    if (Panel != nullptr)
    {
        Panel->SetTraceLook(1.0f, SOLSolidMenu::IdlePulse(frame.NowS, frame.Strength), frame.Strength);
    }
}
