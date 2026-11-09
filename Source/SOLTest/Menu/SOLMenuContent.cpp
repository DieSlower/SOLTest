/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuContent.h"

#include "Flight/SOLFlight.h"
#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLGlossary.h"
#include "Menu/SOLHudSymbolView.h"
#include "Menu/SOLInfoText.h"
#include "Menu/SOLKeyboardDiagram.h"
#include "Menu/SOLKeyCapRow.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuButton.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidScroll.h"
#include "Menu/SOLSolidScrollStack.h"
#include "Menu/SOLSolidStyle.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "UI/SOLHoloMenuMath.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "Universe/SOLSimClock.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/EngineVersion.h"
#include "Styling/CoreStyle.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Adds a section heading to a scroll box, with space above it
    void MenuContentAddSection(UWidgetTree& tree, UScrollBox& scroll, const FString& heading)
    {
        using namespace SOL::MenuStyle;
        UTextBlock* text = SOLMenuContent::MakeText(tree, heading, SECTION_FONT_SIZE, SOLSolidStyle::ValueColor(),
            true);
        Cast<UScrollBoxSlot>(scroll.AddChild(text))->SetPadding(FMargin(0.0f, PANEL_PADDING * 0.5f, 0.0f, ROW_GAP));
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a two-column row: a fixed-width left cell and a wrapping right cell, and registers it with the page for
    // the column fit and the fit checks (outLeft gets the left text block)
    UHorizontalBox* MenuContentMakeRow(UWidgetTree& tree, USOLReadOnlyPageWidget& page, const int32 tab,
        const FString& left, const FString& right, const float leftWidth, const FLinearColor& leftColor,
        UTextBlock** outLeft = nullptr)
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
        UTextBlock* rightText = SOLMenuContent::MakeText(tree, right, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false,
            true);
        row->AddChildToHorizontalBox(rightText)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        page.RegisterColumnText(leftText, leftBox, rightText, tab, leftWidth);
        return row;
    }

    //////////////////////////////////////////////////////////////////////////
    // (33b) Creates a Controls row: the key caps of a bindings cell (at the given keys) in a fixed-width left cell,
    // wrapping in it, and the wrapping action cell; registers it with the page (outCaps gets the cap row)
    UHorizontalBox* MenuContentMakeCapRow(UWidgetTree& tree, USOLReadOnlyPageWidget& page, const int32 tab,
        const TCHAR* bindings, const TArray<FString>& keys, const FString& right, const float leftWidth,
        USOLKeyCapRow*& outCaps)
    {
        using namespace SOL::MenuStyle;
        UHorizontalBox* row = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        USizeBox* leftBox = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        leftBox->SetWidthOverride(leftWidth);
        outCaps = tree.ConstructWidget<USOLKeyCapRow>(USOLKeyCapRow::StaticClass());
        outCaps->SetCell(bindings, keys);
        leftBox->SetContent(outCaps);
        row->AddChildToHorizontalBox(leftBox)->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
        UTextBlock* rightText = SOLMenuContent::MakeText(tree, right, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false,
            true);
        UHorizontalBoxSlot* rightSlot = row->AddChildToHorizontalBox(rightText);
        rightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        rightSlot->SetVerticalAlignment(VAlign_Center);
        page.RegisterColumnCaps(outCaps, leftBox, rightText, tab, leftWidth);
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

    //////////////////////////////////////////////////////////////////////////
    // Returns the hazard stripe's brush: T_SOLSolidHazard (a stripe mask) tiled along X in amber, the mask's black
    // left black; a flat amber bar when the texture is missing (the palette colour, never the texture's)
    FSlateBrush MenuContentHazardBrush(const USOLSolidMenuSubsystem* solid)
    {
        using namespace SOL::SolidMenu;
        FSlateBrush brush;
        UTexture2D* texture = solid != nullptr ? solid->GetTexture(ESOLSolidTexture::Hazard) : nullptr;
        if (texture != nullptr)
        {
            brush.SetResourceObject(texture);
            brush.Tiling = ESlateBrushTileType::Horizontal;
        }
        brush.DrawAs = ESlateBrushDrawType::Image;
        brush.SetImageSize(FVector2f(HAZARD_TILE_WIDTH_PX, HAZARD_HEIGHT_PX));
        brush.TintColor = FSlateColor(SOLSolidStyle::Linear(AMBER_COLOR));
        return brush;
    }

    //////////////////////////////////////////////////////////////////////////
    // (33e-ii, D39) Returns a map glossary row's crop brush: its screenshot crop (USOLSolidMenuSubsystem's, loaded
    // once) stretched over the GLOSSARY_VIEW_SIZE square; draws nothing when the crop is missing (the subsystem warned
    // once at load)
    FSlateBrush MenuContentCropBrush(const USOLSolidMenuSubsystem* solid, const int32 crop)
    {
        FSlateBrush brush;
        UTexture2D* texture = solid != nullptr ? solid->GetGlossaryCrop(crop) : nullptr;
        brush.SetResourceObject(texture);
        brush.DrawAs = texture != nullptr ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
        brush.SetImageSize(FVector2f(SOL::Help::GLOSSARY_VIEW_SIZE));
        return brush;
    }

    //////////////////////////////////////////////////////////////////////////
    // (33d-iii) Gives a text-sample row's box and view the size its sample needs (D44): the square symbol box, or the
    // wide box as tall as its lines at the HUD's size
    void MenuContentSizeSampleBox(USizeBox& box, USOLHudSymbolView& view, const FString& sample)
    {
        using namespace SOL::Help;
        const FVector2f size = USOLHudSymbolView::SampleBoxSize(sample);
        box.SetWidthOverride(size.X);
        box.SetHeightOverride(size.Y);
        view.SetNominalSize(size - FVector2f(2.0f * GLOSSARY_SYMBOL_RECESS_PADDING));
    }

    //////////////////////////////////////////////////////////////////////////
    // (33d-ii) Adds a HUD glossary row to a Help tab and registers it with the page: the symbol box (a recess holding
    // a symbol view for a row with symbols or a text sample, a long sample's box wider and taller, D44; a map row's
    // screenshot crop image, 33e-ii; an empty column for a row without art, rule (c)), then the name and the wrapping
    // description at the given keys (text is a reused scratch)
    void MenuContentAddHelpRow(UWidgetTree& tree, USOLReadOnlyPageWidget& page, UScrollBox& scroll, const int32 tab,
        const int32 entry, const TArray<FString>& keys, FString& text)
    {
        using namespace SOL::MenuStyle;
        using namespace SOL::Help;
        const SOLMenuText::FGlossaryEntry& source = SOLMenuText::GLOSSARY_ENTRIES[entry];
        FSOLHelpGlossaryRow row;
        row.Entry = entry;
        row.Tab = tab;

        // The symbol column: the box is square with art (a long text sample's wider and taller, D44), a bare
        // column of its width without
        UHorizontalBox* line = tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        row.Box = tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
        row.Box->SetWidthOverride(GLOSSARY_SYMBOL_BOX);
        const ESOLGlossaryArt art = SOLGlossary::EntryArt(entry);
        if (art == ESOLGlossaryArt::Crop)
        {
            // (33e-ii, D39) A map row: its screenshot crop fills the recess's GLOSSARY_VIEW_SIZE square
            row.Box->SetHeightOverride(GLOSSARY_SYMBOL_BOX);
            row.CropImage = tree.ConstructWidget<UImage>(UImage::StaticClass());
            row.CropImage->SetBrush(MenuContentCropBrush(USOLSolidMenuSubsystem::Get(&tree),
                SOLGlossary::EntryCrop(entry)));
            row.Box->SetContent(USOLSolidPanel::MakeRecess(tree, row.CropImage, GLOSSARY_SYMBOL_RECESS_PADDING));
        }
        else if (art != ESOLGlossaryArt::None)
        {
            row.Box->SetHeightOverride(GLOSSARY_SYMBOL_BOX);
            row.View = tree.ConstructWidget<USOLHudSymbolView>(USOLHudSymbolView::StaticClass());
            if (art == ESOLGlossaryArt::Symbols)
            {
                FSOLGlossaryEntry resolved;
                SOLGlossary::ResolveEntry(entry, resolved);
                row.View->SetSymbols(resolved.Symbols, resolved.SymbolCount);
            }
            else
            {
                SOLGlossary::FormatEntryText(source.Sample, keys, text);
                row.View->SetSample(text, entry);
                row.LastSample = text;
                MenuContentSizeSampleBox(*row.Box, *row.View, text);
            }
            row.Box->SetContent(USOLSolidPanel::MakeRecess(tree, row.View, GLOSSARY_SYMBOL_RECESS_PADDING));
        }
        UHorizontalBoxSlot* boxSlot = line->AddChildToHorizontalBox(row.Box);
        boxSlot->SetPadding(FMargin(0.0f, 0.0f, BUTTON_GAP, 0.0f));
        boxSlot->SetVerticalAlignment(VAlign_Top);

        // The name (SemiBold, the heading colour) over the wrapping description
        UVerticalBox* texts = tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        row.Name = SOLMenuContent::MakeText(tree, source.Name, BODY_FONT_SIZE, SOLSolidStyle::BrightTextColor(), false,
            true);
        row.Name->SetFont(SOLSolidStyle::MenuFont(USOLSolidMenuSubsystem::Get(&tree), ESOLMenuFontWeight::SemiBold,
            BODY_FONT_SIZE));
        texts->AddChildToVerticalBox(row.Name);
        SOLGlossary::FormatEntryText(source.Description, keys, text);
        row.Description = SOLMenuContent::MakeText(tree, text, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false,
            true);
        row.LastDescription = text;
        texts->AddChildToVerticalBox(row.Description);
        UHorizontalBoxSlot* textSlot = line->AddChildToHorizontalBox(texts);
        textSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        textSlot->SetVerticalAlignment(VAlign_Top);
        Cast<UScrollBoxSlot>(scroll.AddChild(line))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, GLOSSARY_ROW_GAP));
        page.AddGlossaryRow(row);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the hazard stripe (SDD 46 section 5.2): the amber stripe image, HAZARD_HEIGHT_PX tall, full width
