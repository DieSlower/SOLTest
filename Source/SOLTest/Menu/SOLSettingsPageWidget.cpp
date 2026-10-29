/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsPageWidget.h"

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"

namespace
{
    // One slider's range, step and how its value reads
    struct FSOLSliderSpec
    {
        const TCHAR* Label;
        float Min;
        float Max;
        float Step;
        bool bIsPercent;    // Percent ("75%"), else a multiplier ("1.25x")
    };

    // Sliders in ESOLSettingsSlider order
    const FSOLSliderSpec SETTINGS_SLIDERS[] =
    {
        { SOLMenuText::SETTINGS_MASTER_VOLUME, SOL::Settings::VOLUME_MIN, SOL::Settings::VOLUME_MAX,
            SOL::Settings::VOLUME_STEP, true },
        { SOLMenuText::SETTINGS_SFX_VOLUME, SOL::Settings::VOLUME_MIN, SOL::Settings::VOLUME_MAX,
            SOL::Settings::VOLUME_STEP, true },
        { SOLMenuText::SETTINGS_MUSIC_VOLUME, SOL::Settings::VOLUME_MIN, SOL::Settings::VOLUME_MAX,
            SOL::Settings::VOLUME_STEP, true },
        { SOLMenuText::SETTINGS_MOUSE_SENSITIVITY, SOL::Settings::MOUSE_SENSITIVITY_MIN,
            SOL::Settings::MOUSE_SENSITIVITY_MAX, SOL::Settings::MOUSE_SENSITIVITY_STEP, false },
        { SOLMenuText::SETTINGS_HUD_SCALE, SOL::Settings::HUD_SCALE_MIN, SOL::Settings::HUD_SCALE_MAX,
            SOL::Settings::HUD_SCALE_STEP, false },
        { SOLMenuText::SETTINGS_HUD_EFFECTS, SOL::Settings::HUD_EFFECTS_MIN, SOL::Settings::HUD_EFFECTS_MAX,
            SOL::Settings::HUD_EFFECTS_STEP, false },
    };
    static_assert(UE_ARRAY_COUNT(SETTINGS_SLIDERS) == static_cast<int32>(ESOLSettingsSlider::Count),
        "one spec per slider");

    // Checkbox labels in ESOLSettingsToggle order
    const TCHAR* const SETTINGS_TOGGLE_LABELS[] = { SOLMenuText::SETTINGS_VSYNC, SOLMenuText::SETTINGS_INVERT_Y,
        SOLMenuText::SETTINGS_HUD_VISIBLE, SOLMenuText::SETTINGS_DEFAULT_ASSIST,
        SOLMenuText::SETTINGS_MOUSE_STEER_AT_START, SOLMenuText::SETTINGS_ORBIT_LINES };
    static_assert(UE_ARRAY_COUNT(SETTINGS_TOGGLE_LABELS) == static_cast<int32>(ESOLSettingsToggle::Count),
        "one label per toggle");

    // Stepper labels in ESOLSettingsStepper order
    const TCHAR* const SETTINGS_STEPPER_LABELS[] = { SOLMenuText::SETTINGS_RESOLUTION,
        SOLMenuText::SETTINGS_WINDOW_MODE, SOLMenuText::SETTINGS_QUALITY };
    static_assert(UE_ARRAY_COUNT(SETTINGS_STEPPER_LABELS) == static_cast<int32>(ESOLSettingsStepper::Count),
        "one label per stepper");

    // Button index ranges in the page's one button array: steppers (two each), footer actions, then the Rebind buttons
    constexpr int32 SETTINGS_STEP_BUTTON_COUNT = static_cast<int32>(ESOLSettingsStepper::Count) * 2;

    /**
     * Slate input pre-processor of a Settings page: while the page listens for a rebind key it takes the next key or
     * mouse button before any widget sees it (wherever the keyboard focus is, so a button the mouse hovers or the
     * keyboard focused can never be clicked by it), and also swallows that key's release.
     */
    class FSOLRebindInputProcessor final : public IInputProcessor
    {
    public:

        // Remembers the page it captures for
        explicit FSOLRebindInputProcessor(USOLSettingsPageWidget& page) : mPage(&page) {}

        // Nothing to do per tick
        virtual void Tick(const float /*deltaTime*/, FSlateApplication& /*slate*/, TSharedRef<ICursor> /*cursor*/)
            override
        {
        }

        // A key press: the page's new key while it listens
        virtual bool HandleKeyDownEvent(FSlateApplication& /*slate*/, const FKeyEvent& keyEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            return page != nullptr && page->HandleCaptureDown(keyEvent.GetKey());
        }

