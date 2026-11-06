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
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidSlider.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"
#include "UI/SOLOverlayMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"

namespace
{
    // A Buttons entry's tab for the footer's Revert and Apply, which show on every tab
    constexpr ESOLSettingsTab SETTINGS_EVERY_TAB = ESOLSettingsTab::Count;

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
        { SOLMenuText::SETTINGS_HOLO_MENU_EFFECTS, SOL::Settings::HOLO_MENU_EFFECTS_MIN,
            SOL::Settings::HOLO_MENU_EFFECTS_MAX, SOL::Settings::HOLO_MENU_EFFECTS_STEP, false },
        { SOLMenuText::SETTINGS_CHASE_SAFETY_MARGIN, SOL::Settings::CHASE_SAFETY_MARGIN_MIN,
            SOL::Settings::CHASE_SAFETY_MARGIN_MAX, SOL::Settings::CHASE_SAFETY_MARGIN_STEP, true },
    };
    static_assert(UE_ARRAY_COUNT(SETTINGS_SLIDERS) == static_cast<int32>(ESOLSettingsSlider::Count),
        "one spec per slider");

    // Checkbox labels in ESOLSettingsToggle order
    const TCHAR* const SETTINGS_TOGGLE_LABELS[] = { SOLMenuText::SETTINGS_VSYNC, SOLMenuText::SETTINGS_SPACE_DUST,
        SOLMenuText::SETTINGS_INVERT_Y, SOLMenuText::SETTINGS_HUD_VISIBLE, SOLMenuText::SETTINGS_DEFAULT_ASSIST,
        SOLMenuText::SETTINGS_MOUSE_STEER_AT_START };
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

        // A key press: the page's new key while it listens; any key gives the focus its highlight back
        virtual bool HandleKeyDownEvent(FSlateApplication& /*slate*/, const FKeyEvent& keyEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            if (page != nullptr)
            {
                page->NotifyKeyInput();
            }
            return page != nullptr && page->HandleCaptureDown(keyEvent.GetKey());
        }

        // A mouse move: a real one (the cursor moved; Slate's synthetic moves do not) lets hover win again
        virtual bool HandleMouseMoveEvent(FSlateApplication& /*slate*/, const FPointerEvent& mouseEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            if (page != nullptr && !mouseEvent.GetCursorDelta().IsNearlyZero())
            {
                page->NotifyMouseInput();
            }
            return false;
        }

        // A key release: swallowed when it belongs to the captured press
        virtual bool HandleKeyUpEvent(FSlateApplication& /*slate*/, const FKeyEvent& keyEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            return page != nullptr && page->HandleCaptureUp(keyEvent.GetKey());
        }

        // A mouse press: the page's new key while it listens; a press is mouse input (hover wins again)
        virtual bool HandleMouseButtonDownEvent(FSlateApplication& /*slate*/, const FPointerEvent& mouseEvent) override
        {
            USOLSettingsPageWidget* page = mPage.Get();
            if (page != nullptr)
            {
                page->NotifyMouseInput();
            }
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
        case ESOLSettingsSlider::HoloMenuEffects:
            return values.HoloMenuEffects;
        case ESOLSettingsSlider::ChaseSafetyMargin:
            return values.ChaseSafetyMargin;
        case ESOLSettingsSlider::HudScale:
            return values.HudScale;
        case ESOLSettingsSlider::Count:
            break;
        }
        // Not reachable for a valid slider (Count is no slider); keeps the compiler's missing-case warning live
        return values.HudScale;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a checkbox's value in a settings value set
    bool SettingsGetToggleValue(const SOLSettingsModel::FSettingsValues& values, const ESOLSettingsToggle toggle)
    {
        switch (toggle)
        {
        case ESOLSettingsToggle::Vsync:
            return values.bVsync;
        case ESOLSettingsToggle::SpaceDust:
            return values.bSpaceDust;
        case ESOLSettingsToggle::InvertY:
            return values.bInvertY;
        case ESOLSettingsToggle::HudVisible:
            return values.bHudVisible;
        case ESOLSettingsToggle::MouseSteerAtStart:
            return values.bMouseSteerAtStart;
        case ESOLSettingsToggle::DefaultAssist:
        default:
            return values.bDefaultFlightAssist;
        }
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
    // Adds a dim help line to the top of a tab's scroll box
    void SettingsAddHint(UWidgetTree& tree, UScrollBox& scroll, const TCHAR* hint)
    {
        using namespace SOL::MenuStyle;
        Cast<UScrollBoxSlot>(scroll.AddChild(SOLMenuContent::MakeText(tree, hint, SMALL_FONT_SIZE,
            SOLSolidStyle::DimTextColor(), false, true)))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a widget's last laid-out rectangle lies inside another's (absolute space, a small tolerance)
    bool SettingsIsInside(const UWidget& inner, const UWidget& outer)
    {
        constexpr float tolerance = 0.5f;
        const FGeometry& in = inner.GetCachedGeometry();
        const FGeometry& out = outer.GetCachedGeometry();
        const FVector2D inMin = FVector2D(in.LocalToAbsolute(FVector2f::ZeroVector));
        const FVector2D inMax = FVector2D(in.LocalToAbsolute(in.GetLocalSize()));
        const FVector2D outMin = FVector2D(out.LocalToAbsolute(FVector2f::ZeroVector));
        const FVector2D outMax = FVector2D(out.LocalToAbsolute(out.GetLocalSize()));
        return in.GetLocalSize().X > 0.0f && inMin.X >= outMin.X - tolerance && inMin.Y >= outMin.Y - tolerance
            && inMax.X <= outMax.X + tolerance && inMax.Y <= outMax.Y + tolerance;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint): the heading and hint, the tab rail beside the content recess (one
// scroll box per tab in a switcher), then the footer and the status line under both
void USOLSettingsPageWidget::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    Settings = USOLSettingsSubsystem::Get(this);
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(this);
    UVerticalBox* page = SOLMenuContent::MakePage(*WidgetTree, SOLMenuText::SETTINGS_TITLE);
    WidgetTree->RootWidget = page;
    page->AddChildToVerticalBox(SOLMenuContent::MakeText(*WidgetTree, SOLMenuText::SETTINGS_HINT, SMALL_FONT_SIZE,
        SOLSolidStyle::DimTextColor(), false, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));

    // The tab rail and the content recess; the content switcher has one scroll box per tab (index = ESOLSettingsTab)
    Buttons.Reset();
    mButtonTabs.Reset();
    RowLabels.Reset();
    RowLabelBoxes.Reset();
    RowBoxes.Reset();
    mRowLabelTabs.Reset();
    UHorizontalBox* body = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    page->AddChildToVerticalBox(body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    UHorizontalBoxSlot* railSlot = body->AddChildToHorizontalBox(BuildTabRail());
    railSlot->SetPadding(FMargin(0.0f, 0.0f, SOL::SolidMenu::SETTINGS_BODY_GAP, 0.0f));
    TabSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
    TabScrolls.Reset(static_cast<int32>(ESOLSettingsTab::Count));
    const FScrollBarStyle scrollBarStyle = SOLSolidStyle::MakeScrollBarStyle(solid);
    for (int32 tab = 0; tab < static_cast<int32>(ESOLSettingsTab::Count); ++tab)
    {
        UScrollBox* scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
        scroll->SetWidgetBarStyle(scrollBarStyle);
        scroll->SetScrollbarThickness(FVector2D(SOL::SolidMenu::SCROLL_BAR_THICKNESS));
        scroll->SetScrollbarPadding(FMargin(BUTTON_GAP, 0.0f, 0.0f, 0.0f));
        TabSwitcher->AddChild(scroll);
        TabScrolls.Add(scroll);
    }
    body->AddChildToHorizontalBox(USOLSolidPanel::MakeRecess(*WidgetTree, TabSwitcher,
        SOL::SolidMenu::SETTINGS_CONTENT_PADDING))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    // Returns a tab's scroll box
    const auto scrollOf = [this](const ESOLSettingsTab tab) -> UScrollBox&
    {
        return *TabScrolls[static_cast<int32>(tab)];
    };

    // Adds a stepper row to its tab: '<' value '>' (button index = stepper * 2 + direction slot)
    const auto addStepper = [this](const ESOLSettingsStepper stepper)
    {
        const ESOLSettingsTab tab = SOLSettingsTabs::TabOfStepper(stepper);
        UHorizontalBox* row = MakeRow(tab, SETTINGS_STEPPER_LABELS[static_cast<int32>(stepper)]);
        const TCHAR* labels[] = { SOLMenuText::SETTINGS_STEP_BACK, SOLMenuText::SETTINGS_STEP_FORWARD };
        for (int32 side = 0; side < UE_ARRAY_COUNT(labels); ++side)
        {
            USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
            button->Setup(*WidgetTree, labels[side], static_cast<int32>(stepper) * 2 + side, BODY_FONT_SIZE,
                SOLSettingsTabs::RowButtonVariant());
            button->OnMenuClicked.BindUObject(this, &USOLSettingsPageWidget::HandleStepClicked);
            Buttons.Add(button);
            mButtonTabs.Add(tab);
            if (side == 1)
            {
                UTextBlock* value = SOLMenuContent::MakeText(*WidgetTree, FString(), BODY_FONT_SIZE,
                    SOLSolidStyle::ValueColor());
                value->SetJustification(ETextJustify::Center);
                // A value wider than the room between the buttons (narrow views) is cut, never drawn over them (the
                // ellipsis overflow policy was tried: the box gives the text its full width, so nothing is cut)
                value->SetClipping(EWidgetClipping::ClipToBounds);
                StepperValueTexts.Add(value);
                SettingsAddToRow(*row, value, true);
            }
            SettingsAddToRow(*row, SOLMenuContent::MakeSized(*WidgetTree, button, SETTINGS_STEP_BUTTON_WIDTH, 0.0f),
                false);
        }
        AddRow(tab, row);
    };

    // Adds a toggle row to its tab (the solid slot and puck) and returns the checkbox
    const FCheckBoxStyle toggleStyle = SOLSolidStyle::MakeCheckBoxStyle(solid);
    const auto addToggle = [this, &toggleStyle](const ESOLSettingsToggle toggle)
    {
        const ESOLSettingsTab tab = SOLSettingsTabs::TabOfToggle(toggle);
        UHorizontalBox* row = MakeRow(tab, SETTINGS_TOGGLE_LABELS[static_cast<int32>(toggle)]);
        UCheckBox* checkBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
        checkBox->SetWidgetStyle(toggleStyle);
        Toggles[static_cast<int32>(toggle)] = checkBox;
        SettingsAddToRow(*row, checkBox, false);
        AddRow(tab, row);
        return checkBox;
    };

    // Adds a slider row to its tab (the solid groove, cyan fill and knob, and the value readout) and returns the slider
    const auto addSlider = [this, solid](const ESOLSettingsSlider slider)
    {
        const FSOLSliderSpec& spec = SETTINGS_SLIDERS[static_cast<int32>(slider)];
        const ESOLSettingsTab tab = SOLSettingsTabs::TabOfSlider(slider);
        UHorizontalBox* row = MakeRow(tab, spec.Label);
        USOLSolidSlider* control = WidgetTree->ConstructWidget<USOLSolidSlider>(USOLSolidSlider::StaticClass());
        control->SetMinValue(spec.Min);
        control->SetMaxValue(spec.Max);
        control->SetStepSize(spec.Step);
        control->SetSolidStyle(solid);
        Sliders[static_cast<int32>(slider)] = control;
        SettingsAddToRow(*row, control, true);
        UTextBlock* value = SOLMenuContent::MakeText(*WidgetTree, FString(), SOL::MenuStyle::BODY_FONT_SIZE,
            SOLSolidStyle::ValueColor());
        value->SetJustification(ETextJustify::Right);
        SliderValueTexts[static_cast<int32>(slider)] = value;
        SettingsAddToRow(*row, SOLMenuContent::MakeSized(*WidgetTree, value, SOL::MenuStyle::SETTINGS_VALUE_WIDTH,
            0.0f), false);
        AddRow(tab, row);
        return control;
    };
    Toggles.SetNum(static_cast<int32>(ESOLSettingsToggle::Count));
    Sliders.SetNum(static_cast<int32>(ESOLSettingsSlider::Count));
    SliderValueTexts.SetNum(static_cast<int32>(ESOLSettingsSlider::Count));

    // Graphics
    addStepper(ESOLSettingsStepper::Resolution);
    addStepper(ESOLSettingsStepper::WindowMode);
    addStepper(ESOLSettingsStepper::Quality);
    addToggle(ESOLSettingsToggle::Vsync)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleVsyncChanged);
    addToggle(ESOLSettingsToggle::SpaceDust)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleSpaceDustChanged);

    // Audio
    addSlider(ESOLSettingsSlider::MasterVolume)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMasterVolumeChanged);
    addSlider(ESOLSettingsSlider::SfxVolume)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleSfxVolumeChanged);
    addSlider(ESOLSettingsSlider::MusicVolume)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMusicVolumeChanged);

    // Mouse
    addSlider(ESOLSettingsSlider::MouseSensitivity)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMouseSensitivityChanged);
    addToggle(ESOLSettingsToggle::InvertY)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleInvertYChanged);

    // Gameplay
    addSlider(ESOLSettingsSlider::HudScale)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleHudScaleChanged);
    addSlider(ESOLSettingsSlider::HudEffects)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleHudEffectsChanged);
    addSlider(ESOLSettingsSlider::HoloMenuEffects)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleHoloMenuEffectsChanged);
    addToggle(ESOLSettingsToggle::HudVisible)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleHudVisibleChanged);
    addToggle(ESOLSettingsToggle::DefaultAssist)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleDefaultAssistChanged);
    addToggle(ESOLSettingsToggle::MouseSteerAtStart)->OnCheckStateChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleMouseSteerAtStartChanged);
    addSlider(ESOLSettingsSlider::ChaseSafetyMargin)->OnValueChanged.AddDynamic(this,
        &USOLSettingsPageWidget::HandleChaseSafetyMarginChanged);

    // Overlays (SDD 22): the Overlay Menu's own list, embedded; a click applies at once (like Ctrl+O in flight)
    UScrollBox& overlayScroll = scrollOf(ESOLSettingsTab::Overlays);
    SettingsAddHint(*WidgetTree, overlayScroll, SOLMenuText::SETTINGS_OVERLAYS_HINT);
    OverlayList = CreateWidget<USOLOverlayMenuWidget>(this, USOLOverlayMenuWidget::StaticClass());
    if (OverlayList != nullptr)
    {
        OverlayList->SetEmbedded(true);
        overlayScroll.AddChild(OverlayList);
    }

    // Key bindings: the reset button, then one row per rebindable action
    UScrollBox& keyScroll = scrollOf(ESOLSettingsTab::KeyBindings);

    // Creates a footer-style action button in a fixed-size box and returns the button and its box
    const auto makeActionButton = [this](const TCHAR* label, const EAction action, const float width,
        USOLMenuButton** outButton)
    {
        USOLMenuButton* button = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        button->Setup(*WidgetTree, label, SETTINGS_STEP_BUTTON_COUNT + static_cast<int32>(action),
            SOL::MenuStyle::BODY_FONT_SIZE, SOLSettingsTabs::RowButtonVariant());
        button->OnMenuClicked.BindUObject(this, &USOLSettingsPageWidget::HandleActionClicked);
        Buttons.Add(button);
        mButtonTabs.Add(action == EAction::ResetKeys ? ESOLSettingsTab::KeyBindings : SETTINGS_EVERY_TAB);
        if (outButton != nullptr)
        {
            *outButton = button;
        }
        return SOLMenuContent::MakeSized(*WidgetTree, button, width, SOL::MenuStyle::SETTINGS_ROW_HEIGHT);
    };
    Cast<UScrollBoxSlot>(keyScroll.AddChild(makeActionButton(SOLMenuText::SETTINGS_RESET_KEYS, EAction::ResetKeys,
        SETTINGS_RESET_KEYS_WIDTH, nullptr)))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 2.0f));
    KeyTexts.Reset(SOLKeyBindings::BINDING_COUNT);
    RebindBoxes.Reset(SOLKeyBindings::BINDING_COUNT);
    for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
    {
        UHorizontalBox* row = MakeRow(ESOLSettingsTab::KeyBindings, SOLKeyBindings::GetDef(binding).Label);
        UTextBlock* keyText = SOLMenuContent::MakeText(*WidgetTree, FString(), BODY_FONT_SIZE,
            SOLSolidStyle::ValueColor(), true);
        KeyTexts.Add(keyText);
        SettingsAddToRow(*row, keyText, true);
        USOLMenuButton* rebind = WidgetTree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
        rebind->Setup(*WidgetTree, SOLMenuText::SETTINGS_REBIND, binding, SMALL_FONT_SIZE,
            SOLSettingsTabs::RowButtonVariant());
        rebind->OnMenuClicked.BindUObject(this, &USOLSettingsPageWidget::HandleRebindClicked);
        Buttons.Add(rebind);
        mButtonTabs.Add(ESOLSettingsTab::KeyBindings);
        USizeBox* rebindBox = SOLMenuContent::MakeSized(*WidgetTree, rebind, SETTINGS_REBIND_WIDTH, 0.0f);
        RebindBoxes.Add(rebindBox);
        SettingsAddToRow(*row, rebindBox, false);
        AddRow(ESOLSettingsTab::KeyBindings, row);
    }

    // Footer under the content on every tab: dirty indicator, Revert and Apply; the status line under it
    UHorizontalBox* footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    page->AddChildToVerticalBox(footer)->SetPadding(FMargin(0.0f, BUTTON_GAP, 0.0f, 0.0f));
    DirtyText = SOLMenuContent::MakeText(*WidgetTree, FString(), SMALL_FONT_SIZE, SOLSolidStyle::DimTextColor(), false,
        true);
    SettingsAddToRow(*footer, DirtyText, true)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
    USOLMenuButton* revert = nullptr;
    USOLMenuButton* apply = nullptr;
    SettingsAddToRow(*footer, makeActionButton(SOLMenuText::SETTINGS_REVERT, EAction::Revert, SETTINGS_ACTION_WIDTH,
        &revert), false)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
    SettingsAddToRow(*footer, makeActionButton(SOLMenuText::SETTINGS_APPLY, EAction::Apply, SETTINGS_ACTION_WIDTH,
        &apply), false);
    RevertButton = revert;
    ApplyButton = apply;
    StatusText = SOLMenuContent::MakeText(*WidgetTree, FString(), SMALL_FONT_SIZE, SOLSolidStyle::ValueColor(), false,
        true);
    page->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0.0f, ROW_GAP * 2.0f, 0.0f, 0.0f));
    RefreshFromSettings();
}