USizeBox* SOLMenuContent::MakeHazardStripe(UWidgetTree& tree, const USOLSolidMenuSubsystem* solid, const FName name)
{
    UImage* hazard = tree.ConstructWidget<UImage>(UImage::StaticClass(), name);
    hazard->SetBrush(MenuContentHazardBrush(solid));
    return MakeSized(tree, hazard, 0.0f, SOL::SolidMenu::HAZARD_HEIGHT_PX);
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
// Wraps a widget in a fixed-size box (0 leaves that axis free)
USizeBox* SOLMenuContent::MakeSized(UWidgetTree& tree, UWidget* content, const float width, const float height)
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
// Builds the page shell: the hull with side notches, the content recess with the page switcher and the Back recess
USOLSolidPanel* SOLMenuContent::BuildPageShell(UWidgetTree& tree, UWidgetSwitcher* pageSwitcher,
    USOLMenuButton*& outBackButton)
{
    using namespace SOL::MenuStyle;
    using namespace SOL::SolidMenu;
    USOLSolidPanel* shellPanel = tree.ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass(),
        TEXT("ShellPanel"));
    FSOLSolidHull hull = USOLSolidPanel::MakePanelHull();
    hull.NotchY = 0.5f * SOL::SolidMenu::PAGE_HEIGHT;
    hull.NotchDepth = SHELL_NOTCH_DEPTH;
    hull.NotchHeight = SHELL_NOTCH_HEIGHT;
    shellPanel->SetHull(ESOLSolidStyle::Panel, hull);
    shellPanel->SetContentPadding(FMargin(PANEL_INSET_PX + SHELL_PADDING));
    shellPanel->SetDecor(true, true);
    UVerticalBox* shell = tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    shellPanel->SetContent(shell);

    // The content recess with the pages
    shell->AddChildToVerticalBox(USOLSolidPanel::MakeRecess(tree, pageSwitcher, SHELL_RECESS_PADDING))->SetSize(
        FSlateChildSize(ESlateSizeRule::Fill));

    // Back, bottom right (the owner binds it to the same close as Esc)
    outBackButton = tree.ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
    outBackButton->Setup(tree, SOLMenuText::PAGE_BACK, 0, BODY_FONT_SIZE, ESOLSolidButtonVariant::Small);
    UVerticalBoxSlot* backSlot = shell->AddChildToVerticalBox(USOLSolidPanel::MakeRecess(tree, MakeSized(tree,
        outBackButton, BACK_BUTTON_WIDTH, SETTINGS_ROW_HEIGHT), SHELL_BACK_RECESS_PADDING));
    backSlot->SetHorizontalAlignment(HAlign_Right);
    backSlot->SetPadding(FMargin(0.0f, SHELL_BACK_GAP, 0.0f, 0.0f));
    return shellPanel;
}