        // A key release: swallowed when it belongs to the captured press
        virtual bool HandleKeyUpEvent(FSlateApplication& /*slate*/, const FKeyEvent& keyEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            return page != nullptr && page->HandleCaptureUp(keyEvent.GetKey());
        }

        // A mouse press: the page's new key while it listens
        virtual bool HandleMouseButtonDownEvent(FSlateApplication& /*slate*/, const FPointerEvent& mouseEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            return page != nullptr && page->HandleCaptureDown(mouseEvent.GetEffectingButton());
        }

        // A double click arrives instead of the second press: treated as a press
        virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& /*slate*/, const FPointerEvent& mouseEvent)
            override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            return page != nullptr && page->HandleCaptureDown(mouseEvent.GetEffectingButton());
        }

        // A mouse release: swallowed when it belongs to the captured press
        virtual bool HandleMouseButtonUpEvent(FSlateApplication& /*slate*/, const FPointerEvent& mouseEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            return page != nullptr && page->HandleCaptureUp(mouseEvent.GetEffectingButton());
        }

        // Name shown by Slate's input debugging
        virtual const TCHAR* GetDebugName() const override
        {
            return TEXT("SOLRebindCapture");
        }

    private:

        TWeakObjectPtr<USOLSettingsPageWidget> mPage;   // Page the captured keys go to
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a slider value's display text
    FString SettingsFormatSlider(const ESOLSettingsSlider slider, const float value)
    {
        return SETTINGS_SLIDERS[static_cast<int32>(slider)].bIsPercent ? SOLSettingsRules::FormatPercent(value)
            : SOLSettingsRules::FormatMultiplier(value);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a slider's value in a settings value set
    float SettingsGetSliderValue(const SOLSettingsModel::FSettingsValues& values, const ESOLSettingsSlider slider)
    {
        switch (slider)
        {
        case ESOLSettingsSlider::MasterVolume:
            return values.MasterVolume;
        case ESOLSettingsSlider::SfxVolume:
            return values.SfxVolume;
        case ESOLSettingsSlider::MusicVolume:
            return values.MusicVolume;
        case ESOLSettingsSlider::MouseSensitivity:
            return values.MouseSensitivity;
        case ESOLSettingsSlider::HudEffects:
            return values.HudEffectsStrength;
        case ESOLSettingsSlider::HudScale:
        default:
            return values.HudScale;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a checkbox's value in a settings value set
    bool SettingsGetToggleValue(const SOLSettingsModel::FSettingsValues& values, const ESOLSettingsToggle toggle)
    {
        switch (toggle)
        {
        case ESOLSettingsToggle::Vsync:
            return values.bVsync;
        case ESOLSettingsToggle::InvertY:
            return values.bInvertY;
        case ESOLSettingsToggle::HudVisible:
            return values.bHudVisible;
        case ESOLSettingsToggle::OrbitLines:
            return values.bShowOrbitLines;
        case ESOLSettingsToggle::MouseSteerAtStart:
            return values.bMouseSteerAtStart;
        case ESOLSettingsToggle::DefaultAssist:
        default:
            return values.bDefaultFlightAssist;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a settings row with its fixed-width label, ready for the control(s)
    UHorizontalBox* SettingsMakeRow(UWidgetTree& tree, const FString& label)
    {
        using namespace SOL::MenuStyle;
        UHorizontalBox* row = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        USizeBox* labelBox = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        labelBox->SetWidthOverride(SETTINGS_LABEL_WIDTH);
        labelBox->SetContent(SOLMenuContent::MakeText(tree, label, BODY_FONT_SIZE, TEXT_COLOR));
        UHorizontalBoxSlot* labelSlot = row->AddChildToHorizontalBox(labelBox);
        labelSlot->SetVerticalAlignment(VAlign_Center);
        labelSlot->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
        return row;
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a widget to a row, filling the remaining width when bFill, vertically centred
    UHorizontalBoxSlot* SettingsAddToRow(UHorizontalBox& row, UWidget* widget, const bool bFill)
    {
        UHorizontalBoxSlot* rowSlot = row.AddChildToHorizontalBox(widget);
        rowSlot->SetVerticalAlignment(VAlign_Center);
        if (bFill)
        {
            rowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
        return rowSlot;
    }

    //////////////////////////////////////////////////////////////////////////
    // Wraps a widget in a box of a fixed width (0 = free) and height (0 = free)
    USizeBox* SettingsSized(UWidgetTree& tree, UWidget* content, const float width, const float height)
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
    // Adds a row (fixed height) to the scroll box with the row gap below it
    void SettingsAddRow(UWidgetTree& tree, UScrollBox& scroll, UHorizontalBox* row)
    {
        Cast<UScrollBoxSlot>(scroll.AddChild(SettingsSized(tree, row, 0.0f, SOL::MenuStyle::SETTINGS_ROW_HEIGHT)))
            ->SetPadding(FMargin(0.0f, 0.0f, SOL::MenuStyle::BUTTON_GAP, SOL::MenuStyle::ROW_GAP));
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a section heading to the scroll box
    void SettingsAddSection(UWidgetTree& tree, UScrollBox& scroll, const TCHAR* heading)
    {
        using namespace SOL::MenuStyle;
        Cast<UScrollBoxSlot>(scroll.AddChild(SOLMenuContent::MakeText(tree, heading, SECTION_FONT_SIZE, ACCENT_COLOR,
            true)))->SetPadding(FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f, ROW_GAP * 2.0f));
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint)
void USOLSettingsPageWidget::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    Settings = USOLSettingsSubsystem::Get(this);
    UVerticalBox* page = SOLMenuContent::MakePage(*WidgetTree, SOLMenuText::SETTINGS_TITLE);
    WidgetTree->RootWidget = page;
    page->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::SETTINGS_HINT, SMALL_FONT_SIZE,
        DIM_TEXT_COLOR, false, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    UScrollBox* scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
    Scroll = scroll;
    page->AddChildToVerticalBox(scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Buttons.Reset();

    // Adds a stepper row: '<' value '>' (button index = stepper * 2 + direction slot)
    const auto addStepper = [this, scroll](const ESOLSettingsStepper stepper)
    {
        UHorizontalBox* row = SettingsMakeRow(*WidgetTree, SETTINGS_STEPPER_LABELS[static_cast<int32>(stepper)]);
        const TCHAR* labels[] = { SOLMenuText::SETTINGS_STEP_BACK, SOLMenuText::SETTINGS_STEP_FORWARD };
        for (int32 side = 0; side < UE_ARRAY_COUNT(labels); ++side)
        {
            USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
            button->Setup(*WidgetTree, labels[side], static_cast<int32>(stepper) * 2 + side, BODY_FONT_SIZE);
            button->OnMenuClicked.BindUObject(this, &USOLSettingsPageWidget::HandleStepClicked);
            Buttons.Add(button);
            if (side == 1)
            {
                UTextBlock* value = SOLMenuContent::MakeText(*WidgetTree, FString(), BODY_FONT_SIZE, ACCENT_COLOR);
                value->SetJustification(ETextJustify::Center);
                StepperValueTexts.Add(value);
                SettingsAddToRow(*row, value, true);
            }
            SettingsAddToRow(*row, SettingsSized(*WidgetTree, button, SETTINGS_STEP_BUTTON_WIDTH, 0.0f), false);
        }
        SettingsAddRow(*WidgetTree, *scroll, row);
    };

    // Adds a checkbox row and returns the checkbox
    const auto addToggle = [this, scroll](const ESOLSettingsToggle toggle)
    {
        UHorizontalBox* row = SettingsMakeRow(*WidgetTree, SETTINGS_TOGGLE_LABELS[static_cast<int32>(toggle)]);
        UCheckBox* checkBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
        Toggles[static_cast<int32>(toggle)] = checkBox;
        SettingsAddToRow(*row, checkBox, false);
        SettingsAddRow(*WidgetTree, *scroll, row);
        return checkBox;
    };
    // Adds a slider row (slider and value readout) and returns the slider
    const auto addSlider = [this, scroll](const ESOLSettingsSlider slider)
    {
        const FSOLSliderSpec& spec = SETTINGS_SLIDERS[static_cast<int32>(slider)];
        UHorizontalBox* row = SettingsMakeRow(*WidgetTree, spec.Label);
        USlider* control = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
        control->SetMinValue(spec.Min);
        control->SetMaxValue(spec.Max);
        control->SetStepSize(spec.Step);
        control->SetSliderBarColor(SOL::MenuStyle::BUTTON_SELECTED);
        control->SetSliderHandleColor(SOL::MenuStyle::ACCENT_COLOR);
        Sliders[static_cast<int32>(slider)] = control;
        SettingsAddToRow(*row, control, true);
        UTextBlock* value = SOLMenuContent::MakeText(*WidgetTree, FString(), SOL::MenuStyle::BODY_FONT_SIZE,
            SOL::MenuStyle::ACCENT_COLOR);
        value->SetJustification(ETextJustify::Right);
        SliderValueTexts[static_cast<int32>(slider)] = value;
        SettingsAddToRow(*row, SettingsSized(*WidgetTree, value, SOL::MenuStyle::SETTINGS_VALUE_WIDTH, 0.0f), false);
        SettingsAddRow(*WidgetTree, *scroll, row);
        return control;
    };
    Toggles.SetNum(static_cast<int32>(ESOLSettingsToggle::Count));
    Sliders.SetNum(static_cast<int32>(ESOLSettingsSlider::Count));
    SliderValueTexts.SetNum(static_cast<int32>(ESOLSettingsSlider::Count));

    // Graphics
    SettingsAddSection(*WidgetTree, *scroll, SOLMenuText::SETTINGS_GRAPHICS);
    addStepper(ESOLSettingsStepper::Resolution);
    addStepper(ESOLSettingsStepper::WindowMode);
    addStepper(ESOLSettingsStepper::Quality);
    addToggle(ESOLSettingsToggle::Vsync)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleVsyncChanged);

    // Audio
    SettingsAddSection(*WidgetTree, *scroll, SOLMenuText::SETTINGS_AUDIO);
    addSlider(ESOLSettingsSlider::MasterVolume)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMasterVolumeChanged);
    addSlider(ESOLSettingsSlider::SfxVolume)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleSfxVolumeChanged);
    addSlider(ESOLSettingsSlider::MusicVolume)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMusicVolumeChanged);

    // Mouse
    SettingsAddSection(*WidgetTree, *scroll, SOLMenuText::SETTINGS_MOUSE);
    addSlider(ESOLSettingsSlider::MouseSensitivity)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMouseSensitivityChanged);
    addToggle(ESOLSettingsToggle::InvertY)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleInvertYChanged);

    // Gameplay
    SettingsAddSection(*WidgetTree, *scroll, SOLMenuText::SETTINGS_GAMEPLAY);
    addSlider(ESOLSettingsSlider::HudScale)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleHudScaleChanged);
    addSlider(ESOLSettingsSlider::HudEffects)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleHudEffectsChanged);
    addToggle(ESOLSettingsToggle::HudVisible)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleHudVisibleChanged);
    addToggle(ESOLSettingsToggle::DefaultAssist)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleDefaultAssistChanged);
    addToggle(ESOLSettingsToggle::MouseSteerAtStart)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMouseSteerAtStartChanged);
    addToggle(ESOLSettingsToggle::OrbitLines)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleOrbitLinesChanged);

    // Key bindings: the reset button, then one row per rebindable action
    SettingsAddSection(*WidgetTree, *scroll, SOLMenuText::SETTINGS_KEYS);

    // Creates a footer-style action button in a fixed-size box and returns the box
    const auto makeActionButton = [this](const TCHAR* label, const EAction action, const float width)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, label, SETTINGS_STEP_BUTTON_COUNT + static_cast<int32>(action),
            SOL::MenuStyle::BODY_FONT_SIZE);
        button->OnMenuClicked.BindUObject(this, &USOLSettingsPageWidget::HandleActionClicked);
        Buttons.Add(button);
        return SettingsSized(*WidgetTree, button, width, SOL::MenuStyle::SETTINGS_ROW_HEIGHT);
    };
    Cast<UScrollBoxSlot>(scroll->AddChild(makeActionButton(SOLMenuText::SETTINGS_RESET_KEYS, EAction::ResetKeys,
        SETTINGS_RESET_KEYS_WIDTH)))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    KeyTexts.Reset(SOLKeyBindings::BINDING_COUNT);
    for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
    {
        UHorizontalBox* row = SettingsMakeRow(*WidgetTree, SOLKeyBindings::GetDef(binding).Label);
        UTextBlock* keyText = SOLMenuContent::MakeText(*WidgetTree, FString(), BODY_FONT_SIZE, ACCENT_COLOR, true);
        KeyTexts.Add(keyText);
        SettingsAddToRow(*row, keyText, true);
        USOLMenuButton* rebind = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        rebind->Setup(*WidgetTree, SOLMenuText::SETTINGS_REBIND, binding, SMALL_FONT_SIZE);
        rebind->OnMenuClicked.BindUObject(this, &USOLSettingsPageWidget::HandleRebindClicked);
        Buttons.Add(rebind);
        SettingsAddToRow(*row, SettingsSized(*WidgetTree, rebind, SETTINGS_REBIND_WIDTH, 0.0f), false);
        SettingsAddRow(*WidgetTree, *scroll, row);
    }

    // Footer: dirty indicator, Revert and Apply; the status line under it
    UHorizontalBox* footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    page->AddChildToVerticalBox(footer)->SetPadding(FMargin(0.0f, BUTTON_GAP, 0.0f, 0.0f));
    DirtyText = SOLMenuContent::MakeText(*WidgetTree, FString(), SMALL_FONT_SIZE, DIM_TEXT_COLOR, false, true);
    SettingsAddToRow(*footer, DirtyText, true)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
    SettingsAddToRow(*footer, makeActionButton(SOLMenuText::SETTINGS_REVERT, EAction::Revert, SETTINGS_ACTION_WIDTH),
        false)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
    SettingsAddToRow(*footer, makeActionButton(SOLMenuText::SETTINGS_APPLY, EAction::Apply, SETTINGS_ACTION_WIDTH),
        false);
    StatusText = SOLMenuContent::MakeText(*WidgetTree, FString(), SMALL_FONT_SIZE, ACCENT_COLOR, false, true);
    page->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.0f, ROW_GAP * 2.0f, 0.0f, 0.0f));
    RefreshFromSettings();
}