//////////////////////////////////////////////////////////////////////////
// Builds the tab rail (the shared FSOLSolidTabRail kit piece): one Tab button per tab, index = column position
USizeBox* USOLSettingsPageWidget::BuildTabRail()
{
    TArray<FString> labels;
    labels.Reserve(static_cast<int32>(ESOLSettingsTab::Count));
    for (int32 index = 0; index < static_cast<int32>(ESOLSettingsTab::Count); ++index)
    {
        labels.Add(SOLSettingsTabs::TabLabel(SOLSettingsTabs::TabFromIndex(index)));
    }
    FSOLMenuButtonClicked onClicked;
    onClicked.BindUObject(this, &USOLSettingsPageWidget::HandleTabClicked);
    return Rail.Build(*WidgetTree, labels, onClicked);
}

//////////////////////////////////////////////////////////////////////////
// Creates a row of a tab: its label in the tab's label column, then room for the control(s)
UHorizontalBox* USOLSettingsPageWidget::MakeRow(const ESOLSettingsTab tab, const FString& label)
{
    using namespace SOL::MenuStyle;
    UHorizontalBox* row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    USizeBox* labelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    labelBox->SetWidthOverride(SOLSettingsTabs::LabelColumnWidth(tab));
    UTextBlock* text = SOLMenuContent::MakeText(*WidgetTree, label, BODY_FONT_SIZE, SOLSolidStyle::TextColor());
    labelBox->SetContent(text);
    RowLabels.Add(text);
    RowLabelBoxes.Add(labelBox);
    mRowLabelTabs.Add(tab);
    UHorizontalBoxSlot* labelSlot = row->AddChildToHorizontalBox(labelBox);
    labelSlot->SetVerticalAlignment(VAlign_Center);
    labelSlot->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
    return row;
}

