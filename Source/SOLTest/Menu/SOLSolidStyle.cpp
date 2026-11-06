/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidStyle.h"

#include "Menu/SOLSolidMenuSubsystem.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Engine/Font.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Styling/CoreStyle.h"

namespace
{
    constexpr float SOLID_STYLE_BUTTON_BRUSH_WIDTH = 240.0f;   // Button brushes stretch; a nominal image width
    constexpr float SOLID_STYLE_SCROLL_HOVER_BRIGHTEN = 0.3f;  // Scroll thumb: hovered blends this far to white
    constexpr float SOLID_STYLE_SCROLL_DRAG_BRIGHTEN = 0.5f;   // ... dragged this far

    //////////////////////////////////////////////////////////////////////////
    // Returns a channel NaN-safe and clamped to [0, 1] (NaN reads 0, infinities their bound)
    float SolidStyleUnit(const float x)
    {
        return FMath::IsNaN(x) ? 0.0f : FMath::Clamp(x, 0.0f, 1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a look carrying only the tint fields: Edge (the LED and underline with it) and the three flags
    FSOLSolidButtonLook SolidStyleLook(const float edge, const bool bPressed, const bool bMark, const bool bDisabled)
    {
        FSOLSolidButtonLook look;
        look.Edge = edge;
        look.Led = edge;
        look.Underline = edge;
        look.Lift = edge;
        look.Trace = edge;
        look.bPressedHull = bPressed;
        look.bSelectedMark = bMark;
        look.bDisabled = bDisabled;
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a style is drawn raised (RAISED_COLOR lifting to RAISED_LIFTED_COLOR in the flat fallback)
    bool SolidStyleIsRaised(const ESOLSolidStyle style)
    {
        return style == ESOLSolidStyle::StackButton || style == ESOLSolidStyle::Tab
            || style == ESOLSolidStyle::SmallButton || style == ESOLSolidStyle::Knob || style == ESOLSolidStyle::Puck;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a hull brush at full alpha (the disabled brushes of Slate-disabled controls: Slate's 0.45 is the dim)
    FSlateBrush SolidStyleOpaqueBrush(const USOLSolidMenuSubsystem* subsystem, const ESOLSolidStyle style,
        const FVector2f& size, const FSOLSolidButtonLook& look)
    {
        FSlateBrush brush = SOLSolidStyle::MakeHullBrush(subsystem, style, size, look);
        FLinearColor tint = brush.TintColor.GetSpecifiedColor();
        tint.A = 1.0f;
        brush.TintColor = FSlateColor(tint);
        return brush;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the hull style of a button variant
ESOLSolidStyle SOLSolidStyle::ButtonStyleOf(const ESOLSolidButtonVariant variant)
{
    switch (variant)
    {
    case ESOLSolidButtonVariant::Tab:
        return ESOLSolidStyle::Tab;
    case ESOLSolidButtonVariant::Small:
        return ESOLSolidStyle::SmallButton;
    case ESOLSolidButtonVariant::Stack:
    default:
        return ESOLSolidStyle::StackButton;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a variant's hover extends a circuit trace (Q15: the stack and the tab column)
bool SOLSolidStyle::HasHoverTrace(const ESOLSolidButtonVariant variant)
{
    return variant == ESOLSolidButtonVariant::Stack || variant == ESOLSolidButtonVariant::Tab;
}

//////////////////////////////////////////////////////////////////////////
// Returns a text role's font size
int32 SOLSolidStyle::FontSize(const ESOLMenuTextRole role)
{
    using namespace SOL::SolidMenu;
    switch (role)
    {
    case ESOLMenuTextRole::Title:
        return FONT_TITLE_SIZE;
    case ESOLMenuTextRole::Heading:
        return FONT_HEADING_SIZE;
    case ESOLMenuTextRole::Section:
        return FONT_SECTION_SIZE;
    case ESOLMenuTextRole::Button:
        return FONT_BUTTON_SIZE;
    case ESOLMenuTextRole::Small:
        return FONT_SMALL_SIZE;
    case ESOLMenuTextRole::Body:
    default:
        return FONT_BODY_SIZE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a text role's letter spacing
int32 SOLSolidStyle::LetterSpacing(const ESOLMenuTextRole role)
{
    using namespace SOL::SolidMenu;
    switch (role)
    {
    case ESOLMenuTextRole::Title:
        return LETTER_SPACING_TITLE;
    case ESOLMenuTextRole::Button:
        return LETTER_SPACING_STACK;
    case ESOLMenuTextRole::Section:
        return LETTER_SPACING_SMALL_CAPS;
    default:
        return 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the F_SOLMenu typeface name of a weight
FName SOLSolidStyle::TypefaceName(const ESOLMenuFontWeight weight)
{
    switch (weight)
    {
    case ESOLMenuFontWeight::Medium:
        return FName(SOL::SolidMenu::FONT_MEDIUM);
    case ESOLMenuFontWeight::SemiBold:
        return FName(SOL::SolidMenu::FONT_SEMIBOLD);
    case ESOLMenuFontWeight::Bold:
        return FName(SOL::MenuStyle::FONT_BOLD);
    case ESOLMenuFontWeight::Regular:
    default:
        return FName(SOL::MenuStyle::FONT_REGULAR);
    }
}

//////////////////////////////////////////////////////////////////////////
// Maps an encoded tint into [TINT_VERTEX_FLOOR, 1] per RGB channel, A clamped
FLinearColor SOLSolidStyle::PackVertexTint(const FLinearColor& encodedTint)
{
    const float floor = SOL::SolidMenu::TINT_VERTEX_FLOOR;
    const float span = 1.0f - floor;
    return FLinearColor(floor + span * SolidStyleUnit(encodedTint.R), floor + span * SolidStyleUnit(encodedTint.G),
        floor + span * SolidStyleUnit(encodedTint.B), FMath::IsNaN(encodedTint.A) ? 1.0f
        : FMath::Clamp(encodedTint.A, 0.0f, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Maps a vertex tint back to the encoded tint (RGB clamped to [0, 1], A unchanged)
FLinearColor SOLSolidStyle::UnpackVertexTint(const FLinearColor& vertexTint)
{
    const float floor = SOL::SolidMenu::TINT_VERTEX_FLOOR;
    const float span = 1.0f - floor;
    return FLinearColor(SolidStyleUnit((vertexTint.R - floor) / span), SolidStyleUnit((vertexTint.G - floor) / span),
        SolidStyleUnit((vertexTint.B - floor) / span), vertexTint.A);
}

//////////////////////////////////////////////////////////////////////////
// Returns the brush tint of a look
FLinearColor SOLSolidStyle::BrushTint(const FSOLSolidButtonLook& look)
{
    return PackVertexTint(SOLSolidMenu::EncodeButtonTint(look));
}

//////////////////////////////////////////////////////////////////////////
// Returns a palette colour (sRGB) in linear space
FLinearColor SOLSolidStyle::Linear(const FColor& srgb)
{
    return FLinearColor::FromSRGBColor(srgb);
}

//////////////////////////////////////////////////////////////////////////
// Returns the body text colour
FLinearColor SOLSolidStyle::TextColor()
{
    return Linear(SOL::SolidMenu::TEXT_COLOR);
}

//////////////////////////////////////////////////////////////////////////
// Returns the dimmer text colour of hints and secondary lines
FLinearColor SOLSolidStyle::DimTextColor()
{
    return Linear(SOL::SolidMenu::TEXT_DIM_COLOR);
}

//////////////////////////////////////////////////////////////////////////
// Returns the title and heading text colour
FLinearColor SOLSolidStyle::BrightTextColor()
{
    return Linear(SOL::SolidMenu::TEXT_BRIGHT_COLOR);
}

//////////////////////////////////////////////////////////////////////////
// Returns the cyan of values, keys and section headings
FLinearColor SOLSolidStyle::ValueColor()
{
    return Linear(SOL::SolidMenu::CYAN_COLOR);
}

//////////////////////////////////////////////////////////////////////////
// Returns the warning colour
FLinearColor SOLSolidStyle::WarningColor()
{
    return Linear(SOL::SolidMenu::WARNING_COLOR);
}

//////////////////////////////////////////////////////////////////////////
// Returns a button label's colour for its look
FLinearColor SOLSolidStyle::LabelColor(const FSOLSolidButtonLook& look)
{
    using namespace SOL::SolidMenu;
    if (look.bDisabled)
    {
        return Linear(DISABLED_TEXT_COLOR);
    }
    return FMath::Lerp(Linear(TEXT_COLOR), Linear(TEXT_BRIGHT_COLOR), SolidStyleUnit(look.Edge));
}

//////////////////////////////////////////////////////////////////////////
// Returns the flat fallback colour of a style for a look
FLinearColor SOLSolidStyle::FlatColor(const ESOLSolidStyle style, const FSOLSolidButtonLook& look)
{
    using namespace SOL::SolidMenu;
    FLinearColor color;
    if (look.bPressedHull)
    {
        color = Linear(PRESSED_COLOR);
    }
    else if (look.bDisabled)
    {
        color = Linear(DISABLED_HULL_COLOR);
    }
    else if (SolidStyleIsRaised(style))
    {
        color = FMath::Lerp(Linear(RAISED_COLOR), Linear(RAISED_LIFTED_COLOR), SolidStyleUnit(look.Edge));
    }
    else
    {
        color = Linear(style == ESOLSolidStyle::Panel ? HULL_COLOR : BASE_COLOR);
    }
    color.A = look.bDisabled ? DISABLED_ALPHA : 1.0f;
    return color;
}

//////////////////////////////////////////////////////////////////////////
// Returns a horizontal slider's cyan fill: from the bar's start (plus the inset) to the knob's centre, in SSlider's
// geometry
FSOLSliderFill SOLSolidStyle::SliderFill(const float allottedWidth, const float handleWidth, const bool bIndentHandle,
    const float percent, const float inset)
{
    // A size that is not finite and positive draws nothing (no NaN reaches the paint)
    const float width = FMath::IsFinite(allottedWidth) ? FMath::Max(allottedWidth, 0.0f) : 0.0f;
    const float handle = FMath::IsFinite(handleWidth) ? FMath::Clamp(handleWidth, 0.0f, width) : 0.0f;
    const float pad = FMath::IsFinite(inset) ? FMath::Max(inset, 0.0f) : 0.0f;
    const float indent = bIndentHandle ? handle : 0.0f;
    const float halfHandle = 0.5f * handle;
    const float centre = SolidStyleUnit(percent) * FMath::Max(width - handle - indent, 0.0f) + 0.5f * indent
        + halfHandle;
    FSOLSliderFill fill;
    fill.StartX = halfHandle + pad;
    const float endX = FMath::Min(centre, width - halfHandle - pad);
    fill.Width = FMath::Max(endX - fill.StartX, 0.0f);
    return fill;
}

//////////////////////////////////////////////////////////////////////////
// Returns a hull brush of a style: the shared style material with the look's brush tint, or a flat colour brush
FSlateBrush SOLSolidStyle::MakeHullBrush(const USOLSolidMenuSubsystem* subsystem, const ESOLSolidStyle style,
    const FVector2f& imageSize, const FSOLSolidButtonLook& look)
{
    UMaterialInstanceDynamic* material = subsystem != nullptr ? subsystem->GetStyleMaterial(style) : nullptr;
    FSlateBrush brush;
    if (material != nullptr)
    {
        brush.SetResourceObject(material);
        brush.DrawAs = ESlateBrushDrawType::Image;
        brush.TintColor = FSlateColor(BrushTint(look));
    }
    else
    {
        brush = FSlateColorBrush(FlatColor(style, look));
    }
    brush.SetImageSize(imageSize);
    return brush;
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu font at a weight and size (F_SOLMenu, or the engine default font)
FSlateFontInfo SOLSolidStyle::MenuFont(const USOLSolidMenuSubsystem* subsystem, const ESOLMenuFontWeight weight,
    const int32 size)
{
    const UFont* font = subsystem != nullptr ? subsystem->GetMenuFont() : nullptr;
    if (font != nullptr)
    {
        return FSlateFontInfo(font, static_cast<float>(size), TypefaceName(weight));
    }
    const bool bBold = weight == ESOLMenuFontWeight::Bold || weight == ESOLMenuFontWeight::SemiBold;
    return FCoreStyle::GetDefaultFontStyle(bBold ? SOL::MenuStyle::FONT_BOLD : SOL::MenuStyle::FONT_REGULAR, size);
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu font of a text role at a weight (its size and letter spacing)
FSlateFontInfo SOLSolidStyle::MenuFont(const USOLSolidMenuSubsystem* subsystem, const ESOLMenuTextRole role,
    const ESOLMenuFontWeight weight)
{
    FSlateFontInfo font = MenuFont(subsystem, weight, FontSize(role));
    font.LetterSpacing = LetterSpacing(role);
    return font;
}

//////////////////////////////////////////////////////////////////////////
// Returns a menu button style of a variant: Normal-look state brushes and the disabled-look disabled brush
FButtonStyle SOLSolidStyle::MakeButtonStyle(const USOLSolidMenuSubsystem* subsystem,
    const ESOLSolidButtonVariant variant)
{
    const ESOLSolidStyle style = ButtonStyleOf(variant);
    const FVector2f size(SOLID_STYLE_BUTTON_BRUSH_WIDTH, SOL::MenuStyle::BUTTON_HEIGHT);
    const FSlateBrush normal = MakeHullBrush(subsystem, style, size, FSOLSolidButtonLook());
    FButtonStyle buttonStyle;
    buttonStyle.SetNormal(normal);
    buttonStyle.SetHovered(normal);
    buttonStyle.SetPressed(normal);
    buttonStyle.SetDisabled(MakeHullBrush(subsystem, style, size, SolidStyleLook(0.0f, false, false, true)));
    const FMargin padding(SOL::MenuStyle::BUTTON_PADDING_X, SOL::MenuStyle::BUTTON_PADDING_Y);
    buttonStyle.SetNormalPadding(padding);
    buttonStyle.SetPressedPadding(padding);
    return buttonStyle;
}

//////////////////////////////////////////////////////////////////////////
// Returns the slider style: the recessed groove and the octagonal knob, with hovered and disabled looks
FSliderStyle SOLSolidStyle::MakeSliderStyle(const USOLSolidMenuSubsystem* subsystem)
{
    using namespace SOL::SolidMenu;
    const FVector2f barSize(SOLID_STYLE_BUTTON_BRUSH_WIDTH, SLIDER_BAR_THICKNESS);
    const FVector2f knobSize(KNOB_WIDTH, KNOB_HEIGHT);
    const FSOLSolidButtonLook rest;
    const FSOLSolidButtonLook lit = SolidStyleLook(1.0f, false, false, false);
    const FSOLSolidButtonLook disabled = SolidStyleLook(0.0f, false, false, true);
    FSliderStyle style;
    style.SetNormalBarImage(MakeHullBrush(subsystem, ESOLSolidStyle::SliderGroove, barSize, rest));
    style.SetHoveredBarImage(MakeHullBrush(subsystem, ESOLSolidStyle::SliderGroove, barSize, lit));
    style.SetDisabledBarImage(SolidStyleOpaqueBrush(subsystem, ESOLSolidStyle::SliderGroove, barSize, disabled));
    style.SetNormalThumbImage(MakeHullBrush(subsystem, ESOLSolidStyle::Knob, knobSize, rest));
    style.SetHoveredThumbImage(MakeHullBrush(subsystem, ESOLSolidStyle::Knob, knobSize, lit));
    style.SetDisabledThumbImage(SolidStyleOpaqueBrush(subsystem, ESOLSolidStyle::Knob, knobSize, disabled));
    style.SetBarThickness(SLIDER_BAR_THICKNESS);
    return style;
}

//////////////////////////////////////////////////////////////////////////
// Returns the slider fill brush: a flat cyan box (no material, so no hull look to encode)
FSlateBrush SOLSolidStyle::MakeSliderFillBrush()
{
    using namespace SOL::SolidMenu;
    FSlateBrush brush = FSlateColorBrush(Linear(CYAN_COLOR));
    brush.SetImageSize(FVector2f(SOLID_STYLE_BUTTON_BRUSH_WIDTH, SLIDER_FILL_THICKNESS));
    return brush;
}

//////////////////////////////////////////////////////////////////////////
// Returns the toggle style: the slot as the background, the puck as the check images (the mark when checked)
FCheckBoxStyle SOLSolidStyle::MakeCheckBoxStyle(const USOLSolidMenuSubsystem* subsystem, const bool bDisabled)
{
    using namespace SOL::SolidMenu;
    const FVector2f size(TOGGLE_WIDTH, TOGGLE_HEIGHT);
    const float litEdge = bDisabled ? 0.0f : 1.0f;

    // Makes one image: the disabled look (the puck keeps its side, the value stays readable) or the given one
    const auto image = [subsystem, &size, bDisabled](const ESOLSolidStyle style, const float edge, const bool bPressed,
        const bool bMark)
    {
        return bDisabled ? SolidStyleOpaqueBrush(subsystem, style, size, SolidStyleLook(0.0f, false, bMark, true))
            : MakeHullBrush(subsystem, style, size, SolidStyleLook(edge, bPressed, bMark, false));
    };
    FCheckBoxStyle style;
    style.SetCheckBoxType(ESlateCheckBoxType::CheckBox);
    style.SetBackgroundImage(image(ESOLSolidStyle::ToggleSlot, 0.0f, false, false));
    style.SetBackgroundHoveredImage(image(ESOLSolidStyle::ToggleSlot, litEdge, false, false));
    style.SetBackgroundPressedImage(image(ESOLSolidStyle::ToggleSlot, litEdge, false, false));
    style.SetUncheckedImage(image(ESOLSolidStyle::Puck, 0.0f, false, false));
    style.SetUncheckedHoveredImage(image(ESOLSolidStyle::Puck, litEdge, false, false));
    style.SetUncheckedPressedImage(image(ESOLSolidStyle::Puck, litEdge, !bDisabled, false));
    style.SetCheckedImage(image(ESOLSolidStyle::Puck, 0.0f, false, true));
    style.SetCheckedHoveredImage(image(ESOLSolidStyle::Puck, litEdge, false, true));
    style.SetCheckedPressedImage(image(ESOLSolidStyle::Puck, litEdge, !bDisabled, true));
    style.SetUndeterminedImage(image(ESOLSolidStyle::Puck, 0.0f, false, false));
    style.SetUndeterminedHoveredImage(image(ESOLSolidStyle::Puck, litEdge, false, false));
    style.SetUndeterminedPressedImage(image(ESOLSolidStyle::Puck, litEdge, false, false));
    style.SetForegroundColor(FLinearColor::White);
    style.SetHoveredForegroundColor(FLinearColor::White);
    style.SetPressedForegroundColor(FLinearColor::White);
    style.SetCheckedForegroundColor(FLinearColor::White);
    style.SetCheckedHoveredForegroundColor(FLinearColor::White);
    style.SetCheckedPressedForegroundColor(FLinearColor::White);
    style.SetUndeterminedForegroundColor(FLinearColor::White);
    style.SetBorderBackgroundColor(FLinearColor::White);
    style.SetPadding(FMargin(SOL::MenuStyle::BUTTON_GAP, 0.0f, 0.0f, 0.0f));
    return style;
}

//////////////////////////////////////////////////////////////////////////
// Returns the scroll bar style: a thin recess and a flat cyan thumb, brighter when hovered and dragged
FScrollBarStyle SOLSolidStyle::MakeScrollBarStyle(const USOLSolidMenuSubsystem* subsystem)
{
    using namespace SOL::SolidMenu;
    const FLinearColor cyan = Linear(CYAN_COLOR);
    const FSlateBrush recess = MakeHullBrush(subsystem, ESOLSolidStyle::Recess,
        FVector2f(SCROLL_BAR_THICKNESS, SCROLL_BAR_THICKNESS), FSOLSolidButtonLook());
    FScrollBarStyle style;
    style.SetVerticalBackgroundImage(recess);
    style.SetHorizontalBackgroundImage(recess);
    style.SetVerticalTopSlotImage(FSlateNoResource());
    style.SetVerticalBottomSlotImage(FSlateNoResource());
    style.SetHorizontalTopSlotImage(FSlateNoResource());
    style.SetHorizontalBottomSlotImage(FSlateNoResource());
    style.SetNormalThumbImage(FSlateColorBrush(cyan));
    style.SetHoveredThumbImage(FSlateColorBrush(FMath::Lerp(cyan, FLinearColor::White,
        SOLID_STYLE_SCROLL_HOVER_BRIGHTEN)));
    style.SetDraggedThumbImage(FSlateColorBrush(FMath::Lerp(cyan, FLinearColor::White,
        SOLID_STYLE_SCROLL_DRAG_BRIGHTEN)));
    style.SetThickness(SCROLL_BAR_THICKNESS);
    return style;
}