//////////////////////////////////////////////////////////////////////////
// Re-reads every control from the settings subsystem (the page was shown)
void USOLSettingsPageWidget::RefreshFromSettings()
{
    if (Settings == nullptr)
    {
        return;
    }
    const SOLSettingsModel::FSettingsValues& values = Settings->GetLiveValues();
    for (int32 slider = 0; slider < Sliders.Num(); ++slider)
    {
        Sliders[slider]->SetValue(SettingsGetSliderValue(values, static_cast<ESOLSettingsSlider>(slider)));
    }
    for (int32 toggle = 0; toggle < Toggles.Num(); ++toggle)
    {
        Toggles[toggle]->SetIsChecked(SettingsGetToggleValue(values, static_cast<ESOLSettingsToggle>(toggle)));
    }
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Registers the rebind key capture with Slate while the page is on screen
void USOLSettingsPageWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (FSlateApplication::IsInitialized() && !mInputProcessor.IsValid())
    {
        mInputProcessor = MakeShared<FSOLRebindInputProcessor>(*this);
        FSlateApplication::Get().RegisterInputPreProcessor(mInputProcessor);
    }
}

//////////////////////////////////////////////////////////////////////////
// Stops listening and unregisters the rebind key capture
void USOLSettingsPageWidget::NativeDestruct()
{
    SetListening(INDEX_NONE);
    mSwallowedRelease = EKeys::Invalid;
    if (mInputProcessor.IsValid())
    {
        if (FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().UnregisterInputPreProcessor(mInputProcessor);
        }
        mInputProcessor.Reset();
    }
    Super::NativeDestruct();
}