//////////////////////////////////////////////////////////////////////////
// Returns the lower-edge traces: a long one dropping from row A to row B, a short one on row A from the right, one on
// row B from the left
void SOLMenuContent::MakeEdgeTraces(const float left, const float right, const float rowA,
    FSOLSolidTrace (&outTraces)[EDGE_TRACE_COUNT])
{
    using namespace SOL::SolidMenu;
    const float bend = COLUMN_TRACE_GAP;    // The bends are 45 degrees: one row apart over one gap
    const float rowB = rowA + bend;
    const float mid = 0.5f * (left + right);
    const float dropX = mid - COLUMN_TRACE_DROP_BENDS * bend;
    outTraces[0] = FSOLSolidTrace::FromPoints({ FVector2f(left, rowA), FVector2f(dropX, rowA),
        FVector2f(dropX + bend, rowB), FVector2f(right - COLUMN_TRACE_END_BENDS * bend, rowB) }, true);
    outTraces[1] = FSOLSolidTrace::FromPoints({ FVector2f(right, rowA), FVector2f(mid, rowA) }, true);
    outTraces[2] = FSOLSolidTrace::FromPoints({ FVector2f(left, rowB),
        FVector2f(mid - COLUMN_TRACE_SHORT_BENDS * bend, rowB) }, true);
}

