/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSolidMenuMath.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

class USOLSolidMenuSubsystem;

// The hull material's style instances (SDD 46 section 10): one shared material each; the order is
// SOL::Paths::SOLID_STYLE_MATERIALS'
enum class ESOLSolidStyle : uint8
{
    Panel,          // Raised hull panel (the big panels get their own instance for their step, chin and notches)
    Recess,         // Sunken flat recess (content, text, chin recess)
    StackButton,    // Main menu and pause stack: cut top-left corner, pointed right cap, LED, underline
    Tab,            // The shell's tab column (the stack hull)
    SmallButton,    // Steppers, Rebind, footer, Back, start-location rows: the cut corner without the point
    SliderGroove,   // Recessed slider bar
    Knob,           // Octagonal slider knob with a centred LED
    ToggleSlot,     // Recessed toggle slot
    Puck,           // Toggle puck: left when unchecked, right with a cyan LED when checked (the tint's mark)
    Count,
};

// The menu button variants (SDD 46 section 5.2); a variant picks the button's hull style and whether its hover extends
// a circuit trace (Q15: the stack and the tab column only)
enum class ESOLSolidButtonVariant : uint8
{
    Stack,
    Tab,
    Small,
};

// The typefaces of the menu font F_SOLMenu (SDD 46 section 4.4)
enum class ESOLMenuFontWeight : uint8
{
    Regular,
    Medium,
    SemiBold,
    Bold,
};

// Text roles, each with its size and letter spacing (SDD 46 section 9)
enum class ESOLMenuTextRole : uint8
{
    Title,      // "SOLTest", "PAUSED"
    Heading,    // Page headings
    Section,    // Section headings, small caps
    Button,     // Stack, tab and small button labels
    Body,       // Body text, rows, values
    Small,      // Hints, version line, status line
};

static_assert(static_cast<int32>(ESOLSolidStyle::Count) == UE_ARRAY_COUNT(SOL::Paths::SOLID_STYLE_MATERIALS),
    "ESOLSolidStyle needs one entry per SOL::Paths::SOLID_STYLE_MATERIALS row");

/**
 * The solid menu kit's styles (SDD 46 sections 4.2, 5 and 9): pure decisions (fonts, colours, the vertex tint packing)
 * and the factories that build Slate styles from the shared hull materials of USOLSolidMenuSubsystem. Factories are
 * called once per widget at construction, never per frame. A null subsystem, or one whose hull material or font is
 * missing, gives the flat fallback: plain colour brushes and the engine default font, with the same sizes.
 *
 * Every hull brush carries its look in its tint as PackVertexTint(SOLSolidMenu::EncodeButtonTint(look)): Slate
 * multiplies a material brush's colour by its tint for box draws (SlateElementPixelShader.usf GetColor, 46c engine
 * check), so the codes sit in [TINT_VERTEX_FLOOR, 1] and the hull material divides the tint back out. A white tint
 * reads as pressed, selected and lit: never leave a hull brush white.
 */
namespace SOLSolidStyle
{
    // --- Pure decisions (headless) ---

    // Returns the hull style of a button variant (Stack -> StackButton, Tab -> Tab, Small -> SmallButton)
    SOLTEST_API ESOLSolidStyle ButtonStyleOf(ESOLSolidButtonVariant variant);

    // Returns whether a variant's hover extends a circuit trace into its panel (Q15: Stack and Tab, not Small)
    SOLTEST_API bool HasHoverTrace(ESOLSolidButtonVariant variant);

    // Returns a text role's font size (SOL::SolidMenu FONT_*_SIZE)
    SOLTEST_API int32 FontSize(ESOLMenuTextRole role);

    // Returns a text role's letter spacing (Title LETTER_SPACING_TITLE, Button LETTER_SPACING_STACK, Section
    // LETTER_SPACING_SMALL_CAPS, the others 0)
    SOLTEST_API int32 LetterSpacing(ESOLMenuTextRole role);

    // Returns the F_SOLMenu typeface name of a weight (SOL::MenuStyle FONT_REGULAR / FONT_BOLD, SOL::SolidMenu
    // FONT_MEDIUM / FONT_SEMIBOLD)
    SOLTEST_API FName TypefaceName(ESOLMenuFontWeight weight);

    // Maps an encoded tint (SOLSolidMenu::EncodeButtonTint) into the vertex tint a hull brush carries: RGB to
    // TINT_VERTEX_FLOOR + (1 - TINT_VERTEX_FLOOR) x channel (each channel clamped to [0, 1], NaN 0), A unchanged
    // (clamped to [0, 1]), so no RGB channel is ever below the floor
    SOLTEST_API FLinearColor PackVertexTint(const FLinearColor& encodedTint);