//////////////////////////////////////////////////////////////////////////
// The page was hidden: stops listening and ends the settings visit
void USOLSettingsPageWidget::NotifyHidden()
{
    SetListening(INDEX_NONE);
    mStatusMessage.Reset();
    if (Settings != nullptr)
    {
        Settings->EndVisit();
    }
    RefreshFromSettings();
}

//////////////////////////////////////////////////////////////////////////
// Sets a slider's value (snapped to its step) as dragging it would
void USOLSettingsPageWidget::SetSliderValue(const ESOLSettingsSlider slider, const float value)
{
    const int32 index = static_cast<int32>(slider);
    if (!Sliders.IsValidIndex(index))
    {
        return;
    }
    const FSOLSliderSpec& spec = SETTINGS_SLIDERS[index];
    const float snapped = SOLSettingsRules::SnapToStep(value, spec.Step, spec.Min, spec.Max);
    Sliders[index]->SetValue(snapped);
    ApplySliderValue(slider, snapped);
}

//////////////////////////////////////////////////////////////////////////
// Applies one slider value to the pending settings (no slider position change)
void USOLSettingsPageWidget::ApplySliderValue(const ESOLSettingsSlider slider, const float value)
{
    if (Settings == nullptr)
    {
        return;
    }
    const FSOLSliderSpec& spec = SETTINGS_SLIDERS[static_cast<int32>(slider)];
    const float snapped = SOLSettingsRules::SnapToStep(value, spec.Step, spec.Min, spec.Max);
    SOLSettingsModel::FSettingsValues values = Settings->GetLiveValues();
    switch (slider)
    {
    case ESOLSettingsSlider::MasterVolume:
        values.MasterVolume = snapped;
        break;
    case ESOLSettingsSlider::SfxVolume:
        values.SfxVolume = snapped;
        break;
    case ESOLSettingsSlider::MusicVolume:
        values.MusicVolume = snapped;
        break;
    case ESOLSettingsSlider::MouseSensitivity:
        values.MouseSensitivity = snapped;
        break;
    case ESOLSettingsSlider::HudEffects:
        values.HudEffectsStrength = snapped;
        break;
    case ESOLSettingsSlider::HudScale:
    default:
        values.HudScale = snapped;
        break;
    }
    Settings->SetPendingValues(values);
    mStatusMessage.Reset();
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Sets a checkbox as clicking it would
void USOLSettingsPageWidget::SetToggle(const ESOLSettingsToggle toggle, const bool bChecked)
{
    if (Settings == nullptr)
    {
        return;
    }
    SOLSettingsModel::FSettingsValues values = Settings->GetLiveValues();
    switch (toggle)
    {
    case ESOLSettingsToggle::Vsync:
        values.bVsync = bChecked;
        break;
    case ESOLSettingsToggle::InvertY:
        values.bInvertY = bChecked;
        break;
    case ESOLSettingsToggle::HudVisible:
        values.bHudVisible = bChecked;
        break;
    case ESOLSettingsToggle::OrbitLines:
        values.bShowOrbitLines = bChecked;
        break;
    case ESOLSettingsToggle::MouseSteerAtStart:
        values.bMouseSteerAtStart = bChecked;
        break;
    case ESOLSettingsToggle::DefaultAssist:
    default:
        values.bDefaultFlightAssist = bChecked;
        break;
    }
    Settings->SetPendingValues(values);
    if (Toggles.IsValidIndex(static_cast<int32>(toggle)))
    {
        Toggles[static_cast<int32>(toggle)]->SetIsChecked(bChecked);
    }
    mStatusMessage.Reset();
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Steps a stepper back or forward as its buttons would
void USOLSettingsPageWidget::Step(const ESOLSettingsStepper stepper, const int32 direction)
{
    if (Settings == nullptr)
    {
        return;
    }
    switch (stepper)
    {
    case ESOLSettingsStepper::Resolution:
    {
        const TArray<FIntPoint>& choices = Settings->GetResolutionChoices();
        const int32 current = SOLSettingsRules::FindResolution(choices, Settings->GetPendingResolution());
        const int32 next = SOLSettingsRules::StepIndex(current == INDEX_NONE ? 0 : current, choices.Num(), direction);
        if (next != INDEX_NONE)
        {
            Settings->SetPendingResolution(choices[next]);
        }
        break;
    }
    case ESOLSettingsStepper::WindowMode:
        Settings->SetPendingWindowMode(SOLSettingsRules::StepIndex(Settings->GetPendingWindowMode(),
            SOLSettingsRules::WINDOW_MODE_COUNT, direction));
        break;
    case ESOLSettingsStepper::Quality:
    default:
    {
        SOLSettingsModel::FSettingsValues values = Settings->GetLiveValues();
        values.QualityPreset = SOLSettingsRules::StepIndex(values.QualityPreset, SOLSettingsRules::QUALITY_PRESET_COUNT,
            direction);
        Settings->SetPendingValues(values);
        break;
    }
    }
    mStatusMessage.Reset();
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Applies the pending settings (Apply button)
void USOLSettingsPageWidget::Apply()
{
    SetListening(INDEX_NONE);
    mStatusMessage.Reset();
    if (Settings != nullptr)
    {
        Settings->ApplyPending();
    }
    RefreshFromSettings();
}

//////////////////////////////////////////////////////////////////////////
// Discards the pending settings (Revert button)
void USOLSettingsPageWidget::Revert()
{
    SetListening(INDEX_NONE);
    mStatusMessage.Reset();
    if (Settings != nullptr)
    {
        Settings->RevertPending();
    }
    RefreshFromSettings();
}

//////////////////////////////////////////////////////////////////////////
// Sets every pending key back to its default (Reset keys button)
void USOLSettingsPageWidget::ResetKeys()
{
    SetListening(INDEX_NONE);
    if (Settings != nullptr)
    {
        Settings->ResetKeysToDefaults();
    }
    SetStatus(SOLMenuText::SETTINGS_KEYS_RESET);
}

//////////////////////////////////////////////////////////////////////////
// Starts listening for a binding's new key (its Rebind button)
void USOLSettingsPageWidget::BeginRebind(const int32 binding)
{
    if (binding < 0 || binding >= SOLKeyBindings::BINDING_COUNT)
    {
        return;
    }
    SetListening(binding);
    SetStatus(FString::Format(SOLMenuText::SETTINGS_REBIND_PROMPT_FORMAT, { SOLKeyBindings::GetDef(binding).Label }));
}

//////////////////////////////////////////////////////////////////////////
// Takes a key while listening: Esc cancels, anything else is offered to the settings as the new key
void USOLSettingsPageWidget::CaptureKey(const FKey& key)
{
    if (!IsListening() || Settings == nullptr)
    {
        return;
    }
    const int32 binding = mListeningBinding;
    SetListening(INDEX_NONE);
    if (key == EKeys::Escape)
    {
        SetStatus(SOLMenuText::SETTINGS_REBIND_CANCELLED);
        return;
    }

    // The settings refuse keys fixed to other controls; a key another action uses is bound and reported
    const FString keyName = key.GetFName().ToString();
    const FString keyLabel = SOLBindingLabel::KeyDisplayName(keyName);
    const TCHAR* actionLabel = SOLKeyBindings::GetDef(binding).Label;
    int32 conflict = INDEX_NONE;
    const SOLKeyBindings::ERebindResult result = Settings->RebindKey(binding, keyName, conflict);
    if (result == SOLKeyBindings::ERebindResult::Refused)
    {
        SetStatus(FString::Format(SOLMenuText::SETTINGS_REBIND_REFUSED_FORMAT, { keyLabel }));
    }
    else if (conflict != INDEX_NONE)
    {
        SetStatus(FString::Format(SOLMenuText::SETTINGS_REBIND_CONFLICT_FORMAT, { actionLabel, keyLabel,
            SOLKeyBindings::GetDef(conflict).Label }));
    }
    else
    {
        SetStatus(FString::Format(SOLMenuText::SETTINGS_REBIND_DONE_FORMAT, { actionLabel, keyLabel }));
    }
}

//////////////////////////////////////////////////////////////////////////
// Scrolls the page so a binding's key row is in view
void USOLSettingsPageWidget::ScrollToBinding(const int32 binding)
{
    if (Scroll != nullptr && KeyTexts.IsValidIndex(binding))
    {
        Scroll->ScrollWidgetIntoView(KeyTexts[binding], false, EDescendantScrollDestination::Center);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the status line's text
FString USOLSettingsPageWidget::GetStatusText() const
{
    return StatusText != nullptr ? StatusText->GetText().ToString() : FString();
}

//////////////////////////////////////////////////////////////////////////
// Returns a key row's text
FString USOLSettingsPageWidget::GetKeyRowText(const int32 binding) const
{
    return KeyTexts.IsValidIndex(binding) ? KeyTexts[binding]->GetText().ToString() : FString();
}

//////////////////////////////////////////////////////////////////////////
// Rewrites every value text, stepper, key row and the status from the settings
void USOLSettingsPageWidget::RefreshTexts()
{
    using namespace SOL::MenuStyle;
    if (Settings == nullptr)
    {
        return;
    }
    const SOLSettingsModel::FSettingsValues& values = Settings->GetLiveValues();
    for (int32 slider = 0; slider < SliderValueTexts.Num(); ++slider)
    {
        const ESOLSettingsSlider kind = static_cast<ESOLSettingsSlider>(slider);
        SliderValueTexts[slider]->SetText(FText::FromString(SettingsFormatSlider(kind,
            SettingsGetSliderValue(values, kind))));
    }
    const FString stepperValues[] = { SOLSettingsRules::FormatResolution(Settings->GetPendingResolution()),
        SOLSettingsRules::WindowModeName(Settings->GetPendingWindowMode()),
        SOLSettingsRules::QualityName(values.QualityPreset) };
    for (int32 stepper = 0; stepper < StepperValueTexts.Num(); ++stepper)
    {
        StepperValueTexts[stepper]->SetText(FText::FromString(stepperValues[stepper]));
    }

    // Key rows: the pending key, the waiting prompt on the listening row, the warning colour on conflicting rows
    const TArray<FString>& keys = Settings->GetPendingKeys();
    for (int32 binding = 0; binding < KeyTexts.Num(); ++binding)
    {
        const bool bListening = binding == mListeningBinding;
        KeyTexts[binding]->SetText(FText::FromString(bListening ? FString(SOLMenuText::SETTINGS_KEY_WAITING)
            : SOLKeyBindings::BindingDisplayName(binding, keys)));
        KeyTexts[binding]->SetColorAndOpacity(FSlateColor(bListening ? TEXT_COLOR
            : (SOLKeyBindings::HasConflict(keys, binding) ? WARNING_COLOR : ACCENT_COLOR)));
    }

    // Dirty indicator and status line
    const bool bDirty = Settings->IsDirty();
    DirtyText->SetText(FText::FromString(bDirty ? SOLMenuText::SETTINGS_STATUS_DIRTY
        : SOLMenuText::SETTINGS_STATUS_CLEAN));
    DirtyText->SetColorAndOpacity(FSlateColor(bDirty ? WARNING_COLOR : DIM_TEXT_COLOR));
    StatusText->SetText(FText::FromString(mStatusMessage));
}

//////////////////////////////////////////////////////////////////////////
// Sets the transient status message and refreshes the status lines
void USOLSettingsPageWidget::SetStatus(const FString& message)
{
    mStatusMessage = message;
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the button highlights (focus follows the keyboard and the mouse)
void USOLSettingsPageWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    for (USOLMenuButton* button : Buttons)
    {
        button->RefreshHighlight();
    }
}

//////////////////////////////////////////////////////////////////////////
// A key or mouse press reached Slate's pre-processor: captured (true) while listening
bool USOLSettingsPageWidget::HandleCaptureDown(const FKey& key)
{
    if (!IsListening())
    {
        return false;
    }
    CaptureKey(key);
    mSwallowedRelease = key;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// A key or mouse release reached Slate's pre-processor: swallowed (true) if it ends the captured press
bool USOLSettingsPageWidget::HandleCaptureUp(const FKey& key)
{
    if (!mSwallowedRelease.IsValid() || key != mSwallowedRelease)
    {
        return false;
    }
    mSwallowedRelease = EKeys::Invalid;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the listening binding (INDEX_NONE stops) and tells the settings whether a key capture is active
void USOLSettingsPageWidget::SetListening(const int32 binding)
{
    mListeningBinding = binding;
    if (Settings != nullptr)
    {
        Settings->SetKeyCaptureActive(binding != INDEX_NONE);
    }
}

//////////////////////////////////////////////////////////////////////////
// Master volume slider moved
void USOLSettingsPageWidget::HandleMasterVolumeChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::MasterVolume, value);
}

//////////////////////////////////////////////////////////////////////////
// Effects volume slider moved
void USOLSettingsPageWidget::HandleSfxVolumeChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::SfxVolume, value);
}

//////////////////////////////////////////////////////////////////////////
// Music volume slider moved
void USOLSettingsPageWidget::HandleMusicVolumeChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::MusicVolume, value);
}

//////////////////////////////////////////////////////////////////////////
// Mouse sensitivity slider moved
void USOLSettingsPageWidget::HandleMouseSensitivityChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::MouseSensitivity, value);
}