//////////////////////////////////////////////////////////////////////////
// Runs a stack's power-up LED blink (until it ends) and the idle pulse dot on its panel's trace layer
bool SOLMenuContent::TickStackLights(SSOLSolidTraceLayer& layer, const TArray<TObjectPtr<USOLMenuButton>>& buttons,
    const double sinceOpenS, const double nowS, const float s, const bool bPowerUpRunning)
{
    // The blink: each entry's LED in stack order; one whose own LED already shows (hover, press, mark) is left alone
    bool bStillRunning = false;
    if (bPowerUpRunning)
    {
        uint32 mask = 0;
        for (int32 entry = 0; entry < buttons.Num() && entry < SSOLSolidTraceLayer::MAX_LEDS; ++entry)
        {
            const FSOLSolidButtonLook& look = buttons[entry]->GetAppliedLook();
            const bool bOwnLed = look.Led > 0.0f || look.bSelectedMark || look.bDisabled;
            if (!bOwnLed && SOLSolidMenu::PowerUpLedLit(entry, buttons.Num(), sinceOpenS, s))
            {
                mask |= 1u << entry;
            }
        }
        layer.SetLitLeds(mask);
        bStillRunning = SOLSolidMenu::IsPowerUpRunning(sinceOpenS, s);
    }
    layer.SetPulseDot(SOLSolidMenu::PulseDotFraction(nowS), SOLSolidMenu::PulseDotAlpha(s));
    return bStillRunning;
}

