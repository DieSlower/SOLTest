/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLSpeedPanelWidget.h"

#include "Ship/SOLShipPawn.h"
#include "SOLTest.h"
#include "UI/SOLHudFormat.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Fonts (Slate's default font family) and sizes
    const TCHAR* const PANEL_FONT_REGULAR = TEXT("Regular");
    const TCHAR* const PANEL_FONT_MONO = TEXT("Mono");
    constexpr int32 PANEL_TITLE_FONT_SIZE = 18;
    constexpr int32 PANEL_DIGIT_FONT_SIZE = 34;
    constexpr int32 PANEL_TEXT_FONT_SIZE = 13;
    constexpr int32 PANEL_ROW_FONT_SIZE = 14;

    // Layout
    constexpr float PANEL_PADDING = 20.0f;
    constexpr float PANEL_SECTION_GAP = 10.0f;
    constexpr float PANEL_LIST_WIDTH = 440.0f;
    constexpr float PANEL_LIST_HEIGHT = 230.0f;

    // Colors
    const FLinearColor PANEL_SCREEN_DIM(0.0f, 0.0f, 0.0f, 0.35f);
    const FLinearColor PANEL_BACKGROUND(0.02f, 0.06f, 0.04f, 0.92f);
    const FLinearColor PANEL_TEXT(0.85f, 1.0f, 0.88f, 1.0f);
    const FLinearColor PANEL_DIM_TEXT(0.4f, 0.5f, 0.44f, 1.0f);
    const FLinearColor PANEL_HIGHLIGHT(1.0f, 0.85f, 0.3f, 1.0f);

    // Hundredths per unit: the odometer's least significant place is 10^MinDigit
    constexpr double PANEL_HUNDREDTHS_PER_UNIT = 100.0;

    //////////////////////////////////////////////////////////////////////////
    // Appends a speed with its auto-picked unit (m/s, km/s or c)
    void SpeedPanelAppendSpeed(FString& text, const double speedMps)
    {
        const ESOLSpeedUnit unit = SOLHudFormat::PickSpeedUnit(speedMps);
        const double value = SOLHudFormat::ToUnitValue(speedMps, unit);
        switch (unit)
        {
        case ESOLSpeedUnit::KilometersPerSecond:
            text.Appendf(TEXT("%.2f km/s"), value);
            break;
        case ESOLSpeedUnit::LightSpeed:
            text.Appendf(TEXT("%.4f c"), value);
            break;
        case ESOLSpeedUnit::MetersPerSecond:
        default:
            text.Appendf(TEXT("%.1f m/s"), value);
            break;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit field's label
    const TCHAR* SpeedPanelUnitLabel(const ESOLSpeedUnit unit)
    {
        switch (unit)
        {
        case ESOLSpeedUnit::KilometersPerSecond:
            return TEXT("km/s");
        case ESOLSpeedUnit::LightSpeed:
            return TEXT("c");
        case ESOLSpeedUnit::MetersPerSecond:
        default:
            return TEXT("m/s");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a text block in the tree with a font face, size and color
    UTextBlock* SpeedPanelMakeText(UWidgetTree* tree, const TCHAR* text, const TCHAR* face, const int32 size,
        const FLinearColor& color)
    {
        UTextBlock* block = tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        block->SetFont(FCoreStyle::GetDefaultFontStyle(face, size));
        block->SetColorAndOpacity(FSlateColor(color));
        block->SetText(FText::FromString(text));
        return block;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint) and makes the panel focusable
void USOLSpeedPanelWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    BodyRegistry = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLBodyRegistrySubsystem>() : nullptr;

    // Full-screen dimmer (it also catches the wheel anywhere on screen) holding a centered panel
    UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    root->SetBrushColor(PANEL_SCREEN_DIM);
    root->SetHorizontalAlignment(HAlign_Center);
    root->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = root;
    UBorder* panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    panel->SetBrushColor(PANEL_BACKGROUND);
    panel->SetPadding(FMargin(PANEL_PADDING));
    root->SetContent(panel);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    panel->SetContent(column);
    column->AddChildToVerticalBox(SpeedPanelMakeText(WidgetTree, TEXT("SPEED CAP  (F3 / Enter close, Esc close)"),
        PANEL_FONT_REGULAR, PANEL_TITLE_FONT_SIZE, PANEL_TEXT));

    // Steppers side by side: odometer digits (10^MaxDigit .. 10^MinDigit, point before the tenths) and the unit
    UHorizontalBox* steppers = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    column->AddChildToVerticalBox(steppers)->SetPadding(FMargin(0.0f, PANEL_SECTION_GAP));
    DigitTexts.Reset();
    for (int32 digit = SOLSpeedStepper::MaxDigit; digit >= SOLSpeedStepper::MinDigit; --digit)
    {
        if (digit == -1)
        {
            steppers->AddChildToHorizontalBox(SpeedPanelMakeText(WidgetTree, TEXT("."), PANEL_FONT_MONO,
                PANEL_DIGIT_FONT_SIZE, PANEL_TEXT));
        }
        UTextBlock* digitText = SpeedPanelMakeText(WidgetTree, TEXT("0"), PANEL_FONT_MONO, PANEL_DIGIT_FONT_SIZE,
            PANEL_TEXT);
        steppers->AddChildToHorizontalBox(digitText);
        DigitTexts.Add(digitText);
    }
    UnitText = SpeedPanelMakeText(WidgetTree, TEXT("km/s"), PANEL_FONT_MONO, PANEL_DIGIT_FONT_SIZE, PANEL_TEXT);
    steppers->AddChildToHorizontalBox(UnitText)->SetPadding(FMargin(PANEL_SECTION_GAP * 2.0f, 0.0f, 0.0f, 0.0f));
    CapText = SpeedPanelMakeText(WidgetTree, TEXT(""), PANEL_FONT_REGULAR, PANEL_TEXT_FONT_SIZE, PANEL_TEXT);
    column->AddChildToVerticalBox(CapText);
    column->AddChildToVerticalBox(SpeedPanelMakeText(WidgetTree, TEXT("Up/Down or wheel: step digit (unit field: "
        "cycle)   Left/Right: move highlight   U: cycle unit   Tab: body list"), PANEL_FONT_REGULAR,
        PANEL_TEXT_FONT_SIZE, PANEL_DIM_TEXT));

    // Scrollable body list: one row per registry body with its orbital speed
    column->AddChildToVerticalBox(SpeedPanelMakeText(WidgetTree, TEXT("BODY ORBITAL SPEEDS  (Tab, Up/Down, Enter = "
        "set cap)"), PANEL_FONT_REGULAR, PANEL_TEXT_FONT_SIZE, PANEL_TEXT))->SetPadding(
        FMargin(0.0f, PANEL_SECTION_GAP, 0.0f, 0.0f));
    USizeBox* listBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    listBox->SetWidthOverride(PANEL_LIST_WIDTH);
    listBox->SetHeightOverride(PANEL_LIST_HEIGHT);
    column->AddChildToVerticalBox(listBox);
    BodyList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BodyList"));
    listBox->SetContent(BodyList);
    BodyRows.Reset();
    const int32 bodyCount = BodyRegistry != nullptr ? BodyRegistry->GetRegistry().Num() : 0;
    mBodySpeedsMps.SetNumZeroed(bodyCount);
    for (int32 index = 0; index < bodyCount; ++index)
    {
        UTextBlock* row = SpeedPanelMakeText(WidgetTree, TEXT(""), PANEL_FONT_MONO, PANEL_ROW_FONT_SIZE, PANEL_TEXT);
        BodyList->AddChild(row);
        BodyRows.Add(row);
    }
}

//////////////////////////////////////////////////////////////////////////
// Seeds the steppers from the current cap, refreshes the body speeds and remembers the pawn to apply the cap to
void USOLSpeedPanelWidget::Open(ASOLShipPawn* pawn, const double currentCapMps)
{
    mPawn = pawn;
    mStepper = SOLSpeedStepper::FromSpeedMps(currentCapMps);
    mIsUnitHighlighted = false;
    mIsListFocused = false;
    RefreshBodySpeeds();
    RefreshStepperText();
    RefreshListText();
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's speed as listed when the panel was opened (m/s, relative to its parent), or 0
double USOLSpeedPanelWidget::GetListedBodySpeedMps(const int32 bodyIndex) const
{
    return mBodySpeedsMps.IsValidIndex(bodyIndex) ? mBodySpeedsMps[bodyIndex] : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Handles one key press; returns true when the panel used it (verification scripts call it like Slate does)
bool USOLSpeedPanelWidget::HandlePanelKey(const FKey& key)
{
    if (key == EKeys::F3 || key == EKeys::Escape || (key == EKeys::Enter && !mIsListFocused))
    {
        RequestClose();
        return true;
    }
    if (key == EKeys::Up || key == EKeys::Down)
    {
        HandleWheelSteps(key == EKeys::Up ? 1 : -1);
        return true;
    }
    if (key == EKeys::Enter)
    {
        ApplySelectedBody();
        return true;
    }
    if (key == EKeys::Tab)
    {
        mIsListFocused = !mIsListFocused;
        RefreshStepperText();
        RefreshListText();
        return true;
    }
    if (mIsListFocused)
    {
        return false;
    }
    if (key == EKeys::Left || key == EKeys::Right)
    {
        MoveHighlight(key == EKeys::Right);
        return true;
    }
    if (key == EKeys::U)
    {
        mStepper = SOLSpeedStepper::CycleUnit(mStepper, false);
        RefreshStepperText();
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Steps the highlighted digit (or the list selection) by wheel notches (positive = up)
void USOLSpeedPanelWidget::HandleWheelSteps(const int32 steps)
{
    const bool bDown = steps < 0;
    for (int32 step = 0; step < FMath::Abs(steps); ++step)
    {
        if (mIsListFocused)
        {
            // Up moves toward the top of the list
            mListSelection = FMath::Clamp(mListSelection + (bDown ? 1 : -1), 0, FMath::Max(0, BodyRows.Num() - 1));
        }
        else if (mIsUnitHighlighted)
        {
            mStepper = SOLSpeedStepper::CycleUnit(mStepper, bDown);
        }
        else
        {
            StepHighlightedDigit(bDown);
        }
    }
    RefreshStepperText();
    RefreshListText();
}

//////////////////////////////////////////////////////////////////////////
// Routes a key press to HandlePanelKey and consumes every key so none reaches the game viewport
FReply USOLSpeedPanelWidget::NativeOnKeyDown(const FGeometry& /*geometry*/, const FKeyEvent& keyEvent)
{
    ++mSlateKeyCount;
    HandlePanelKey(keyEvent.GetKey());
    return FReply::Handled();
}

//////////////////////////////////////////////////////////////////////////
// Consumes key releases so they do not reach the game viewport either
FReply USOLSpeedPanelWidget::NativeOnKeyUp(const FGeometry& /*geometry*/, const FKeyEvent& /*keyEvent*/)
{
    return FReply::Handled();
}

//////////////////////////////////////////////////////////////////////////
// Routes the mouse wheel to HandleWheelSteps
FReply USOLSpeedPanelWidget::NativeOnMouseWheel(const FGeometry& /*geometry*/, const FPointerEvent& mouseEvent)
{
    const float delta = mouseEvent.GetWheelDelta();
    int32 steps = FMath::RoundToInt32(delta);
    if (steps == 0 && delta != 0.0f)
    {
        steps = delta > 0.0f ? 1 : -1;
    }
    HandleWheelSteps(steps);
    return FReply::Handled();
}

//////////////////////////////////////////////////////////////////////////
// Steps the highlighted digit up or down and applies the resulting cap
void USOLSpeedPanelWidget::StepHighlightedDigit(const bool bDown)
{
    ApplyCapMps(SOLSpeedStepper::StepDigit(mStepper, bDown));
}

//////////////////////////////////////////////////////////////////////////
// Moves the highlight one place left (more significant) or right (toward the unit field)
void USOLSpeedPanelWidget::MoveHighlight(const bool bRight)
{
    if (bRight)
    {
        if (mStepper.HighlightedDigit <= SOLSpeedStepper::MinDigit)
        {
            mIsUnitHighlighted = true;
        }
        else
        {
            --mStepper.HighlightedDigit;
        }
    }
    else if (mIsUnitHighlighted)
    {
        mIsUnitHighlighted = false;
        mStepper.HighlightedDigit = SOLSpeedStepper::MinDigit;
    }
    else
    {
        mStepper.HighlightedDigit = FMath::Min(mStepper.HighlightedDigit + 1, SOLSpeedStepper::MaxDigit);
    }
    mStepper.HighlightedDigit = FMath::Clamp(mStepper.HighlightedDigit, SOLSpeedStepper::MinDigit,
        SOLSpeedStepper::MaxDigit);
    RefreshStepperText();
}

//////////////////////////////////////////////////////////////////////////
// Sets the cap to the highlighted body's listed speed, in that speed's natural unit
void USOLSpeedPanelWidget::ApplySelectedBody()
{
    if (!mBodySpeedsMps.IsValidIndex(mListSelection))
    {
        return;
    }
    mStepper = SOLSpeedStepper::FromSpeedMps(mBodySpeedsMps[mListSelection]);
    mIsUnitHighlighted = false;
    mIsListFocused = false;
    ApplyCapMps(SOLHudFormat::FromUnitValue(mStepper.ValueInUnit, mStepper.Unit));
    if (BodyRegistry != nullptr)
    {
        UE_LOG(LogSOL, Log, TEXT("SpeedPanel: cap set from %s's speed %.3f m/s"),
            *BodyRegistry->GetRegistry().GetName(mListSelection).ToString(), mBodySpeedsMps[mListSelection]);
    }
    RefreshStepperText();
    RefreshListText();
}

//////////////////////////////////////////////////////////////////////////
// Sends a cap to the ship and re-expresses the steppers' value in their current unit
void USOLSpeedPanelWidget::ApplyCapMps(const double capMps)
{
    double appliedMps = capMps;
    if (ASOLShipPawn* pawn = mPawn.Get())
    {
        appliedMps = pawn->ApplySpeedCapMps(capMps);
    }
    mStepper.ValueInUnit = SOLHudFormat::ToUnitValue(appliedMps, mStepper.Unit);
    UE_LOG(LogSOL, Log, TEXT("SpeedPanel: cap %.3f m/s (%.2f %s, digit %d)"), appliedMps, mStepper.ValueInUnit,
        SpeedPanelUnitLabel(mStepper.Unit), mStepper.HighlightedDigit);
}

//////////////////////////////////////////////////////////////////////////
// Reads every body's speed relative to its parent into the reused list buffer
void USOLSpeedPanelWidget::RefreshBodySpeeds()
{
    if (BodyRegistry == nullptr)
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const int32 bodyCount = FMath::Min(registry.Num(), mBodySpeedsMps.Num());
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const int32 parent = registry.GetParent(index);
        const FVector3d parentVelocity = parent == INDEX_NONE ? FVector3d::ZeroVector : registry.GetVelocityMps(parent);
        mBodySpeedsMps[index] = (registry.GetVelocityMps(index) - parentVelocity).Size();
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-renders the digit, unit and cap texts from the stepper state
void USOLSpeedPanelWidget::RefreshStepperText()
{
    // Whole hundredths of the value, so each odometer place is an exact integer digit
    const int64 hundredths = FMath::RoundToInt64(mStepper.ValueInUnit * PANEL_HUNDREDTHS_PER_UNIT);
    const bool bStepperActive = !mIsListFocused;
    for (int32 slot = 0; slot < DigitTexts.Num(); ++slot)
    {
        const int32 digit = SOLSpeedStepper::MaxDigit - slot;
        int64 placeHundredths = 1;
        for (int32 power = SOLSpeedStepper::MinDigit; power < digit; ++power)
        {
            placeHundredths *= 10;
        }
        const int32 value = static_cast<int32>((hundredths / placeHundredths) % 10);
        const TCHAR character[2] = { static_cast<TCHAR>(TEXT('0') + value), TEXT('\0') };
        DigitTexts[slot]->SetText(FText::FromString(character));

        // Highlighted place bright, leading zeros dim
        const bool bHighlighted = bStepperActive && !mIsUnitHighlighted && digit == mStepper.HighlightedDigit;
        const bool bLeadingZero = digit > 0 && hundredths < placeHundredths;
        DigitTexts[slot]->SetColorAndOpacity(FSlateColor(bHighlighted ? PANEL_HIGHLIGHT
            : (bLeadingZero ? PANEL_DIM_TEXT : PANEL_TEXT)));
    }
    UnitText->SetText(FText::FromString(SpeedPanelUnitLabel(mStepper.Unit)));
    UnitText->SetColorAndOpacity(FSlateColor(bStepperActive && mIsUnitHighlighted ? PANEL_HIGHLIGHT : PANEL_TEXT));

    // The cap the ship now flies with, in its auto-picked unit
    FString capLine(TEXT("Ship speed cap: "));
    SpeedPanelAppendSpeed(capLine, SOLHudFormat::FromUnitValue(mStepper.ValueInUnit, mStepper.Unit));
    CapText->SetText(FText::FromString(capLine));
}

//////////////////////////////////////////////////////////////////////////
// Re-renders the body list rows and keeps the selection in view
void USOLSpeedPanelWidget::RefreshListText()
{
    if (BodyRegistry == nullptr)
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    FString row;
    for (int32 index = 0; index < BodyRows.Num() && index < registry.Num(); ++index)
    {
        const bool bSelected = index == mListSelection;
        row.Reset();
        row.Append(bSelected && mIsListFocused ? TEXT("> ") : TEXT("  "));
        const FString name = registry.GetName(index).ToString();
        row.Appendf(TEXT("%-10s "), *name);
        SpeedPanelAppendSpeed(row, mBodySpeedsMps.IsValidIndex(index) ? mBodySpeedsMps[index] : 0.0);
        BodyRows[index]->SetText(FText::FromString(row));
        BodyRows[index]->SetColorAndOpacity(FSlateColor(bSelected && mIsListFocused ? PANEL_HIGHLIGHT
            : (mIsListFocused ? PANEL_TEXT : PANEL_DIM_TEXT)));
    }
    if (BodyRows.IsValidIndex(mListSelection))
    {
        BodyList->ScrollWidgetIntoView(BodyRows[mListSelection], false);
    }
}

//////////////////////////////////////////////////////////////////////////
// Closes the panel through the pawn (which restores ship input)
void USOLSpeedPanelWidget::RequestClose()
{
    if (ASOLShipPawn* pawn = mPawn.Get())
    {
        pawn->CloseSpeedPanel();
        return;
    }
    RemoveFromParent();
}