//////////////////////////////////////////////////////////////////////////
// HUD scale slider moved
void USOLSettingsPageWidget::HandleHudScaleChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::HudScale, value);
}

//////////////////////////////////////////////////////////////////////////
// HUD hologram effects slider moved (SDD 21)
void USOLSettingsPageWidget::HandleHudEffectsChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::HudEffects, value);
}

//////////////////////////////////////////////////////////////////////////
// VSync checkbox clicked
void USOLSettingsPageWidget::HandleVsyncChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::Vsync, bChecked);
}

//////////////////////////////////////////////////////////////////////////
// Invert Y checkbox clicked
void USOLSettingsPageWidget::HandleInvertYChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::InvertY, bChecked);
}

//////////////////////////////////////////////////////////////////////////
// HUD visible checkbox clicked
void USOLSettingsPageWidget::HandleHudVisibleChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::HudVisible, bChecked);
}

//////////////////////////////////////////////////////////////////////////
// Flight assist at launch checkbox clicked
void USOLSettingsPageWidget::HandleDefaultAssistChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::DefaultAssist, bChecked);
}

//////////////////////////////////////////////////////////////////////////
// Mouse steering at start checkbox clicked
void USOLSettingsPageWidget::HandleMouseSteerAtStartChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::MouseSteerAtStart, bChecked);
}

//////////////////////////////////////////////////////////////////////////
// A stepper button was clicked (index = stepper * 2 + 0 back / 1 forward)
void USOLSettingsPageWidget::HandleStepClicked(const int32 index)
{
    Step(static_cast<ESOLSettingsStepper>(index / 2), index % 2 == 0 ? -1 : 1);
}

//////////////////////////////////////////////////////////////////////////
// A footer button was clicked (index = EAction after the stepper buttons)
void USOLSettingsPageWidget::HandleActionClicked(const int32 action)
{
    switch (static_cast<EAction>(action - SETTINGS_STEP_BUTTON_COUNT))
    {
    case EAction::Revert:
        Revert();
        break;
    case EAction::Apply:
        Apply();
        break;
    case EAction::ResetKeys:
    default:
        ResetKeys();
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// A Rebind button was clicked (index = binding)
void USOLSettingsPageWidget::HandleRebindClicked(const int32 binding)
{
    BeginRebind(binding);
}

//////////////////////////////////////////////////////////////////////////
// Show orbit lines checkbox clicked (previews live: ASOLOrbitLineVisuals follows the pending value)
void USOLSettingsPageWidget::HandleOrbitLinesChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::OrbitLines, bChecked);
}