//////////////////////////////////////////////////////////////////////////
// Returns a stack's per-entry hover traces and LED centres on the entries' unscrolled centre lines
void SOLMenuContent::MakeStackLights(const FSOLSolidScrollStack& stack, const float ledX, const float traceLeft,
    const float traceRight, TArrayView<FSOLSolidTrace> outTraces, TArrayView<FVector2f> outLeds)
{
    const int32 count = FMath::Min(outTraces.Num(), outLeds.Num());
    for (int32 entry = 0; entry < count; ++entry)
    {
        const float y = stack.ViewTop + SOLSolidScroll::StackItemTop(entry, stack.ItemHeight, stack.Gap, 0.0f)
            + 0.5f * stack.ItemHeight;
        outTraces[entry] = FSOLSolidTrace::FromPoints({ FVector2f(traceLeft, y), FVector2f(traceRight, y) }, false);
        outLeds[entry] = FVector2f(ledX, y);
    }
}

//////////////////////////////////////////////////////////////////////////
// Binds a stack's hover traces and LEDs to its panel's trace layer and makes the stack push its scroll again
void SOLMenuContent::BindStackLights(const TSharedPtr<SSOLSolidTraceLayer>& layer,
    const TArray<TObjectPtr<USOLMenuButton>>& buttons, TConstArrayView<FSOLSolidTrace> traces,
    TConstArrayView<FVector2f> leds, FSOLSolidScrollStack& stack)
{
    for (int32 entry = 0; entry < buttons.Num() && entry < traces.Num(); ++entry)
    {
        buttons[entry]->SetHoverTrace(layer, traces[entry]);
    }
    if (layer.IsValid())
    {
        layer->SetLeds(leds);
    }
    stack.ResetPushed();
}

//////////////////////////////////////////////////////////////////////////
// Starts a panel's open and plays the open cue only when it opens from Closed
bool SOLMenuContent::BeginPanelOpenWithCue(const UObject& context, FSOLHoloMenuAnim& anim, const double nowS,
    const float s)
{
    return BeginPanelOpenWithCue(context, anim, nowS, s, SOL::SolidMenu::PANEL_OPEN_S, SOL::SolidMenu::CLOSE_S);
}