//////////////////////////////////////////////////////////////////////////
// Adds a row to its tab's scroll box in a fixed-height box, the row gap below it; the box is kept (RowBoxes, parallel
// to RowLabels) so a narrow layout can let a wrapped label grow it
void USOLSettingsPageWidget::AddRow(const ESOLSettingsTab tab, UHorizontalBox* row)
{
    USizeBox* rowBox = SOLMenuContent::MakeSized(*WidgetTree, row, 0.0f, SOL::MenuStyle::SETTINGS_ROW_HEIGHT);
    RowBoxes.Add(rowBox);
    Cast<UScrollBoxSlot>(TabScrolls[static_cast<int32>(tab)]->AddChild(rowBox))->SetPadding(FMargin(0.0f, 0.0f, 0.0f,
        SOL::MenuStyle::ROW_GAP));
}

//////////////////////////////////////////////////////////////////////////
// Fits the Key bindings and Gameplay rows to the rows' laid-out width, only on a change of a tab's narrow state
void USOLSettingsPageWidget::FitNarrowRows()
{
    // The rows are as wide as the content switcher less a scroll bar's room (taken as shown, the Info page's rule, so
    // a tab gets the same layout with or without its bar); before the first layout the switcher has no size and the
    // wide layout stays
    if (TabSwitcher == nullptr)
    {
        return;
    }
    mFitRowWidth = TabSwitcher->GetCachedGeometry().GetLocalSize().X - SOL::SolidMenu::SCROLL_BAR_THICKNESS
        - SOL::MenuStyle::BUTTON_GAP;
    const ESOLSettingsTab narrowable[] = { ESOLSettingsTab::Gameplay, ESOLSettingsTab::KeyBindings };
    for (const ESOLSettingsTab tab : narrowable)
    {
        const bool bNarrow = SOLSettingsTabs::IsNarrowLayout(tab, mFitRowWidth);
        bool& bApplied = mIsNarrowTab[static_cast<int32>(tab)];
        if (bNarrow == bApplied)
        {
            continue;
        }
        bApplied = bNarrow;

        // The tab's rows: the column width and the labels' wrap; a narrow row box grows with a wrapped label
        const float labelWidth = SOLSettingsTabs::LabelColumnWidth(tab, mFitRowWidth);
        for (int32 index = 0; index < RowLabels.Num(); ++index)
        {
            if (mRowLabelTabs[index] != tab)
            {
                continue;
            }
            RowLabelBoxes[index]->SetWidthOverride(labelWidth);
            RowLabels[index]->SetAutoWrapText(bNarrow);
            USizeBox* rowBox = RowBoxes[index];
            if (bNarrow)
            {
                rowBox->ClearHeightOverride();
                rowBox->SetMinDesiredHeight(SOL::MenuStyle::SETTINGS_ROW_HEIGHT);
            }
            else
            {
                rowBox->ClearMinDesiredHeight();
                rowBox->SetHeightOverride(SOL::MenuStyle::SETTINGS_ROW_HEIGHT);
            }
        }
        // The Rebind boxes: narrower, and held to the row height (a fixed row clamps the button to it; a growing one
        // would take the button's taller desired height, so every one-line key row would grow)
        if (tab == ESOLSettingsTab::KeyBindings)
        {
            const float rebindWidth = SOLSettingsTabs::RebindWidth(mFitRowWidth);
            for (USizeBox* rebindBox : RebindBoxes)
            {
                rebindBox->SetWidthOverride(rebindWidth);
                if (bNarrow)
                {
                    rebindBox->SetHeightOverride(SOL::MenuStyle::SETTINGS_ROW_HEIGHT);
                }
                else
                {
                    rebindBox->ClearHeightOverride();
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a key row's Rebind button (its box's content), or null for an invalid binding
const USOLMenuButton* USOLSettingsPageWidget::GetRebindButton(const int32 binding) const
{
    const USizeBox* box = GetRebindBox(binding);
    return box != nullptr ? Cast<USOLMenuButton>(box->GetContent()) : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// The page was shown: the opening tab (Q8, no last-tab memory) with every tab back at its top, no hover left from the
// last visit (the list's hover, and the rail's trace unless a tab still has the keyboard focus) and every control
// re-read
void USOLSettingsPageWidget::RefreshFromSettings()
{
    SelectTab(SOLSettingsTabs::OpeningTab());
    for (UScrollBox* scroll : TabScrolls)
    {
        scroll->ScrollToStart();
    }
    Rail.ScrollToTop();
    if (OverlayList != nullptr)
    {
        OverlayList->ClearEmbeddedHover();
    }
    Rail.ClearStaleHover();
    RefreshControls();
}

//////////////////////////////////////////////////////////////////////////
// Shows a tab: the content switcher's child and the tab buttons' selected mark
void USOLSettingsPageWidget::SelectTab(const ESOLSettingsTab tab)
{
    const ESOLSettingsTab previous = mTab;
    mTab = SOLSettingsTabs::SelectTab(mTab, tab);
    const int32 index = static_cast<int32>(mTab);
    if (TabSwitcher != nullptr && TabSwitcher->GetActiveWidgetIndex() != index)
    {
        TabSwitcher->SetActiveWidgetIndex(index);
    }
    Rail.Select(index);

    // The newly shown tab's buttons get their current look at once (they were not refreshed while hidden)
    if (mTab != previous)
    {
        RefreshButtons(USOLSolidMenuSubsystem::BeginFrame(this));
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the highlights of the shown tab's buttons and the footer's, and of any hidden button not yet at rest (a
// ramp, press or focus still to play out), so a hidden button never keeps a stale hover or focus look
void USOLSettingsPageWidget::RefreshButtons(const FSOLSolidMenuFrame& frame)
{
    for (int32 index = 0; index < Buttons.Num(); ++index)
    {
        USOLMenuButton* button = Buttons[index];
        const ESOLSettingsTab tab = mButtonTabs.IsValidIndex(index) ? mButtonTabs[index] : SETTINGS_EVERY_TAB;
        if (button != nullptr && (tab == SETTINGS_EVERY_TAB || tab == mTab || !button->IsAtRest()))
        {
            button->RefreshHighlightAt(frame, mIsHoverElsewhere);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the content switcher's shown index
int32 USOLSettingsPageWidget::GetShownContentIndex() const
{
    return TabSwitcher != nullptr ? TabSwitcher->GetActiveWidgetIndex() : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's button in the tab column, or null
USOLMenuButton* USOLSettingsPageWidget::GetTabButton(const ESOLSettingsTab tab) const
{
    return Rail.GetButton(static_cast<int32>(tab));
}

//////////////////////////////////////////////////////////////////////////
// Returns a stepper's back or forward button (the steppers' buttons come first in the button array), or null
USOLMenuButton* USOLSettingsPageWidget::GetStepperButton(const ESOLSettingsStepper stepper, const int32 side) const
{
    const int32 index = static_cast<int32>(stepper) * 2 + side;
    return side >= 0 && side < 2 && index < SETTINGS_STEP_BUTTON_COUNT && Buttons.IsValidIndex(index)
        ? Buttons[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a slider, or null
const USOLSolidSlider* USOLSettingsPageWidget::GetSlider(const ESOLSettingsSlider slider) const
{
    const int32 index = static_cast<int32>(slider);
    return Sliders.IsValidIndex(index) ? Sliders[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a checkbox, or null
const UCheckBox* USOLSettingsPageWidget::GetToggle(const ESOLSettingsToggle toggle) const
{
    const int32 index = static_cast<int32>(toggle);
    return Toggles.IsValidIndex(index) ? Toggles[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the tab rail draws a tab's hover trace
bool USOLSettingsPageWidget::IsTabHoverTraceShown(const ESOLSettingsTab tab) const
{
    return SOLSettingsTabs::IsValidTab(tab) && Rail.IsTraceShown(static_cast<int32>(tab));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the tab rail draws any hover trace
bool USOLSettingsPageWidget::IsAnyTabHoverTraceShown() const
{
    return Rail.IsAnyTraceShown();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a binding's key row lies inside the Key bindings tab's scroll box as last laid out
bool USOLSettingsPageWidget::IsBindingRowInView(const int32 binding) const
{
    const UScrollBox* scroll = TabScrolls.IsValidIndex(static_cast<int32>(ESOLSettingsTab::KeyBindings))
        ? TabScrolls[static_cast<int32>(ESOLSettingsTab::KeyBindings)].Get() : nullptr;
    return scroll != nullptr && KeyTexts.IsValidIndex(binding) && SettingsIsInside(*KeyTexts[binding], *scroll);
}

//////////////////////////////////////////////////////////////////////////
// Returns a tab's scroll offset, or -1 for an invalid tab
float USOLSettingsPageWidget::GetTabScrollOffset(const ESOLSettingsTab tab) const
{
    const int32 index = static_cast<int32>(tab);
    return TabScrolls.IsValidIndex(index) ? TabScrolls[index]->GetScrollOffset() : -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns a stepper's value text, or null
const UTextBlock* USOLSettingsPageWidget::GetStepperValueText(const ESOLSettingsStepper stepper) const
{
    const int32 index = static_cast<int32>(stepper);
    return StepperValueTexts.IsValidIndex(index) ? StepperValueTexts[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns a row's label and its tab, or null for an invalid index
const UTextBlock* USOLSettingsPageWidget::GetRowLabel(const int32 index, ESOLSettingsTab& outTab) const
{
    outTab = mRowLabelTabs.IsValidIndex(index) ? mRowLabelTabs[index] : ESOLSettingsTab::Count;
    return RowLabels.IsValidIndex(index) ? RowLabels[index].Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether hover wins over the keyboard focus now: the last input was a real mouse move or press and the mouse
// is over a slider, a toggle or an embedded Overlay tab or row of the shown tab (controls that take no keyboard focus
// on hover, so a focused button would show a second highlight)
bool USOLSettingsPageWidget::IsHoverOnFocuslessControl() const
{
    if (!mIsMouseLastInput)
    {
        return false;
    }
    for (int32 slider = 0; slider < Sliders.Num(); ++slider)
    {
        const USOLSolidSlider* control = Sliders[slider];
        if (control != nullptr && SOLSettingsTabs::TabOfSlider(static_cast<ESOLSettingsSlider>(slider)) == mTab
            && control->IsHovered())
        {
            return true;
        }
    }
    for (int32 toggle = 0; toggle < Toggles.Num(); ++toggle)
    {
        const UCheckBox* control = Toggles[toggle];
        if (control != nullptr && SOLSettingsTabs::TabOfToggle(static_cast<ESOLSettingsToggle>(toggle)) == mTab
            && control->IsHovered())
        {
            return true;
        }
    }
    return mTab == ESOLSettingsTab::Overlays && OverlayList != nullptr
        && (OverlayList->GetEmbeddedHoverRow() != INDEX_NONE || OverlayList->GetHoveredTab() != INDEX_NONE);
}

//////////////////////////////////////////////////////////////////////////
// Re-reads every control from the settings subsystem (the tab is left as it is)
void USOLSettingsPageWidget::RefreshControls()
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
    if (OverlayList != nullptr)
    {
        OverlayList->RefreshRows();
    }
    RefreshTexts();
}

//////////////////////////////////////////////////////////////////////////
// Registers the rebind key capture with Slate while the page is on screen and binds the tabs' hover traces to the
// rail's (re)built trace layer
void USOLSettingsPageWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (FSlateApplication::IsInitialized() && !mInputProcessor.IsValid())
    {
        mInputProcessor = MakeShared<FSOLRebindInputProcessor>(*this);
        FSlateApplication::Get().RegisterInputPreProcessor(mInputProcessor);
    }
    Rail.Bind();
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
    RefreshControls();
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
    case ESOLSettingsSlider::HoloMenuEffects:
        values.HoloMenuEffects = snapped;
        break;
    case ESOLSettingsSlider::ChaseSafetyMargin:
        values.ChaseSafetyMargin = snapped;
        break;
    case ESOLSettingsSlider::HudScale:
        values.HudScale = snapped;
        break;
    case ESOLSettingsSlider::Count:
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
    case ESOLSettingsToggle::SpaceDust:
        values.bSpaceDust = bChecked;
        break;
    case ESOLSettingsToggle::InvertY:
        values.bInvertY = bChecked;
        break;
    case ESOLSettingsToggle::HudVisible:
        values.bHudVisible = bChecked;
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
    RefreshControls();
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
    RefreshControls();
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
    // The listening row is a Key bindings row, so that tab shows (a rebind started from code on another tab)
    SelectTab(ESOLSettingsTab::KeyBindings);
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
// Shows the Key bindings tab, then scrolls it so a binding's key row is in view (the scroll box runs the request on
// its next tick, once the newly shown tab is laid out)
void USOLSettingsPageWidget::ScrollToBinding(const int32 binding)
{
    SelectTab(SOLSettingsTabs::TabForBinding(mTab, binding));
    const int32 keyTab = static_cast<int32>(ESOLSettingsTab::KeyBindings);
    if (mTab == ESOLSettingsTab::KeyBindings && TabScrolls.IsValidIndex(keyTab) && KeyTexts.IsValidIndex(binding))
    {
        TabScrolls[keyTab]->ScrollWidgetIntoView(KeyTexts[binding], false, EDescendantScrollDestination::Center);
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
        KeyTexts[binding]->SetColorAndOpacity(FSlateColor(bListening ? SOLSolidStyle::TextColor()
            : (SOLKeyBindings::HasConflict(keys, binding) ? SOLSolidStyle::WarningColor()
            : SOLSolidStyle::ValueColor())));
    }

    // Dirty indicator and status line
    const bool bDirty = Settings->IsDirty();
    DirtyText->SetText(FText::FromString(bDirty ? SOLMenuText::SETTINGS_STATUS_DIRTY
        : SOLMenuText::SETTINGS_STATUS_CLEAN));
    DirtyText->SetColorAndOpacity(FSlateColor(bDirty ? SOLSolidStyle::WarningColor() : SOLSolidStyle::DimTextColor()));
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
// Refreshes the button highlights (focus follows the keyboard and the mouse; the mouse over a slider, toggle or
// Overlay row wins over the focus, so one highlight shows), gives the tab rail's trace layer the effects strength
// when it changed and fits the narrow rows to the laid-out width (46i)
void USOLSettingsPageWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    const FSOLSolidMenuFrame frame = USOLSolidMenuSubsystem::BeginFrame(this);
    mIsHoverElsewhere = IsHoverOnFocuslessControl();
    Rail.Tick(frame, mIsHoverElsewhere);
    RefreshButtons(frame);
    FitNarrowRows();
}

//////////////////////////////////////////////////////////////////////////
// A tab button was clicked (index = the tab's column position)
void USOLSettingsPageWidget::HandleTabClicked(const int32 index)
{
    // A re-click on the shown tab is a no-op (no scroll reset)
    const ESOLSettingsTab tab = SOLSettingsTabs::TabFromIndex(index);
    if (SOLSettingsTabs::IsTabChange(mTab, tab))
    {
        SelectTab(tab);
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
// Hologram menu effects slider moved (SDD 42)
void USOLSettingsPageWidget::HandleHoloMenuEffectsChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::HoloMenuEffects, value);
}

//////////////////////////////////////////////////////////////////////////
// Chase Down safety margin slider moved (SDD 32)
void USOLSettingsPageWidget::HandleChaseSafetyMarginChanged(const float value)
{
    ApplySliderValue(ESOLSettingsSlider::ChaseSafetyMargin, value);
}

//////////////////////////////////////////////////////////////////////////
// VSync checkbox clicked
void USOLSettingsPageWidget::HandleVsyncChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::Vsync, bChecked);
}

//////////////////////////////////////////////////////////////////////////
// Space dust checkbox clicked (pending until Apply; the dust reads the applied value)
void USOLSettingsPageWidget::HandleSpaceDustChanged(const bool bChecked)
{
    SetToggle(ESOLSettingsToggle::SpaceDust, bChecked);
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