    // The inverse the hull material applies before SOLSolidMenu::DecodeButtonTint: RGB from the floor back to [0, 1]
    // (clamped), A unchanged; round trip within 1e-5 for channels in [0, 1]
    SOLTEST_API FLinearColor UnpackVertexTint(const FLinearColor& vertexTint);

    // Returns the brush tint of a look: PackVertexTint(SOLSolidMenu::EncodeButtonTint(look))
    SOLTEST_API FLinearColor BrushTint(const FSOLSolidButtonLook& look);

    // Returns a palette colour (sRGB, SOL::SolidMenu) in linear space (FLinearColor::FromSRGBColor, SDD 46 section 5.1)
    SOLTEST_API FLinearColor Linear(const FColor& srgb);

    // The solid text colours (linear; SDD 46 section 5.1, Q12-Q14): body text, the dimmer hints and secondary lines,
    // titles and headings, values / keys / section headings in cyan, warnings
    SOLTEST_API FLinearColor TextColor();
    SOLTEST_API FLinearColor DimTextColor();
    SOLTEST_API FLinearColor BrightTextColor();
    SOLTEST_API FLinearColor ValueColor();
    SOLTEST_API FLinearColor WarningColor();

    // Returns a button label's colour for its look (Q12): DISABLED_TEXT_COLOR when bDisabled, else TEXT_COLOR blended
    // to TEXT_BRIGHT_COLOR by Edge (the hovered / focused label is bright; selected alone stays TEXT_COLOR), linear
    SOLTEST_API FLinearColor LabelColor(const FSOLSolidButtonLook& look);

    // Returns the flat fallback colour of a style for a look (no hull material): Panel HULL_COLOR, Recess, SliderGroove
    // and ToggleSlot BASE_COLOR, the raised styles RAISED_COLOR blended to RAISED_LIFTED_COLOR by Edge, PRESSED_COLOR
    // for bPressedHull, DISABLED_HULL_COLOR for bDisabled (pressed wins over disabled, as the tint's G code); linear,
    // alpha 1 (DISABLED_ALPHA for bDisabled)
    SOLTEST_API FLinearColor FlatColor(ESOLSolidStyle style, const FSOLSolidButtonLook& look);

    // --- Factories (game thread, once per widget) ---

    // Returns a hull brush of a style at an image size (UMG units) carrying a look in its tint (BrushTint); the
    // subsystem's shared material when it has one, else a flat FlatColor brush
    SOLTEST_API FSlateBrush MakeHullBrush(const USOLSolidMenuSubsystem* subsystem, ESOLSolidStyle style,
        const FVector2f& imageSize, const FSOLSolidButtonLook& look);

    // Returns the menu font at a weight and size (F_SOLMenu's typeface, or the engine default font as the fallback)
    SOLTEST_API FSlateFontInfo MenuFont(const USOLSolidMenuSubsystem* subsystem, ESOLMenuFontWeight weight, int32 size);

    // Returns the menu font of a text role (its size and letter spacing) at a weight
    SOLTEST_API FSlateFontInfo MenuFont(const USOLSolidMenuSubsystem* subsystem, ESOLMenuTextRole role,
        ESOLMenuFontWeight weight);

    // Returns a menu button style of a variant: every state brush is the variant's hull with the Normal look (the
    // button's own background colour carries its animated look, USOLMenuButton::RefreshHighlightAt); the disabled
    // brush carries the disabled look; padding SOL::MenuStyle BUTTON_PADDING_X / Y
    SOLTEST_API FButtonStyle MakeButtonStyle(const USOLSolidMenuSubsystem* subsystem, ESOLSolidButtonVariant variant);

    // Returns the slider style: the recessed groove (normal, hovered with the hover look, disabled with the disabled
    // look), the knob (normal, hovered lit, disabled), bar thickness SLIDER_BAR_THICKNESS. Slate dims a disabled
    // slider itself (alpha x 0.45 on material brushes), so the disabled brushes keep alpha 1
    SOLTEST_API FSliderStyle MakeSliderStyle(const USOLSolidMenuSubsystem* subsystem);

    // Returns the toggle style (FCheckBoxStyle, CheckBox type): the toggle slot as the background images and the puck
    // as the check images (unchecked left, checked right with the cyan LED, hovered with the hover look); bDisabled
    // gives every image the disabled look (a check box has no disabled images: the owner swaps the style when it
    // disables the toggle); foreground and border colours white so the tints reach the material unchanged
    SOLTEST_API FCheckBoxStyle MakeCheckBoxStyle(const USOLSolidMenuSubsystem* subsystem, bool bDisabled = false);

    // Returns the scroll bar style: a thin recess (SCROLL_BAR_THICKNESS) and a flat cyan thumb (CYAN_COLOR; hovered
    // and dragged brighter)
    SOLTEST_API FScrollBarStyle MakeScrollBarStyle(const USOLSolidMenuSubsystem* subsystem);
}