//////////////////////////////////////////////////////////////////////////
// Starts a panel's open at an open / close duration and plays the open cue only when it opens from Closed
bool SOLMenuContent::BeginPanelOpenWithCue(const UObject& context, FSOLHoloMenuAnim& anim, const double nowS,
    const float s, const double openS, const double closeS)
{
    const bool bWasClosed = anim.Phase == ESOLHoloMenuPhase::Closed;
    if (!SOLSolidMenu::BeginPanelOpen(anim, nowS, s, openS, closeS) || !bWasClosed)
    {
        return false;
    }
    USOLSolidMenuSubsystem::PlayCue(&context, ESOLHoloCue::Open, nowS);
    return true;
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
// Builds the Help page
USOLReadOnlyPageWidget* SOLMenuContent::BuildHelpPage(UWidgetTree& tree)
{
    return BuildReadOnlyPage(tree, ESOLReadOnlyPage::Help);
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
    case ESOLReadOnlyPage::Help:
        FillHelpTabs(page, tree);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills the Controls tabs: one per area, in table order (rows of one area are contiguous); the key cap cells start from
// the defaults and are refreshed from the live keys whenever the page is shown
void SOLMenuContent::FillControlsTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    page.ResetKeyCells(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS));
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
        USOLKeyCapRow* keyCell = nullptr;
        MenuContentAddRow(scroll, MenuContentMakeCapRow(tree, page, tab, row.Bindings, defaultKeys,
            FormatControlAction(row.Action), SOLPageTabs::LeftColumnWidth(ESOLReadOnlyPage::Controls, 0.0f), keyCell));
        page.AddKeyCell(keyCell);
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows the live keys' caps in the Controls page's key cells
void SOLMenuContent::RefreshControlsPage(const TArray<TObjectPtr<USOLKeyCapRow>>& keyCells,
    const TArray<FString>& liveKeys)
{
    const int32 count = FMath::Min(keyCells.Num(), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)));
    for (int32 row = 0; row < count; ++row)
    {
        if (keyCells[row] != nullptr)
        {
            keyCells[row]->SetCell(SOLMenuText::CONTROL_ROWS[row].Bindings, liveKeys);
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
    page.ResetKeyTexts(UE_ARRAY_COUNT(INFO_PROSE) + 1);

    // Flight, from the speed-cap constants and the default flight parameters the player's ship uses; addLine adds a
    // label / value row to the current tab and returns the label's text block
    int32 tab = static_cast<int32>(ESOLInfoTab::Flight);
    UScrollBox* scroll = &page.GetFillScroll(tab);
    MenuContentAddSection(tree, *scroll, INFO_FLIGHT);
    const auto addLine = [&tree, &page, &scroll, &tab](const FString& label, const FString& value)
    {
        UTextBlock* labelText = nullptr;
        MenuContentAddRow(*scroll, MenuContentMakeRow(tree, page, tab, label, value,
            SOLPageTabs::LeftColumnWidth(ESOLReadOnlyPage::Info, 0.0f), SOLSolidStyle::TextColor(), &labelText));
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
    tab = static_cast<int32>(ESOLInfoTab::Time);
    scroll = &page.GetFillScroll(tab);
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
    page.AddKeyText(addLine(SOLInfoText::FormatSurfaceLockManualLabel(defaultKeys), FString::Format(
        INFO_MANUAL_RANGE_FORMAT, { SOLInfoText::FormatPercentOrValue(SOL::SURFACE_LOCK_MANUAL_RANGE_MIN_M
        / SOL::METERS_PER_KM) })));

    // Combat, through the tested formatter
    tab = static_cast<int32>(ESOLInfoTab::Combat);
    scroll = &page.GetFillScroll(tab);
    MenuContentAddSection(tree, *scroll, INFO_COMBAT);
    for (const SOLInfoText::FInfoLine& line : SOLInfoText::BuildCombatLines(SOL::COMBAT_MUZZLE_SPEED_MPS,
        SOL::COMBAT_BOLT_DAMAGE, SOL::COMBAT_FIRE_RATE_PER_S, SOL::COMBAT_TARGET_SHIELD, SOL::COMBAT_TARGET_HEALTH,
        SOL::COMBAT_TARGET_SHIELD_REGEN_PER_S, SOL::COMBAT_TARGET_SHIELD_REGEN_DELAY_S))
    {
        addLine(line.Label, line.Value);
    }

    // Prose on how the mechanics behave (the keys it names follow rebinds, through RefreshInfoPage)
    tab = static_cast<int32>(ESOLInfoTab::HowItWorks);
    scroll = &page.GetFillScroll(tab);
    MenuContentAddSection(tree, *scroll, INFO_HOW_IT_WORKS);
    for (const FString& paragraph : SOLInfoText::BuildProse(defaultKeys))
    {
        UTextBlock* text = MakeText(tree, paragraph, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false, true);
        Cast<UScrollBoxSlot>(scroll->AddChild(text))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, ROW_GAP * 3.0f));
        page.AddKeyText(text);
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
    UScrollBox& game = page.GetFillScroll(static_cast<int32>(ESOLAboutTab::Game));
    MenuContentAddText(game, MakeText(tree, GAME_TITLE, TITLE_FONT_SIZE, SOLSolidStyle::BrightTextColor(), true), 0.0f);
    MenuContentAddText(game, MakeText(tree, STUDIO_NAME, SUBTITLE_FONT_SIZE, SOLSolidStyle::ValueColor(), true), gap);
    MenuContentAddText(game, MakeText(tree, ABOUT_DESCRIPTION, BODY_FONT_SIZE, SOLSolidStyle::TextColor(), false,
        true), gap);
    MenuContentAddText(game, MakeText(tree, FString::Format(ABOUT_ENGINE_FORMAT,
        { FEngineVersion::Current().ToString(EVersionComponent::Patch) }), BODY_FONT_SIZE, SOLSolidStyle::TextColor()),
        gap);
    MenuContentAddText(game, MakeText(tree, ABOUT_COPYRIGHT, SMALL_FONT_SIZE, SOLSolidStyle::DimTextColor(), false,
        true), 0.0f);
    UScrollBox& credits = page.GetFillScroll(static_cast<int32>(ESOLAboutTab::Credits));
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

//////////////////////////////////////////////////////////////////////////
// Fills the Help tabs (SDD 33 sections 5, 7 and 8): the Keyboard tab's heading, live diagram and pointer to Controls,
// then each glossary tab's heading and rows (symbol box, name, description), all at the default keys until the first
// RefreshHelpPage
void SOLMenuContent::FillHelpTabs(USOLReadOnlyPageWidget& page, UWidgetTree& tree)
{
    using namespace SOL::MenuStyle;
    const TArray<FString> defaultKeys = SOLKeyBindings::DefaultKeys();

    // Keyboard: the diagram the page owns, then the line pointing to the Controls page
    UScrollBox& keys = page.GetFillScroll(static_cast<int32>(ESOLHelpTab::Keyboard));
    MenuContentAddSection(tree, keys, SOLGlossary::TabHeading(ESOLHelpTab::Keyboard));
    USOLKeyboardDiagram* diagram = tree.ConstructWidget<USOLKeyboardDiagram>(USOLKeyboardDiagram::StaticClass());
    diagram->SetKeys(defaultKeys);
    keys.AddChild(diagram);
    page.SetDiagram(diagram, SOL::Help::DIAGRAM_START_LAYOUT_WIDTH);
    UTextBlock* hint = MakeText(tree, SOLMenuText::HELP_KEYBOARD_CONTROLS, BODY_FONT_SIZE,
        SOLSolidStyle::DimTextColor(), false, true);
    Cast<UScrollBoxSlot>(keys.AddChild(hint))->SetPadding(FMargin(0.0f, ROW_GAP * 3.0f, 0.0f, 0.0f));
    page.SetKeyboardHint(hint);

    // The glossary tabs: one row per GLOSSARY_ENTRIES row of the tab (one index scratch reused)
    page.ResetGlossaryRows(SOLGlossary::EntryCount());
    TArray<int32> entries;
    FString text;
    for (int32 tab = static_cast<int32>(ESOLHelpTab::FlightHud); tab < SOLGlossary::TabCount(); ++tab)
    {
        UScrollBox& scroll = page.GetFillScroll(tab);
        MenuContentAddSection(tree, scroll, SOLGlossary::TabHeading(static_cast<ESOLHelpTab>(tab)));
        SOLGlossary::EntriesOfTab(static_cast<ESOLHelpTab>(tab), entries);
        for (const int32 entry : entries)
        {
            MenuContentAddHelpRow(tree, page, scroll, tab, entry, defaultKeys, text);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes the Help page from the live keys: the diagram's keys (it skips an unchanged set), then each glossary
// row's description and sample, each written only when its formatted text changed
void SOLMenuContent::RefreshHelpPage(USOLReadOnlyPageWidget& page, const TArray<FString>& liveKeys)
{
    if (USOLKeyboardDiagram* diagram = page.GetDiagram())
    {
        diagram->SetKeys(liveKeys);
    }
    FString text;
    for (FSOLHelpGlossaryRow& row : page.GetGlossaryRowsForRefresh())
    {
        // The description at the live keys, written only when it changed; a sample row's view gets its new sample
        // (it rebuilds at its next update) only when the formatted sample changed
        if (row.Entry < 0 || row.Entry >= SOLGlossary::EntryCount())
        {
            continue;
        }
        const SOLMenuText::FGlossaryEntry& source = SOLMenuText::GLOSSARY_ENTRIES[row.Entry];
        SOLGlossary::FormatEntryText(source.Description, liveKeys, text);
        if (text != row.LastDescription && row.Description != nullptr)
        {
            row.Description->SetText(FText::FromString(text));
            row.LastDescription = text;
        }
        if (row.View != nullptr && row.View->IsSample())
        {
            SOLGlossary::FormatEntryText(source.Sample, liveKeys, text);
            if (text != row.LastSample)
            {
                row.View->SetSample(text, row.Entry);
                row.LastSample = text;
                if (row.Box != nullptr)
                {
                    MenuContentSizeSampleBox(*row.Box, *row.View, text);
                }
            }
        }
    }
}
