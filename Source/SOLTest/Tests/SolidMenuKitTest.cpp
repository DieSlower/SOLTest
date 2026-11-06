/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuButton.h"
#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSmokeBoard.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "Menu/SOLSolidStyle.h"
#include "Menu/SOLSolidTraceLayer.h"
#include "SOLConstants.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Styling/SlateTypes.h"
#include "UObject/UObjectHash.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #46 part 46c solid menu kit (Docs/SDDs/46-solid-menus.md sections 4, 5, 7, 8, 9 and 10 with
// their 46c amendments): the vertex tint packing, the hull material parameter cache, the SOLSolidStyle decisions and
// factories (flat fallback headless, the material path in a bare game world), the button animation driver
// FSOLSolidButtonAnim, the trace layer's pass alphas and USOLSolidMenuSubsystem. Written against the headers before
// the production code existed; every expected number comes from SOL::SolidMenu, SOL::MenuStyle or SOL::Settings.
// The USOLMenuButton and USOLSolidPanel tests (Button.*, Panel.*) were added after the 46c review.
// Helper names are unique across the test files so unity builds do not collide.
namespace
{
    constexpr float SOLIDKITTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float SOLIDKITTEST_INF = std::numeric_limits<float>::infinity();
    constexpr double SOLIDKITTEST_NAN_D = std::numeric_limits<double>::quiet_NaN();
    constexpr double SOLIDKITTEST_INF_D = std::numeric_limits<double>::infinity();

    // Tolerances
    constexpr float SOLIDKITTEST_TOLERANCE = 1.0e-4f;                          // Look amounts, alphas
    constexpr float SOLIDKITTEST_PACK_TOLERANCE = 1.0e-5f;                     // Pack / unpack round trip (header)
    constexpr float SOLIDKITTEST_COLOR_TOLERANCE = 1.0e-3f;                    // Linear palette colours
    constexpr float SOLIDKITTEST_LINEAR_8BIT_TOLERANCE = 2.0f / 255.0f + 1.0e-5f;  // Encode + linear 8-bit vertex
    constexpr float SOLIDKITTEST_SRGB_8BIT_TOLERANCE = 3.0f / 255.0f + 1.0e-5f;    // Encode + sRGB 8-bit vertex
    constexpr float SOLIDKITTEST_TINT_STEP = 0.5f / 255.0f;                    // Two tints closer than this are equal
    constexpr float SOLIDKITTEST_MAX_DISABLED_CHROMA = 0.1f;                   // Linear max - min RGB: never amber /
                                                                               // cyan (both above 0.8)
    constexpr float SOLIDKITTEST_LIT_EDGE_MIN = 0.5f;                          // A hovered / lit brush's decoded Edge

    // Times (real application seconds) and the strength the animation tests run at
    constexpr double SOLIDKITTEST_SETTLED_S = 5.0;     // Long past every ramp and linger
    constexpr double SOLIDKITTEST_T0 = 100.0;          // First Update of every animation test
    constexpr double SOLIDKITTEST_T1 = 101.0;          // First target change
    constexpr double SOLIDKITTEST_T2 = 102.0;          // Second target change (the press)
    constexpr float SOLIDKITTEST_S = 1.0f;

    // A hull brush's image size for the factory tests (UMG units)
    constexpr float SOLIDKITTEST_BRUSH_W = 240.0f;
    constexpr float SOLIDKITTEST_BRUSH_H = 52.0f;

    constexpr ESOLSolidStyle SOLIDKITTEST_STYLES[] = {
        ESOLSolidStyle::Panel, ESOLSolidStyle::Recess, ESOLSolidStyle::StackButton, ESOLSolidStyle::Tab,
        ESOLSolidStyle::SmallButton, ESOLSolidStyle::SliderGroove, ESOLSolidStyle::Knob, ESOLSolidStyle::ToggleSlot,
        ESOLSolidStyle::Puck };

    // The raised styles (RAISED_COLOR lifting to RAISED_LIFTED_COLOR in the flat fallback)
    constexpr ESOLSolidStyle SOLIDKITTEST_RAISED_STYLES[] = {
        ESOLSolidStyle::StackButton, ESOLSolidStyle::Tab, ESOLSolidStyle::SmallButton, ESOLSolidStyle::Knob,
        ESOLSolidStyle::Puck };

    // The sunken flat styles (BASE_COLOR in the flat fallback)
    constexpr ESOLSolidStyle SOLIDKITTEST_BASE_STYLES[] = {
        ESOLSolidStyle::Recess, ESOLSolidStyle::SliderGroove, ESOLSolidStyle::ToggleSlot };

    // Every style a control can be disabled in (section 5.3: buttons, tabs, sliders, toggles and rows)
    constexpr ESOLSolidStyle SOLIDKITTEST_CONTROL_STYLES[] = {
        ESOLSolidStyle::StackButton, ESOLSolidStyle::Tab, ESOLSolidStyle::SmallButton, ESOLSolidStyle::SliderGroove,
        ESOLSolidStyle::Knob, ESOLSolidStyle::ToggleSlot, ESOLSolidStyle::Puck };

    constexpr ESOLSolidButtonVariant SOLIDKITTEST_VARIANTS[] = {
        ESOLSolidButtonVariant::Stack, ESOLSolidButtonVariant::Tab, ESOLSolidButtonVariant::Small };

    constexpr ESOLMenuFontWeight SOLIDKITTEST_WEIGHTS[] = {
        ESOLMenuFontWeight::Regular, ESOLMenuFontWeight::Medium, ESOLMenuFontWeight::SemiBold,
        ESOLMenuFontWeight::Bold };

    constexpr ESOLMenuTextRole SOLIDKITTEST_ROLES[] = {
        ESOLMenuTextRole::Title, ESOLMenuTextRole::Heading, ESOLMenuTextRole::Section, ESOLMenuTextRole::Button,
        ESOLMenuTextRole::Body, ESOLMenuTextRole::Small };

    // The textures the kit draws itself (the hull material references its grain on its own; the hazard mask is also
    // the main menu's stripe, 46d)
    constexpr ESOLSolidTexture SOLIDKITTEST_TEXTURES[] = { ESOLSolidTexture::Rivet, ESOLSolidTexture::Vent,
        ESOLSolidTexture::Hazard };

    // Counts failures of a sweep and keeps the first one's description (avoids flooding the log)
    struct FSOLSolidKitSweepLog
    {
        int32 Failures = 0;
        FString First;

        // Records a failure, keeping the first description
        void Fail(const FString& what)
        {
            if (Failures == 0)
            {
                First = what;
            }
            ++Failures;
        }

        // Reports the sweep as one assertion
        void Report(FAutomationTestBase& test, const FString& what) const
        {
            test.TestEqual(FString::Printf(TEXT("%s (first failure: %s)"), *what, *First), Failures, 0);
        }
    };

    // A bare game world (no play begun), so USOLSolidMenuSubsystem initializes and loads its assets; destroyed at
    // scope exit
    struct FSOLSolidKitTestWorld
    {
        UWorld* World = nullptr;

        //////////////////////////////////////////////////////////////////////////
        // Creates the world and its context (the world subsystems initialize here)
        FSOLSolidKitTestWorld()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SOLSolidMenuKitTestWorld"));
            FWorldContext& context = GEngine->CreateNewWorldContext(EWorldType::Game);
            context.SetCurrentWorld(World);
        }

        //////////////////////////////////////////////////////////////////////////
        // Destroys the world and its context
        ~FSOLSolidKitTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a palette colour in linear space (SDD 46 section 5.1: converted with FLinearColor::FromSRGBColor)
    FLinearColor SolidKitTestColor(const FColor& srgb)
    {
        return FLinearColor::FromSRGBColor(srgb);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two colours' RGB channels are within a tolerance
    bool SolidKitTestNearRgb(const FLinearColor& a, const FLinearColor& b, const float tolerance)
    {
        return FMath::Abs(a.R - b.R) <= tolerance && FMath::Abs(a.G - b.G) <= tolerance
            && FMath::Abs(a.B - b.B) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two colours' RGBA channels are within a tolerance
    bool SolidKitTestNearRgba(const FLinearColor& a, const FLinearColor& b, const float tolerance)
    {
        return SolidKitTestNearRgb(a, b, tolerance) && FMath::Abs(a.A - b.A) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks two colours' RGBA within a tolerance, describing both on failure
    void SolidKitTestColorEqual(FAutomationTestBase& test, const FString& label, const FLinearColor& actual,
        const FLinearColor& expected, const float tolerance)
    {
        if (!test.TestTrue(label, SolidKitTestNearRgba(actual, expected, tolerance)))
        {
            test.AddError(FString::Printf(TEXT("%s: %s, expected %s"), *label, *actual.ToString(),
                *expected.ToString()));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks two colours' RGB within a tolerance (alpha not compared), describing both on failure
    void SolidKitTestRgbEqual(FAutomationTestBase& test, const FString& label, const FLinearColor& actual,
        const FLinearColor& expected, const float tolerance)
    {
        if (!test.TestTrue(label, SolidKitTestNearRgb(actual, expected, tolerance)))
        {
            test.AddError(FString::Printf(TEXT("%s: %s, expected %s"), *label, *actual.ToString(),
                *expected.ToString()));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the RGB chroma of a linear colour: max channel minus min channel
    float SolidKitTestChroma(const FLinearColor& c)
    {
        return FMath::Max3(c.R, c.G, c.B) - FMath::Min3(c.R, c.G, c.B);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a brush's tint colour
    FLinearColor SolidKitTestTint(const FSlateBrush& brush)
    {
        return brush.TintColor.GetSpecifiedColor();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the look the hull material reads from a brush's tint: unpack, then decode (SDD 46 sections 4.3 and 8.3)
    FSOLSolidButtonLook SolidKitTestDecodeBrush(const FSlateBrush& brush)
    {
        return SOLSolidMenu::DecodeButtonTint(SOLSolidStyle::UnpackVertexTint(SolidKitTestTint(brush)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a brush draws with a material (the hull material path, not the flat fallback)
    bool SolidKitTestIsMaterialBrush(const FSlateBrush& brush)
    {
        return Cast<UMaterialInterface>(brush.GetResourceObject()) != nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two brushes draw differently: another resource, or a tint channel apart by an 8-bit half step
    bool SolidKitTestBrushDiffers(const FSlateBrush& a, const FSlateBrush& b)
    {
        return a.GetResourceObject() != b.GetResourceObject()
            || !SolidKitTestNearRgba(SolidKitTestTint(a), SolidKitTestTint(b), SOLIDKITTEST_TINT_STEP);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a settled look of a state from EvaluateButton (strength SOLIDKITTEST_S)
    FSOLSolidButtonLook SolidKitTestSettled(const ESOLSolidButtonState state, const bool bSelected = false)
    {
        return SOLSolidMenu::EvaluateButton(state, ESOLSolidButtonState::Normal, SOLIDKITTEST_SETTLED_S,
            SOLIDKITTEST_S, bSelected);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns EvaluateButtonFrom at strength SOLIDKITTEST_S (the reference the animation driver must follow)
    FSOLSolidButtonLook SolidKitTestFrom(const ESOLSolidButtonState state, const FSOLSolidButtonLook& atChange,
        const double sinceChangeS, const bool bSelected = false)
    {
        return SOLSolidMenu::EvaluateButtonFrom(state, atChange, sinceChangeS, SOLIDKITTEST_S, bSelected);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a look with only the tint-carried fields set (Edge drives the LED and underline)
    FSOLSolidButtonLook SolidKitTestLook(const float edge, const bool bPressed, const bool bMark, const bool bDisabled)
    {
        FSOLSolidButtonLook look;
        look.Edge = edge;
        look.Led = edge;
        look.Underline = edge;
        look.bPressedHull = bPressed;
        look.bSelectedMark = bMark;
        look.bDisabled = bDisabled;
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two looks match: every amount within a tolerance and every flag equal; describes a difference
    bool SolidKitTestLookMatches(const FSOLSolidButtonLook& a, const FSOLSolidButtonLook& b, const float tolerance,
        FString& outWhy)
    {
        const float amountsA[] = { a.Lift, a.Edge, a.Led, a.Underline, a.Trace, a.Glow };
        const float amountsB[] = { b.Lift, b.Edge, b.Led, b.Underline, b.Trace, b.Glow };
        const TCHAR* names[] = { TEXT("Lift"), TEXT("Edge"), TEXT("Led"), TEXT("Underline"), TEXT("Trace"),
            TEXT("Glow") };
        for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(amountsA)); ++i)
        {
            if (!(FMath::Abs(amountsA[i] - amountsB[i]) <= tolerance))
            {
                outWhy = FString::Printf(TEXT("%s %.5f, expected %.5f"), names[i], amountsA[i], amountsB[i]);
                return false;
            }
        }
        if (a.bPressedHull != b.bPressedHull || a.bSelectedMark != b.bSelectedMark || a.bDisabled != b.bDisabled)
        {
            outWhy = FString::Printf(TEXT("flags pressed %d mark %d disabled %d, expected %d %d %d"),
                a.bPressedHull ? 1 : 0, a.bSelectedMark ? 1 : 0, a.bDisabled ? 1 : 0, b.bPressedHull ? 1 : 0,
                b.bSelectedMark ? 1 : 0, b.bDisabled ? 1 : 0);
            return false;
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a look against an expected one, reporting the first difference as an error
    void SolidKitTestLookEqual(FAutomationTestBase& test, const FString& label, const FSOLSolidButtonLook& actual,
        const FSOLSolidButtonLook& expected)
    {
        FString why;
        if (!test.TestTrue(label, SolidKitTestLookMatches(actual, expected, SOLIDKITTEST_TOLERANCE, why)))
        {
            test.AddError(label + TEXT(": ") + why);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the five animated amounts' largest difference between two looks
    float SolidKitTestMaxAmountDelta(const FSOLSolidButtonLook& a, const FSOLSolidButtonLook& b)
    {
        float delta = FMath::Abs(a.Lift - b.Lift);
        delta = FMath::Max(delta, FMath::Abs(a.Edge - b.Edge));
        delta = FMath::Max(delta, FMath::Abs(a.Led - b.Led));
        delta = FMath::Max(delta, FMath::Abs(a.Underline - b.Underline));
        return FMath::Max(delta, FMath::Abs(a.Trace - b.Trace));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a colour as a linear 8-bit vertex colour would carry it (each channel rounded to k / 255)
    FLinearColor SolidKitTestQuantizeLinear(const FLinearColor& c)
    {
        // Rounds one channel, clamped to [0, 1], to the nearest k / 255
        const auto quantize = [](const float x)
        {
            return FMath::RoundToFloat(FMath::Clamp(x, 0.0f, 1.0f) * 255.0f) / 255.0f;
        };
        return FLinearColor(quantize(c.R), quantize(c.G), quantize(c.B), quantize(c.A));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a colour as Slate's sRGB 8-bit vertex colour carries it back to the shader (RGB sRGB-encoded)
    FLinearColor SolidKitTestQuantizeSrgb(const FLinearColor& c)
    {
        return FLinearColor::FromSRGBColor(c.ToFColorSRGB());
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the brush tint of a look survives the vertex path under one quantization: flags exact, Edge / Led /
    // Underline within a tolerance, every packed RGB channel at or above the floor, A the encoded A
    void SolidKitTestRoundTrip(FSOLSolidKitSweepLog& log, const FString& label, const FSOLSolidButtonLook& look,
        const bool bSrgb)
    {
        const FLinearColor encoded = SOLSolidMenu::EncodeButtonTint(look);
        const FLinearColor packed = SOLSolidStyle::BrushTint(look);
        const float floor = SOL::SolidMenu::TINT_VERTEX_FLOOR;
        if (packed.R < floor || packed.G < floor || packed.B < floor)
        {
            log.Fail(FString::Printf(TEXT("%s: packed %s below the floor"), *label, *packed.ToString()));
        }
        if (FMath::Abs(packed.A - encoded.A) > SOLIDKITTEST_PACK_TOLERANCE)
        {
            log.Fail(FString::Printf(TEXT("%s: packed A %.5f, encoded %.5f"), *label, packed.A, encoded.A));
        }
        const FLinearColor vertex = bSrgb ? SolidKitTestQuantizeSrgb(packed) : SolidKitTestQuantizeLinear(packed);
        const FSOLSolidButtonLook decoded = SOLSolidMenu::DecodeButtonTint(SOLSolidStyle::UnpackVertexTint(vertex));
        const float tolerance = bSrgb ? SOLIDKITTEST_SRGB_8BIT_TOLERANCE : SOLIDKITTEST_LINEAR_8BIT_TOLERANCE;
        const TCHAR* path = bSrgb ? TEXT("sRGB") : TEXT("linear");
        if (decoded.bPressedHull != look.bPressedHull || decoded.bSelectedMark != look.bSelectedMark
            || decoded.bDisabled != look.bDisabled)
        {
            log.Fail(FString::Printf(TEXT("%s (%s): flags pressed %d mark %d disabled %d, expected %d %d %d"), *label,
                path, decoded.bPressedHull ? 1 : 0, decoded.bSelectedMark ? 1 : 0, decoded.bDisabled ? 1 : 0,
                look.bPressedHull ? 1 : 0, look.bSelectedMark ? 1 : 0, look.bDisabled ? 1 : 0));
        }
        if (FMath::Abs(decoded.Edge - look.Edge) > tolerance || FMath::Abs(decoded.Led - look.Edge) > tolerance
            || FMath::Abs(decoded.Underline - look.Edge) > tolerance)
        {
            log.Fail(FString::Printf(TEXT("%s (%s): edge %.5f led %.5f underline %.5f, expected %.5f"), *label, path,
                decoded.Edge, decoded.Led, decoded.Underline, look.Edge));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a disabled brush never shows amber or cyan: through the material it decodes disabled with no edge (and no
    // mark unless bAllowMark: a disabled toggle may keep its puck position); flat, it is not a material and its colour
    // is near grey
    void SolidKitTestDisabledBrush(FAutomationTestBase& test, const FString& label, const FSlateBrush& brush,
        const bool bMaterial, const bool bAllowMark = false)
    {
        if (bMaterial)
        {
            test.TestTrue(label + TEXT(": draws with the hull material"), SolidKitTestIsMaterialBrush(brush));
            const FSOLSolidButtonLook decoded = SolidKitTestDecodeBrush(brush);
            test.TestTrue(label + TEXT(": decodes disabled"), decoded.bDisabled);
            test.TestFalse(label + TEXT(": decodes no pressed hull"), decoded.bPressedHull);
            if (!bAllowMark)
            {
                test.TestFalse(label + TEXT(": decodes no cyan mark"), decoded.bSelectedMark);
            }
            test.TestTrue(label + TEXT(": decodes no amber edge, LED or underline"),
                decoded.Edge <= SOLIDKITTEST_LINEAR_8BIT_TOLERANCE);
        }
        else
        {
            test.TestFalse(label + TEXT(": flat (no material)"), SolidKitTestIsMaterialBrush(brush));
            const FLinearColor tint = SolidKitTestTint(brush);
            if (!test.TestTrue(label + TEXT(": near grey, never amber or cyan"),
                SolidKitTestChroma(tint) <= SOLIDKITTEST_MAX_DISABLED_CHROMA))
            {
                test.AddError(label + TEXT(": tint ") + tint.ToString());
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a brush differs from another, naming both
    void SolidKitTestDiffers(FAutomationTestBase& test, const FString& label, const FSlateBrush& a,
        const FSlateBrush& b)
    {
        if (!test.TestTrue(label, SolidKitTestBrushDiffers(a, b)))
        {
            test.AddError(FString::Printf(TEXT("%s: both tint %s"), *label, *SolidKitTestTint(a).ToString()));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the style factories' disabled art and sizes for a subsystem; bMaterial says whether the hull materials
    // are expected (a loaded subsystem) or the flat fallback (null or bare)
    void SolidKitTestFactories(FAutomationTestBase& test, const FString& label,
        const USOLSolidMenuSubsystem* subsystem, const bool bMaterial)
    {
        using namespace SOL::SolidMenu;

        // Button styles: the disabled brush differs from normal and hovered and never shows amber or cyan
        for (const ESOLSolidButtonVariant variant : SOLIDKITTEST_VARIANTS)
        {
            const FString name = FString::Printf(TEXT("%s: button variant %d"), *label, static_cast<int32>(variant));
            const FButtonStyle style = SOLSolidStyle::MakeButtonStyle(subsystem, variant);
            SolidKitTestDiffers(test, name + TEXT(": disabled differs from normal"), style.Disabled, style.Normal);
            SolidKitTestDiffers(test, name + TEXT(": disabled differs from hovered"), style.Disabled, style.Hovered);
            SolidKitTestDisabledBrush(test, name + TEXT(": disabled"), style.Disabled, bMaterial);
            test.TestTrue(name + TEXT(": padding BUTTON_PADDING_X / Y"), style.NormalPadding
                == FMargin(SOL::MenuStyle::BUTTON_PADDING_X, SOL::MenuStyle::BUTTON_PADDING_Y));
        }

        // Slider: distinct disabled groove and knob at alpha 1 (Slate's own disabled dim is the dim)
        {
            const FString name = label + TEXT(": slider");
            const FSliderStyle style = SOLSolidStyle::MakeSliderStyle(subsystem);
            SolidKitTestDiffers(test, name + TEXT(": disabled bar differs from normal"), style.DisabledBarImage,
                style.NormalBarImage);
            SolidKitTestDiffers(test, name + TEXT(": disabled bar differs from hovered"), style.DisabledBarImage,
                style.HoveredBarImage);
            SolidKitTestDiffers(test, name + TEXT(": disabled thumb differs from normal"), style.DisabledThumbImage,
                style.NormalThumbImage);
            SolidKitTestDiffers(test, name + TEXT(": disabled thumb differs from hovered"), style.DisabledThumbImage,
                style.HoveredThumbImage);
            SolidKitTestDisabledBrush(test, name + TEXT(": disabled bar"), style.DisabledBarImage, bMaterial);
            SolidKitTestDisabledBrush(test, name + TEXT(": disabled thumb"), style.DisabledThumbImage, bMaterial);
            test.TestEqual(name + TEXT(": disabled bar alpha 1"), SolidKitTestTint(style.DisabledBarImage).A, 1.0f,
                SOLIDKITTEST_TOLERANCE);
            test.TestEqual(name + TEXT(": disabled thumb alpha 1"), SolidKitTestTint(style.DisabledThumbImage).A,
                1.0f, SOLIDKITTEST_TOLERANCE);
            test.TestEqual(name + TEXT(": bar thickness"), style.BarThickness, SLIDER_BAR_THICKNESS);
        }

        // Toggle: a check box style; the disabled variant differs image for image and keeps alpha 1
        {
            const FString name = label + TEXT(": toggle");
            const FCheckBoxStyle enabled = SOLSolidStyle::MakeCheckBoxStyle(subsystem, false);
            const FCheckBoxStyle disabled = SOLSolidStyle::MakeCheckBoxStyle(subsystem, true);
            test.TestTrue(name + TEXT(": CheckBox type"), enabled.CheckBoxType == ESlateCheckBoxType::CheckBox);
            test.TestTrue(name + TEXT(": disabled CheckBox type"),
                disabled.CheckBoxType == ESlateCheckBoxType::CheckBox);
            SolidKitTestColorEqual(test, name + TEXT(": foreground white"),
                enabled.ForegroundColor.GetSpecifiedColor(), FLinearColor::White, SOLIDKITTEST_TOLERANCE);
            SolidKitTestColorEqual(test, name + TEXT(": border background white"),
                enabled.BorderBackgroundColor.GetSpecifiedColor(), FLinearColor::White, SOLIDKITTEST_TOLERANCE);

            const FSlateBrush* enabledImages[] = { &enabled.UncheckedImage, &enabled.UncheckedHoveredImage,
                &enabled.CheckedImage, &enabled.CheckedHoveredImage, &enabled.BackgroundImage,
                &enabled.BackgroundHoveredImage };
            const FSlateBrush* disabledImages[] = { &disabled.UncheckedImage, &disabled.UncheckedHoveredImage,
                &disabled.CheckedImage, &disabled.CheckedHoveredImage, &disabled.BackgroundImage,
                &disabled.BackgroundHoveredImage };
            const TCHAR* imageNames[] = { TEXT("unchecked"), TEXT("unchecked hovered"), TEXT("checked"),
                TEXT("checked hovered"), TEXT("background"), TEXT("background hovered") };
            for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(imageNames)); ++i)
            {
                const FString image = name + TEXT(": ") + imageNames[i];
                SolidKitTestDiffers(test, image + TEXT(": disabled differs from enabled"), *disabledImages[i],
                    *enabledImages[i]);
                SolidKitTestDisabledBrush(test, image + TEXT(": disabled"), *disabledImages[i], bMaterial, true);
                test.TestEqual(image + TEXT(": disabled alpha 1"), SolidKitTestTint(*disabledImages[i]).A, 1.0f,
                    SOLIDKITTEST_TOLERANCE);
            }
        }

        // Scroll bar: thin, a flat cyan thumb, hovered and dragged brighter
        {
            const FString name = label + TEXT(": scroll bar");
            const FScrollBarStyle style = SOLSolidStyle::MakeScrollBarStyle(subsystem);
            test.TestEqual(name + TEXT(": thickness"), style.Thickness, SCROLL_BAR_THICKNESS);
            const FLinearColor normal = SolidKitTestTint(style.NormalThumbImage);
            SolidKitTestRgbEqual(test, name + TEXT(": thumb CYAN_COLOR"), normal, SolidKitTestColor(CYAN_COLOR),
                SOLIDKITTEST_COLOR_TOLERANCE);
            test.TestTrue(name + TEXT(": hovered thumb brighter"),
                SolidKitTestTint(style.HoveredThumbImage).GetLuminance() > normal.GetLuminance());
            test.TestTrue(name + TEXT(": dragged thumb brighter"),
                SolidKitTestTint(style.DraggedThumbImage).GetLuminance() > normal.GetLuminance());
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a scalar parameter of a material (0 for a null material)
    float SolidKitTestScalar(UMaterialInstanceDynamic* material, const TCHAR* name)
    {
        return material != nullptr ? material->K2_GetScalarParameterValue(FName(name)) : 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitPackTest, "SOLTest.SolidMenuKit.Tint.PackIntoFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PackVertexTint maps RGB to TINT_VERTEX_FLOOR + (1 - floor) x channel (clamped to [0, 1], NaN 0), A unchanged but
// clamped, so no RGB channel is ever below the floor
bool FSOLSolidKitPackTest::RunTest(const FString& /*parameters*/)
{
    const float floor = SOL::SolidMenu::TINT_VERTEX_FLOOR;
    TestTrue(TEXT("floor in (0, 1)"), floor > 0.0f && floor < 1.0f);

    // Endpoints and the linear map
    SolidKitTestColorEqual(*this, TEXT("black -> the floor"), SOLSolidStyle::PackVertexTint(FLinearColor(0, 0, 0, 1)),
        FLinearColor(floor, floor, floor, 1.0f), SOLIDKITTEST_PACK_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("white -> white"), SOLSolidStyle::PackVertexTint(FLinearColor(1, 1, 1, 1)),
        FLinearColor::White, SOLIDKITTEST_PACK_TOLERANCE);
    const float half = floor + (1.0f - floor) * 0.5f;
    const float quarter = floor + (1.0f - floor) * 0.25f;
    SolidKitTestColorEqual(*this, TEXT("channels mapped independently"),
        SOLSolidStyle::PackVertexTint(FLinearColor(0.5f, 0.25f, 1.0f, 0.4f)),
        FLinearColor(half, quarter, 1.0f, 0.4f), SOLIDKITTEST_PACK_TOLERANCE);

    // Out-of-range and NaN channels: RGB clamped (NaN 0), A clamped
    SolidKitTestColorEqual(*this, TEXT("RGB clamped, NaN reads 0"),
        SOLSolidStyle::PackVertexTint(FLinearColor(-1.0f, 2.0f, SOLIDKITTEST_NAN, 0.3f)),
        FLinearColor(floor, 1.0f, floor, 0.3f), SOLIDKITTEST_PACK_TOLERANCE);
    TestEqual(TEXT("A above 1 clamped"), SOLSolidStyle::PackVertexTint(FLinearColor(0, 0, 0, 2.0f)).A, 1.0f);
    TestEqual(TEXT("A below 0 clamped"), SOLSolidStyle::PackVertexTint(FLinearColor(0, 0, 0, -1.0f)).A, 0.0f);
    TestEqual(TEXT("A in range unchanged"), SOLSolidStyle::PackVertexTint(FLinearColor(0, 0, 0,
        SOL::SolidMenu::DISABLED_ALPHA)).A, SOL::SolidMenu::DISABLED_ALPHA);
    TestEqual(TEXT("+inf R reads 1"), SOLSolidStyle::PackVertexTint(FLinearColor(SOLIDKITTEST_INF, 0, 0, 1)).R, 1.0f);
    TestEqual(TEXT("-inf G reads the floor"), SOLSolidStyle::PackVertexTint(FLinearColor(0, -SOLIDKITTEST_INF, 0, 1)).G,
        floor);

    // Sweep: every packed RGB channel sits in [floor, 1] whatever the input
    FSOLSolidKitSweepLog log;
    const float inputs[] = { -100.0f, -1.0f, -1.0e-6f, 0.0f, 1.0e-6f, 0.1f, 0.33f, 0.5f, 0.75f, 0.999f, 1.0f, 1.5f,
        100.0f, SOLIDKITTEST_NAN, SOLIDKITTEST_INF, -SOLIDKITTEST_INF };
    for (const float x : inputs)
    {
        const FLinearColor packed = SOLSolidStyle::PackVertexTint(FLinearColor(x, x, x, 1.0f));
        const bool bInRange = packed.R >= floor && packed.R <= 1.0f && packed.G >= floor && packed.G <= 1.0f
            && packed.B >= floor && packed.B <= 1.0f;
        if (!bInRange)
        {
            log.Fail(FString::Printf(TEXT("input %f -> %s"), x, *packed.ToString()));
        }
    }
    log.Report(*this, TEXT("packed RGB always in [floor, 1]"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitUnpackTest, "SOLTest.SolidMenuKit.Tint.UnpackIsTheInverse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// UnpackVertexTint maps RGB from the floor back to [0, 1] (clamped), A unchanged; Unpack(Pack(c)) is c within 1e-5
// for channels in [0, 1]
bool FSOLSolidKitUnpackTest::RunTest(const FString& /*parameters*/)
{
    const float floor = SOL::SolidMenu::TINT_VERTEX_FLOOR;
    SolidKitTestColorEqual(*this, TEXT("the floor -> 0"),
        SOLSolidStyle::UnpackVertexTint(FLinearColor(floor, floor, floor, 1.0f)), FLinearColor(0, 0, 0, 1),
        SOLIDKITTEST_PACK_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("white -> white"), SOLSolidStyle::UnpackVertexTint(FLinearColor::White),
        FLinearColor::White, SOLIDKITTEST_PACK_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("below the floor clamps to 0, above 1 clamps to 1, A unchanged"),
        SOLSolidStyle::UnpackVertexTint(FLinearColor(0.1f, 1.5f, 0.0f, 0.4f)), FLinearColor(0.0f, 1.0f, 0.0f, 0.4f),
        SOLIDKITTEST_PACK_TOLERANCE);

    // Round trip over a fine grid and every 8-bit level
    FSOLSolidKitSweepLog log;
    for (int32 k = 0; k <= 255; ++k)
    {
        const float x = static_cast<float>(k) / 255.0f;
        const float y = 1.0f - x;
        const float z = FMath::Frac(x * 7.0f);
        const FLinearColor input(x, y, z, x);
        const FLinearColor back = SOLSolidStyle::UnpackVertexTint(SOLSolidStyle::PackVertexTint(input));
        if (!SolidKitTestNearRgba(back, input, SOLIDKITTEST_PACK_TOLERANCE))
        {
            log.Fail(FString::Printf(TEXT("%s -> %s"), *input.ToString(), *back.ToString()));
        }
    }
    log.Report(*this, TEXT("Unpack(Pack(c)) == c within 1e-5"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitRoundTripTest, "SOLTest.SolidMenuKit.Tint.BrushTintRoundTripEveryState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BrushTint is PackVertexTint(EncodeButtonTint(look)); through an 8-bit vertex colour (linear or sRGB) and the
// material's unpack + decode, every state keeps its flags and its Edge within the quantization
bool FSOLSolidKitRoundTripTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    const double midRamp = 0.37 * HOVER_RAMP_S;

    // Looks of every state, settled and mid-ramp, with and without the selected flag
    struct FSOLSolidKitNamedLook
    {
        const TCHAR* Name;
        FSOLSolidButtonLook Look;
    };
    const FSOLSolidKitNamedLook looks[] = {
        { TEXT("Normal"), SolidKitTestSettled(ESOLSolidButtonState::Normal) },
        { TEXT("Normal + selected"), SolidKitTestSettled(ESOLSolidButtonState::Normal, true) },
        { TEXT("Hovered"), SolidKitTestSettled(ESOLSolidButtonState::Hovered) },
        { TEXT("Hovered + selected"), SolidKitTestSettled(ESOLSolidButtonState::Hovered, true) },
        { TEXT("Hovered mid-ramp"), SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Hovered,
            ESOLSolidButtonState::Normal, midRamp, SOLIDKITTEST_S) },
        { TEXT("Hovered + selected mid-ramp"), SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Hovered,
            ESOLSolidButtonState::Normal, midRamp, SOLIDKITTEST_S, true) },
        { TEXT("leaving Hovered mid-ramp"), SOLSolidMenu::EvaluateButton(ESOLSolidButtonState::Normal,
            ESOLSolidButtonState::Hovered, 0.6 * HOVER_RAMP_S, SOLIDKITTEST_S) },
        { TEXT("Pressed"), SolidKitTestSettled(ESOLSolidButtonState::Pressed) },
        { TEXT("Pressed + selected"), SolidKitTestSettled(ESOLSolidButtonState::Pressed, true) },
        { TEXT("Selected"), SolidKitTestSettled(ESOLSolidButtonState::Selected) },
        { TEXT("Disabled"), SolidKitTestSettled(ESOLSolidButtonState::Disabled) },
        { TEXT("Disabled + selected (ignored)"), SolidKitTestSettled(ESOLSolidButtonState::Disabled, true) },
    };

    // BrushTint is exactly the pack of the encoding
    for (const FSOLSolidKitNamedLook& named : looks)
    {
        SolidKitTestColorEqual(*this, FString::Printf(TEXT("%s: BrushTint = Pack(Encode)"), named.Name),
            SOLSolidStyle::BrushTint(named.Look),
            SOLSolidStyle::PackVertexTint(SOLSolidMenu::EncodeButtonTint(named.Look)), 0.0f);
    }

    // Every state through both vertex paths
    FSOLSolidKitSweepLog log;
    for (const FSOLSolidKitNamedLook& named : looks)
    {
        SolidKitTestRoundTrip(log, named.Name, named.Look, false);
        SolidKitTestRoundTrip(log, named.Name, named.Look, true);
    }

    // Every 8-bit Edge level with every flag combination a look can carry
    const bool flagSets[][3] = { { false, false, false }, { true, false, false }, { false, true, false },
        { true, true, false }, { false, false, true } };
    for (int32 k = 0; k <= 255; ++k)
    {
        for (const auto& flags : flagSets)
        {
            const float edge = flags[2] ? 0.0f : static_cast<float>(k) / 255.0f;
            const FSOLSolidButtonLook look = SolidKitTestLook(edge, flags[0], flags[1], flags[2]);
            const FString name = FString::Printf(TEXT("edge %d/255 pressed %d mark %d disabled %d"), k,
                flags[0] ? 1 : 0, flags[1] ? 1 : 0, flags[2] ? 1 : 0);
            SolidKitTestRoundTrip(log, name, look, false);
            SolidKitTestRoundTrip(log, name, look, true);
        }
    }
    log.Report(*this, TEXT("every look survives the 8-bit vertex path"));

    // The disabled tint's dim: A is DISABLED_ALPHA, the enabled ones 1
    TestEqual(TEXT("disabled tint A"), SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Disabled)).A,
        DISABLED_ALPHA, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("hovered tint A"), SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Hovered)).A,
        1.0f, SOLIDKITTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitNormalTintTest, "SOLTest.SolidMenuKit.Tint.NormalNeverZeroNeverWhite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Normal look's brush tint is the floor (never black, so the hull is not drawn black) and never white (white reads
// as pressed, selected and lit); only the lit pressed selected look packs to white; distinct states differ
bool FSOLSolidKitNormalTintTest::RunTest(const FString& /*parameters*/)
{
    const float floor = SOL::SolidMenu::TINT_VERTEX_FLOOR;
    const FLinearColor normal = SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Normal));
    SolidKitTestColorEqual(*this, TEXT("Normal: RGB at the floor, A 1"), normal, FLinearColor(floor, floor, floor, 1),
        SOLIDKITTEST_PACK_TOLERANCE);
    TestTrue(TEXT("Normal: never zero"), normal.R > 0.0f && normal.G > 0.0f && normal.B > 0.0f);
    TestFalse(TEXT("Normal: never white"), SolidKitTestNearRgb(normal, FLinearColor::White, SOLIDKITTEST_TINT_STEP));
    SolidKitTestColorEqual(*this, TEXT("the default look packs as Normal"), SOLSolidStyle::BrushTint(
        FSOLSolidButtonLook()), normal, SOLIDKITTEST_PACK_TOLERANCE);

    // White is the lit pressed selected look, nothing else of the states
    const FLinearColor pressedSelected = SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Pressed,
        true));
    SolidKitTestColorEqual(*this, TEXT("pressed + selected + lit packs to white"), pressedSelected,
        FLinearColor::White, SOLIDKITTEST_PACK_TOLERANCE);
    const ESOLSolidButtonState others[] = { ESOLSolidButtonState::Normal, ESOLSolidButtonState::Hovered,
        ESOLSolidButtonState::Selected, ESOLSolidButtonState::Disabled, ESOLSolidButtonState::Pressed };
    for (const ESOLSolidButtonState state : others)
    {
        const FLinearColor tint = SOLSolidStyle::BrushTint(SolidKitTestSettled(state));
        TestFalse(FString::Printf(TEXT("state %d unselected: not white"), static_cast<int32>(state)),
            SolidKitTestNearRgb(tint, FLinearColor::White, SOLIDKITTEST_TINT_STEP));
    }

    // Distinct states give distinct tints
    const FLinearColor tints[] = { normal,
        SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Hovered)),
        SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Pressed)),
        SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Selected)),
        SOLSolidStyle::BrushTint(SolidKitTestSettled(ESOLSolidButtonState::Disabled)) };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(tints)); ++i)
    {
        for (int32 j = i + 1; j < static_cast<int32>(UE_ARRAY_COUNT(tints)); ++j)
        {
            TestFalse(FString::Printf(TEXT("states %d and %d differ"), i, j),
                SolidKitTestNearRgba(tints[i], tints[j], SOLIDKITTEST_TINT_STEP));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitVariantTest, "SOLTest.SolidMenuKit.Style.ButtonVariants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ButtonStyleOf maps Stack -> StackButton, Tab -> Tab, Small -> SmallButton; only Stack and Tab extend a hover trace
bool FSOLSolidKitVariantTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("Stack -> StackButton"),
        SOLSolidStyle::ButtonStyleOf(ESOLSolidButtonVariant::Stack) == ESOLSolidStyle::StackButton);
    TestTrue(TEXT("Tab -> Tab"), SOLSolidStyle::ButtonStyleOf(ESOLSolidButtonVariant::Tab) == ESOLSolidStyle::Tab);
    TestTrue(TEXT("Small -> SmallButton"),
        SOLSolidStyle::ButtonStyleOf(ESOLSolidButtonVariant::Small) == ESOLSolidStyle::SmallButton);
    TestTrue(TEXT("Stack has a hover trace"), SOLSolidStyle::HasHoverTrace(ESOLSolidButtonVariant::Stack));
    TestTrue(TEXT("Tab has a hover trace"), SOLSolidStyle::HasHoverTrace(ESOLSolidButtonVariant::Tab));
    TestFalse(TEXT("Small has no hover trace"), SOLSolidStyle::HasHoverTrace(ESOLSolidButtonVariant::Small));
    TestEqual(TEXT("one material path per style"),
        static_cast<int32>(UE_ARRAY_COUNT(SOL::Paths::SOLID_STYLE_MATERIALS)), static_cast<int32>(ESOLSolidStyle::Count));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitFontRoleTest, "SOLTest.SolidMenuKit.Style.FontRoles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FontSize, LetterSpacing and TypefaceName give the SOL::SolidMenu / SOL::MenuStyle values per role and weight
bool FSOLSolidKitFontRoleTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    TestEqual(TEXT("Title size"), SOLSolidStyle::FontSize(ESOLMenuTextRole::Title), FONT_TITLE_SIZE);
    TestEqual(TEXT("Heading size"), SOLSolidStyle::FontSize(ESOLMenuTextRole::Heading), FONT_HEADING_SIZE);
    TestEqual(TEXT("Section size"), SOLSolidStyle::FontSize(ESOLMenuTextRole::Section), FONT_SECTION_SIZE);
    TestEqual(TEXT("Button size"), SOLSolidStyle::FontSize(ESOLMenuTextRole::Button), FONT_BUTTON_SIZE);
    TestEqual(TEXT("Body size"), SOLSolidStyle::FontSize(ESOLMenuTextRole::Body), FONT_BODY_SIZE);
    TestEqual(TEXT("Small size"), SOLSolidStyle::FontSize(ESOLMenuTextRole::Small), FONT_SMALL_SIZE);

    TestEqual(TEXT("Title spacing"), SOLSolidStyle::LetterSpacing(ESOLMenuTextRole::Title), LETTER_SPACING_TITLE);
    TestEqual(TEXT("Button spacing"), SOLSolidStyle::LetterSpacing(ESOLMenuTextRole::Button), LETTER_SPACING_STACK);
    TestEqual(TEXT("Section spacing"), SOLSolidStyle::LetterSpacing(ESOLMenuTextRole::Section),
        LETTER_SPACING_SMALL_CAPS);
    TestEqual(TEXT("Heading spacing 0"), SOLSolidStyle::LetterSpacing(ESOLMenuTextRole::Heading), 0);
    TestEqual(TEXT("Body spacing 0"), SOLSolidStyle::LetterSpacing(ESOLMenuTextRole::Body), 0);
    TestEqual(TEXT("Small spacing 0"), SOLSolidStyle::LetterSpacing(ESOLMenuTextRole::Small), 0);

    TestTrue(TEXT("Regular typeface"),
        SOLSolidStyle::TypefaceName(ESOLMenuFontWeight::Regular) == FName(SOL::MenuStyle::FONT_REGULAR));
    TestTrue(TEXT("Bold typeface"),
        SOLSolidStyle::TypefaceName(ESOLMenuFontWeight::Bold) == FName(SOL::MenuStyle::FONT_BOLD));
    TestTrue(TEXT("Medium typeface"), SOLSolidStyle::TypefaceName(ESOLMenuFontWeight::Medium) == FName(FONT_MEDIUM));
    TestTrue(TEXT("SemiBold typeface"),
        SOLSolidStyle::TypefaceName(ESOLMenuFontWeight::SemiBold) == FName(FONT_SEMIBOLD));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitLabelColorTest, "SOLTest.SolidMenuKit.Style.LabelColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LabelColor: DISABLED_TEXT_COLOR when disabled (whatever the edge), else TEXT_COLOR blended linearly to
// TEXT_BRIGHT_COLOR by Edge; selected alone stays TEXT_COLOR
bool FSOLSolidKitLabelColorTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    const FLinearColor text = SolidKitTestColor(TEXT_COLOR);
    const FLinearColor bright = SolidKitTestColor(TEXT_BRIGHT_COLOR);
    const FLinearColor disabled = SolidKitTestColor(DISABLED_TEXT_COLOR);

    SolidKitTestColorEqual(*this, TEXT("Normal: TEXT_COLOR"),
        SOLSolidStyle::LabelColor(SolidKitTestSettled(ESOLSolidButtonState::Normal)), text,
        SOLIDKITTEST_COLOR_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("Hovered: TEXT_BRIGHT_COLOR"),
        SOLSolidStyle::LabelColor(SolidKitTestSettled(ESOLSolidButtonState::Hovered)), bright,
        SOLIDKITTEST_COLOR_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("Hovered + selected: TEXT_BRIGHT_COLOR"),
        SOLSolidStyle::LabelColor(SolidKitTestSettled(ESOLSolidButtonState::Hovered, true)), bright,
        SOLIDKITTEST_COLOR_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("Selected alone: TEXT_COLOR"),
        SOLSolidStyle::LabelColor(SolidKitTestSettled(ESOLSolidButtonState::Selected)), text,
        SOLIDKITTEST_COLOR_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("Edge 0.5: the linear midpoint"),
        SOLSolidStyle::LabelColor(SolidKitTestLook(0.5f, false, false, false)), FMath::Lerp(text, bright, 0.5f),
        SOLIDKITTEST_COLOR_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("Disabled: DISABLED_TEXT_COLOR"),
        SOLSolidStyle::LabelColor(SolidKitTestSettled(ESOLSolidButtonState::Disabled)), disabled,
        SOLIDKITTEST_COLOR_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("Disabled with a full edge: still DISABLED_TEXT_COLOR"),
        SOLSolidStyle::LabelColor(SolidKitTestLook(1.0f, false, true, true)), disabled, SOLIDKITTEST_COLOR_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitFlatColorTest, "SOLTest.SolidMenuKit.Style.FlatColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FlatColor: Panel HULL_COLOR, sunken styles BASE_COLOR, raised styles RAISED_COLOR blended to RAISED_LIFTED_COLOR by
// Edge, PRESSED_COLOR when pressed, DISABLED_HULL_COLOR at DISABLED_ALPHA when disabled (pressed wins)
bool FSOLSolidKitFlatColorTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    const FLinearColor hull = SolidKitTestColor(HULL_COLOR);
    const FLinearColor base = SolidKitTestColor(BASE_COLOR);
    const FLinearColor raised = SolidKitTestColor(RAISED_COLOR);
    const FLinearColor lifted = SolidKitTestColor(RAISED_LIFTED_COLOR);
    const FLinearColor pressed = SolidKitTestColor(PRESSED_COLOR);
    FLinearColor disabled = SolidKitTestColor(DISABLED_HULL_COLOR);
    disabled.A = DISABLED_ALPHA;

    const FSOLSolidButtonLook rest = SolidKitTestSettled(ESOLSolidButtonState::Normal);
    const FSOLSolidButtonLook lit = SolidKitTestLook(1.0f, false, false, false);
    const FSOLSolidButtonLook halfLit = SolidKitTestLook(0.5f, false, false, false);
    const FSOLSolidButtonLook pressedLook = SolidKitTestSettled(ESOLSolidButtonState::Pressed);
    const FSOLSolidButtonLook disabledLook = SolidKitTestSettled(ESOLSolidButtonState::Disabled);
    const FSOLSolidButtonLook pressedDisabled = SolidKitTestLook(1.0f, true, false, true);

    // Panel and the sunken styles ignore the edge
    SolidKitTestColorEqual(*this, TEXT("Panel at rest"), SOLSolidStyle::FlatColor(ESOLSolidStyle::Panel, rest), hull,
        SOLIDKITTEST_COLOR_TOLERANCE);
    SolidKitTestColorEqual(*this, TEXT("Panel lit"), SOLSolidStyle::FlatColor(ESOLSolidStyle::Panel, lit), hull,
        SOLIDKITTEST_COLOR_TOLERANCE);
    for (const ESOLSolidStyle style : SOLIDKITTEST_BASE_STYLES)
    {
        const FString name = FString::Printf(TEXT("sunken style %d"), static_cast<int32>(style));
        SolidKitTestColorEqual(*this, name + TEXT(" at rest: BASE_COLOR"), SOLSolidStyle::FlatColor(style, rest), base,
            SOLIDKITTEST_COLOR_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(" lit: BASE_COLOR"), SOLSolidStyle::FlatColor(style, lit), base,
            SOLIDKITTEST_COLOR_TOLERANCE);
    }

    // Raised styles lift with the edge and darken when pressed
    for (const ESOLSolidStyle style : SOLIDKITTEST_RAISED_STYLES)
    {
        const FString name = FString::Printf(TEXT("raised style %d"), static_cast<int32>(style));
        SolidKitTestColorEqual(*this, name + TEXT(" at rest: RAISED_COLOR"), SOLSolidStyle::FlatColor(style, rest),
            raised, SOLIDKITTEST_COLOR_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(" lit: RAISED_LIFTED_COLOR"), SOLSolidStyle::FlatColor(style, lit),
            lifted, SOLIDKITTEST_COLOR_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(" half lit: the linear midpoint"),
            SOLSolidStyle::FlatColor(style, halfLit), FMath::Lerp(raised, lifted, 0.5f), SOLIDKITTEST_COLOR_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(" pressed: PRESSED_COLOR"),
            SOLSolidStyle::FlatColor(style, pressedLook), pressed, SOLIDKITTEST_COLOR_TOLERANCE);
        SolidKitTestRgbEqual(*this, name + TEXT(" pressed and disabled: pressed wins"),
            SOLSolidStyle::FlatColor(style, pressedDisabled), pressed, SOLIDKITTEST_COLOR_TOLERANCE);
    }

    // Every control style has the disabled hull at the disabled dim
    for (const ESOLSolidStyle style : SOLIDKITTEST_CONTROL_STYLES)
    {
        const FString name = FString::Printf(TEXT("control style %d disabled"), static_cast<int32>(style));
        SolidKitTestColorEqual(*this, name + TEXT(": DISABLED_HULL_COLOR at DISABLED_ALPHA"),
            SOLSolidStyle::FlatColor(style, disabledLook), disabled, SOLIDKITTEST_COLOR_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitSanitizeTest, "SOLTest.SolidMenuKit.ParamCache.SanitizeAndDpi",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SanitizeStrength: NaN reads 0, clamped to [HUD_EFFECTS_MIN, HUD_EFFECTS_MAX]; IsValidDpiScale: finite and positive
bool FSOLSolidKitSanitizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Settings;
    TestEqual(TEXT("1 kept"), FSOLSolidMenuParamCache::SanitizeStrength(1.0f), 1.0f);
    TestEqual(TEXT("0.5 kept"), FSOLSolidMenuParamCache::SanitizeStrength(0.5f), 0.5f);
    TestEqual(TEXT("0 kept"), FSOLSolidMenuParamCache::SanitizeStrength(0.0f), 0.0f);
    TestEqual(TEXT("2 kept"), FSOLSolidMenuParamCache::SanitizeStrength(2.0f), HUD_EFFECTS_MAX);
    TestEqual(TEXT("-1 reads the minimum"), FSOLSolidMenuParamCache::SanitizeStrength(-1.0f), HUD_EFFECTS_MIN);
    TestEqual(TEXT("3 reads the maximum"), FSOLSolidMenuParamCache::SanitizeStrength(3.0f), HUD_EFFECTS_MAX);
    TestEqual(TEXT("NaN reads 0"), FSOLSolidMenuParamCache::SanitizeStrength(SOLIDKITTEST_NAN), 0.0f);
    TestEqual(TEXT("+inf reads the maximum"), FSOLSolidMenuParamCache::SanitizeStrength(SOLIDKITTEST_INF),
        HUD_EFFECTS_MAX);
    TestEqual(TEXT("-inf reads the minimum"), FSOLSolidMenuParamCache::SanitizeStrength(-SOLIDKITTEST_INF),
        HUD_EFFECTS_MIN);

    TestTrue(TEXT("DPI 1 valid"), FSOLSolidMenuParamCache::IsValidDpiScale(1.0f));
    TestTrue(TEXT("DPI 0.5 valid"), FSOLSolidMenuParamCache::IsValidDpiScale(0.5f));
    TestTrue(TEXT("DPI 2.25 valid"), FSOLSolidMenuParamCache::IsValidDpiScale(2.25f));
    TestFalse(TEXT("DPI 0 invalid"), FSOLSolidMenuParamCache::IsValidDpiScale(0.0f));
    TestFalse(TEXT("DPI -1 invalid"), FSOLSolidMenuParamCache::IsValidDpiScale(-1.0f));
    TestFalse(TEXT("DPI NaN invalid"), FSOLSolidMenuParamCache::IsValidDpiScale(SOLIDKITTEST_NAN));
    TestFalse(TEXT("DPI +inf invalid"), FSOLSolidMenuParamCache::IsValidDpiScale(SOLIDKITTEST_INF));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitPushTest, "SOLTest.SolidMenuKit.ParamCache.PushOnlyOnChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Update asks for a push on the first call and on an exact change of the sanitized strength or of a valid DPI scale;
// an invalid DPI scale keeps the last valid one (1 before any)
bool FSOLSolidKitPushTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Settings;

    // Before any call
    FSOLSolidMenuParamCache cache;
    TestEqual(TEXT("fresh strength: the default"), cache.GetStrength(), HUD_EFFECTS_DEFAULT);
    TestEqual(TEXT("fresh DPI scale 1"), cache.GetDpiScale(), 1.0f);
    TestEqual(TEXT("fresh: no push"), cache.GetPushCount(), 0);

    // The first call always pushes, even with the default values
    TestTrue(TEXT("first call pushes (values equal to the defaults)"), cache.Update(HUD_EFFECTS_DEFAULT, 1.0f));
    TestFalse(TEXT("same values: no push"), cache.Update(HUD_EFFECTS_DEFAULT, 1.0f));
    TestTrue(TEXT("strength change pushes"), cache.Update(0.5f, 1.0f));
    TestEqual(TEXT("strength stored"), cache.GetStrength(), 0.5f);
    TestTrue(TEXT("DPI change pushes"), cache.Update(0.5f, 1.5f));
    TestEqual(TEXT("DPI stored"), cache.GetDpiScale(), 1.5f);
    TestFalse(TEXT("the same again: no push"), cache.Update(0.5f, 1.5f));
    TestEqual(TEXT("3 pushes"), cache.GetPushCount(), 3);

    // Invalid DPI scales keep the last valid one and never push on their own
    const float badDpi[] = { 0.0f, -2.0f, SOLIDKITTEST_NAN, SOLIDKITTEST_INF, -SOLIDKITTEST_INF };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(badDpi)); ++i)
    {
        TestFalse(FString::Printf(TEXT("invalid DPI %d: no push"), i), cache.Update(0.5f, badDpi[i]));
        TestEqual(FString::Printf(TEXT("invalid DPI %d: the last valid kept"), i), cache.GetDpiScale(), 1.5f);
    }
    TestTrue(TEXT("strength change with an invalid DPI pushes"), cache.Update(0.75f, SOLIDKITTEST_NAN));
    TestEqual(TEXT("DPI still the last valid"), cache.GetDpiScale(), 1.5f);
    TestFalse(TEXT("then the kept DPI: no push"), cache.Update(0.75f, 1.5f));
    TestEqual(TEXT("4 pushes"), cache.GetPushCount(), 4);

    // Strengths compare after sanitizing
    TestTrue(TEXT("5 pushes (reads 2)"), cache.Update(5.0f, 1.5f));
    TestEqual(TEXT("5 stored as the maximum"), cache.GetStrength(), HUD_EFFECTS_MAX);
    TestFalse(TEXT("2 after 5: no push"), cache.Update(2.0f, 1.5f));
    TestFalse(TEXT("+inf after 2: no push"), cache.Update(SOLIDKITTEST_INF, 1.5f));
    TestTrue(TEXT("NaN pushes (reads 0)"), cache.Update(SOLIDKITTEST_NAN, 1.5f));
    TestEqual(TEXT("NaN stored as 0"), cache.GetStrength(), 0.0f);
    TestFalse(TEXT("-3 after NaN: no push"), cache.Update(-3.0f, 1.5f));
    TestFalse(TEXT("0 after NaN: no push"), cache.Update(0.0f, 1.5f));
    TestTrue(TEXT("the smallest positive change pushes (exact compare)"),
        cache.Update(std::nextafter(0.0f, 1.0f), 1.5f));
    TestTrue(TEXT("the smallest DPI change pushes (exact compare)"),
        cache.Update(std::nextafter(0.0f, 1.0f), std::nextafter(1.5f, 2.0f)));
    TestEqual(TEXT("8 pushes"), cache.GetPushCount(), 8);

    // A first call with an invalid DPI still pushes and keeps 1
    FSOLSolidMenuParamCache fresh;
    TestTrue(TEXT("first call with an invalid DPI pushes"), fresh.Update(1.0f, SOLIDKITTEST_NAN));
    TestEqual(TEXT("its DPI scale stays 1"), fresh.GetDpiScale(), 1.0f);
    TestEqual(TEXT("its push counted"), fresh.GetPushCount(), 1);
    TestFalse(TEXT("then DPI 1: no change, no push"), fresh.Update(1.0f, 1.0f));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitFlatHullTest, "SOLTest.SolidMenuKit.Factory.FlatHullBrush",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Without a subsystem, or with a bare one, MakeHullBrush gives a flat brush of FlatColor at the image size
bool FSOLSolidKitFlatHullTest::RunTest(const FString& /*parameters*/)
{
    USOLSolidMenuSubsystem* const bare = NewObject<USOLSolidMenuSubsystem>();
    if (!TestNotNull(TEXT("bare subsystem"), bare))
    {
        return false;
    }
    const USOLSolidMenuSubsystem* subsystems[] = { nullptr, bare };
    const TCHAR* subsystemNames[] = { TEXT("null subsystem"), TEXT("bare subsystem") };
    const FVector2f size(SOLIDKITTEST_BRUSH_W, SOLIDKITTEST_BRUSH_H);
    const FSOLSolidButtonLook looks[] = { SolidKitTestSettled(ESOLSolidButtonState::Normal),
        SolidKitTestSettled(ESOLSolidButtonState::Hovered), SolidKitTestSettled(ESOLSolidButtonState::Pressed),
        SolidKitTestSettled(ESOLSolidButtonState::Disabled) };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(subsystems)); ++i)
    {
        for (const ESOLSolidStyle style : SOLIDKITTEST_STYLES)
        {
            for (int32 l = 0; l < static_cast<int32>(UE_ARRAY_COUNT(looks)); ++l)
            {
                const FString name = FString::Printf(TEXT("%s: style %d look %d"), subsystemNames[i],
                    static_cast<int32>(style), l);
                const FSlateBrush brush = SOLSolidStyle::MakeHullBrush(subsystems[i], style, size, looks[l]);
                TestFalse(name + TEXT(": flat"), SolidKitTestIsMaterialBrush(brush));
                SolidKitTestColorEqual(*this, name + TEXT(": FlatColor"), SolidKitTestTint(brush),
                    SOLSolidStyle::FlatColor(style, looks[l]), SOLIDKITTEST_COLOR_TOLERANCE);
                const FVector2f imageSize = brush.GetImageSize();
                TestTrue(name + TEXT(": image size"), imageSize.Equals(size, SOLIDKITTEST_TOLERANCE));
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitFlatStylesTest, "SOLTest.SolidMenuKit.Factory.FlatStylesDisabledArt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The flat fallback styles (null and bare subsystem): every button variant, the slider and the toggle have disabled
// brushes distinct from normal / hovered that never show amber or cyan; sizes and the cyan scroll thumb
bool FSOLSolidKitFlatStylesTest::RunTest(const FString& /*parameters*/)
{
    SolidKitTestFactories(*this, TEXT("null subsystem"), nullptr, false);
    USOLSolidMenuSubsystem* const bare = NewObject<USOLSolidMenuSubsystem>();
    if (!TestNotNull(TEXT("bare subsystem"), bare))
    {
        return false;
    }
    SolidKitTestFactories(*this, TEXT("bare subsystem"), bare, false);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitFontFallbackTest, "SOLTest.SolidMenuKit.Factory.FontFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Without the menu font, MenuFont gives a valid engine fallback font at the requested size; the role form gives the
// role's size and letter spacing
bool FSOLSolidKitFontFallbackTest::RunTest(const FString& /*parameters*/)
{
    USOLSolidMenuSubsystem* const bare = NewObject<USOLSolidMenuSubsystem>();
    if (!TestNotNull(TEXT("bare subsystem"), bare))
    {
        return false;
    }
    const USOLSolidMenuSubsystem* subsystems[] = { nullptr, bare };
    const TCHAR* subsystemNames[] = { TEXT("null subsystem"), TEXT("bare subsystem") };
    const int32 sizes[] = { 12, 20, 56 };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(subsystems)); ++i)
    {
        for (const ESOLMenuFontWeight weight : SOLIDKITTEST_WEIGHTS)
        {
            for (const int32 size : sizes)
            {
                const FString name = FString::Printf(TEXT("%s: weight %d size %d"), subsystemNames[i],
                    static_cast<int32>(weight), size);
                const FSlateFontInfo font = SOLSolidStyle::MenuFont(subsystems[i], weight, size);
                TestTrue(name + TEXT(": a valid font"), font.HasValidFont());
                TestEqual(name + TEXT(": size"), static_cast<float>(font.Size), static_cast<float>(size));
            }
        }
        for (const ESOLMenuTextRole role : SOLIDKITTEST_ROLES)
        {
            const FString name = FString::Printf(TEXT("%s: role %d"), subsystemNames[i], static_cast<int32>(role));
            const FSlateFontInfo font = SOLSolidStyle::MenuFont(subsystems[i], role, ESOLMenuFontWeight::SemiBold);
            TestTrue(name + TEXT(": a valid font"), font.HasValidFont());
            TestEqual(name + TEXT(": role size"), static_cast<float>(font.Size),
                static_cast<float>(SOLSolidStyle::FontSize(role)));
            TestEqual(name + TEXT(": role letter spacing"), static_cast<int32>(font.LetterSpacing),
                SOLSolidStyle::LetterSpacing(role));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitResolveTest, "SOLTest.SolidMenuKit.Anim.ResolveState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ResolveState: Disabled, else Pressed, else Hovered (focus is the hover) unless the mouse hovers another control
// (hover wins, 46e follow-up), else Normal; the three-flag form is the hover-elsewhere-false one
bool FSOLSolidKitResolveTest::RunTest(const FString& /*parameters*/)
{
    for (int32 mask = 0; mask < 16; ++mask)
    {
        const bool bDisabled = (mask & 1) != 0;
        const bool bPressed = (mask & 2) != 0;
        const bool bFocused = (mask & 4) != 0;
        const bool bHoverElsewhere = (mask & 8) != 0;
        const ESOLSolidButtonState expected = bDisabled ? ESOLSolidButtonState::Disabled
            : bPressed ? ESOLSolidButtonState::Pressed
            : bFocused && !bHoverElsewhere ? ESOLSolidButtonState::Hovered
            : ESOLSolidButtonState::Normal;
        const ESOLSolidButtonState actual = FSOLSolidButtonAnim::ResolveState(bDisabled, bPressed, bFocused,
            bHoverElsewhere);
        const FString name = FString::Printf(TEXT("disabled %d pressed %d focused %d hover elsewhere %d"),
            bDisabled ? 1 : 0, bPressed ? 1 : 0, bFocused ? 1 : 0, bHoverElsewhere ? 1 : 0);
        TestEqual(name, static_cast<int32>(actual), static_cast<int32>(expected));
        if (!bHoverElsewhere)
        {
            TestEqual(name + TEXT(": three-flag form"), static_cast<int32>(FSOLSolidButtonAnim::ResolveState(
                bDisabled, bPressed, bFocused)), static_cast<int32>(expected));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitSliderFillTest, "SOLTest.SolidMenuKit.Style.SliderFill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SliderFill (46e follow-up), from its header: the fill starts the inset past the bar's start (handle / 2) and runs to
// the knob's centre (percent x (width - handle - indent) + indent / 2 + handle / 2), never past the bar's end less
// the inset; percent clamped (NaN 0); never negative or NaN
bool FSOLSolidKitSliderFillTest::RunTest(const FString& /*parameters*/)
{
    constexpr float width = 200.0f;
    constexpr float handle = 18.0f;
    constexpr float inset = 2.0f;
    constexpr float tolerance = 1.0e-3f;

    // No indent (the solid sliders): nothing at 0, the bar's end less the inset at 1, the knob's centre between
    const FSOLSliderFill zero = SOLSolidStyle::SliderFill(width, handle, false, 0.0f, inset);
    TestEqual(TEXT("0%: start at the bar's start plus the inset"), zero.StartX, 0.5f * handle + inset, tolerance);
    TestEqual(TEXT("0%: no fill"), zero.Width, 0.0f, tolerance);
    const FSOLSliderFill full = SOLSolidStyle::SliderFill(width, handle, false, 1.0f, inset);
    TestEqual(TEXT("100%: to the bar's end less the inset"), full.StartX + full.Width,
        width - 0.5f * handle - inset, tolerance);
    const float percents[] = { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f };
    for (const float percent : percents)
    {
        const FSOLSliderFill fill = SOLSolidStyle::SliderFill(width, handle, false, percent, inset);
        const float centre = percent * (width - handle) + 0.5f * handle;
        TestEqual(FString::Printf(TEXT("%.2f: ends at the knob's centre"), percent), fill.StartX + fill.Width,
            centre, tolerance);
        TestEqual(FString::Printf(TEXT("%.2f: proportional along the bar"), percent),
            (fill.StartX + fill.Width - 0.5f * handle) / (width - handle), percent, tolerance);
    }

    // Monotonic in percent
    float last = -1.0f;
    bool bMonotonic = true;
    for (int32 step = 0; step <= 100; ++step)
    {
        const float fillWidth = SOLSolidStyle::SliderFill(width, handle, false, step / 100.0f, inset).Width;
        bMonotonic &= fillWidth >= last;
        last = fillWidth;
    }
    TestTrue(TEXT("the fill never shrinks as the value grows"), bMonotonic);

    // The indent: the knob's centre moves in by half a handle at each end
    const FSOLSliderFill indented = SOLSolidStyle::SliderFill(width, handle, true, 0.5f, inset);
    TestEqual(TEXT("indented 50%: ends at the knob's centre"), indented.StartX + indented.Width,
        0.5f * (width - 2.0f * handle) + handle, tolerance);
    const FSOLSliderFill indentedFull = SOLSolidStyle::SliderFill(width, handle, true, 1.0f, inset);
    TestEqual(TEXT("indented 100%: ends a half handle short of the bar's end"),
        indentedFull.StartX + indentedFull.Width, width - handle, tolerance);

    // Clamping and bad input: percent outside [0, 1] or NaN, a negative inset, non-finite or tiny sizes
    TestEqual(TEXT("percent below 0 reads 0"), SOLSolidStyle::SliderFill(width, handle, false, -0.5f, inset).Width,
        0.0f, tolerance);
    TestEqual(TEXT("percent above 1 reads 1"), SOLSolidStyle::SliderFill(width, handle, false, 1.5f, inset).Width,
        full.Width, tolerance);
    TestEqual(TEXT("NaN percent reads 0"), SOLSolidStyle::SliderFill(width, handle, false, SOLIDKITTEST_NAN,
        inset).Width, 0.0f, tolerance);
    const FSOLSliderFill noInset = SOLSolidStyle::SliderFill(width, handle, false, 1.0f, -3.0f);
    TestEqual(TEXT("negative inset reads 0: start"), noInset.StartX, 0.5f * handle, tolerance);
    TestEqual(TEXT("negative inset reads 0: end"), noInset.StartX + noInset.Width, width - 0.5f * handle,
        tolerance);
    const float badSizes[][2] = { { SOLIDKITTEST_NAN, handle }, { SOLIDKITTEST_INF, handle }, { -10.0f, handle },
        { width, SOLIDKITTEST_NAN }, { width, SOLIDKITTEST_INF }, { 10.0f, handle }, { 0.0f, 0.0f } };
    const float badPercents[] = { 0.0f, 0.5f, 1.0f };
    for (const auto& size : badSizes)
    {
        for (const float percent : badPercents)
        {
            const FSOLSliderFill fill = SOLSolidStyle::SliderFill(size[0], size[1], false, percent, inset);
            const FString name = FString::Printf(TEXT("width %f handle %f at %.1f"), size[0], size[1], percent);
            TestTrue(name + TEXT(": finite"), FMath::IsFinite(fill.StartX) && FMath::IsFinite(fill.Width));
            TestTrue(name + TEXT(": never negative"), fill.Width >= 0.0f);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitSliderFillBrushTest, "SOLTest.SolidMenuKit.Factory.SliderFillBrush",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MakeSliderFillBrush (46e follow-up): a flat CYAN_COLOR box (no material), opaque, SLIDER_FILL_THICKNESS tall and
// thinner than the groove
bool FSOLSolidKitSliderFillBrushTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    const FSlateBrush brush = SOLSolidStyle::MakeSliderFillBrush();
    TestFalse(TEXT("flat: no material"), SolidKitTestIsMaterialBrush(brush));
    SolidKitTestRgbEqual(*this, TEXT("CYAN_COLOR"), SolidKitTestTint(brush), SolidKitTestColor(CYAN_COLOR),
        SOLIDKITTEST_COLOR_TOLERANCE);
    TestEqual(TEXT("opaque"), SolidKitTestTint(brush).A, 1.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("SLIDER_FILL_THICKNESS tall"), brush.GetImageSize().Y, SLIDER_FILL_THICKNESS);
    TestTrue(TEXT("thinner than the groove"), SLIDER_FILL_THICKNESS < SLIDER_BAR_THICKNESS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitDiffersTest, "SOLTest.SolidMenuKit.Anim.DiffersOnScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DiffersOnScreen: two looks differ when their 8-bit brush tints differ or their lifts are more than LIFT_EPSILON_PX
// apart; a change below both is no change
bool FSOLSolidKitDiffersTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidButtonLook a = SolidKitTestLook(0.4f, false, false, false);
    a.Lift = 0.5f;
    TestFalse(TEXT("a look against itself"), FSOLSolidButtonAnim::DiffersOnScreen(a, a));

    // Edge below and above one 8-bit step
    FSOLSolidButtonLook b = a;
    b.Edge = a.Edge + 0.1f / 255.0f;
    b.Led = b.Edge;
    b.Underline = b.Edge;
    TestFalse(TEXT("edge within one 8-bit level"), FSOLSolidButtonAnim::DiffersOnScreen(a, b));
    b.Edge = a.Edge + 2.0f / 255.0f;
    b.Led = b.Edge;
    b.Underline = b.Edge;
    TestTrue(TEXT("edge two 8-bit levels apart"), FSOLSolidButtonAnim::DiffersOnScreen(a, b));

    // Lift below and above LIFT_EPSILON_PX
    b = a;
    b.Lift = a.Lift + 0.5f * LIFT_EPSILON_PX / HOVER_LIFT_PX;
    TestFalse(TEXT("lift within the epsilon"), FSOLSolidButtonAnim::DiffersOnScreen(a, b));
    b.Lift = a.Lift + 2.0f * LIFT_EPSILON_PX / HOVER_LIFT_PX;
    TestTrue(TEXT("lift past the epsilon"), FSOLSolidButtonAnim::DiffersOnScreen(a, b));

    // Every tint flag
    b = a;
    b.bPressedHull = true;
    TestTrue(TEXT("pressed hull"), FSOLSolidButtonAnim::DiffersOnScreen(a, b));
    b = a;
    b.bSelectedMark = true;
    TestTrue(TEXT("selected mark"), FSOLSolidButtonAnim::DiffersOnScreen(a, b));
    b = a;
    b.bDisabled = true;
    TestTrue(TEXT("disabled"), FSOLSolidButtonAnim::DiffersOnScreen(a, b));
    TestTrue(TEXT("hovered against normal"), FSOLSolidButtonAnim::DiffersOnScreen(
        SolidKitTestSettled(ESOLSolidButtonState::Hovered), SolidKitTestSettled(ESOLSolidButtonState::Normal)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitHoverRampTest, "SOLTest.SolidMenuKit.Anim.HoverRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A hover ramps in from the Normal look over HOVER_RAMP_S (EvaluateButtonFrom), is not settled mid-ramp, and settles
// at the full hover look
bool FSOLSolidKitHoverRampTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    const FSOLSolidButtonLook normal = SolidKitTestSettled(ESOLSolidButtonState::Normal);
    FSOLSolidButtonAnim anim;
    SolidKitTestLookEqual(*this, TEXT("first update Normal"),
        anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S), normal);
    TestTrue(TEXT("Normal settled"), anim.IsSettled(SOLIDKITTEST_T0, SOLIDKITTEST_S));

    // The change starts the ramp from the Normal look
    SolidKitTestLookEqual(*this, TEXT("hover at the change: the ramp's start"),
        anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, SOLIDKITTEST_S),
        SolidKitTestFrom(ESOLSolidButtonState::Hovered, normal, 0.0));
    TestEqual(TEXT("shown Hovered"), static_cast<int32>(anim.GetShownState()),
        static_cast<int32>(ESOLSolidButtonState::Hovered));

    // Mid-ramp
    const double mid = SOLIDKITTEST_T1 + 0.5 * HOVER_RAMP_S;
    const FSOLSolidButtonLook midLook = anim.Update(ESOLSolidButtonState::Hovered, false, mid, SOLIDKITTEST_S);
    SolidKitTestLookEqual(*this, TEXT("half the ramp"), midLook,
        SolidKitTestFrom(ESOLSolidButtonState::Hovered, normal, 0.5 * HOVER_RAMP_S));
    TestEqual(TEXT("half the ramp: Edge 0.5"), midLook.Edge, 0.5f, 1.0e-3f);
    TestEqual(TEXT("half the ramp: Lift 0.5"), midLook.Lift, 0.5f, 1.0e-3f);
    TestFalse(TEXT("not settled mid-ramp"), anim.IsSettled(mid, SOLIDKITTEST_S));
    SolidKitTestLookEqual(*this, TEXT("GetLook is the last look"), anim.GetLook(), midLook);

    // Settled
    const double done = SOLIDKITTEST_T1 + 2.0 * HOVER_RAMP_S;
    const FSOLSolidButtonLook hovered = SolidKitTestSettled(ESOLSolidButtonState::Hovered);
    SolidKitTestLookEqual(*this, TEXT("after the ramp: the full hover look"),
        anim.Update(ESOLSolidButtonState::Hovered, false, done, SOLIDKITTEST_S), hovered);
    TestTrue(TEXT("settled after the ramp"), anim.IsSettled(done, SOLIDKITTEST_S));

    // Strength 0: the hover look is instant and fully present
    FSOLSolidButtonAnim flat;
    flat.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, 0.0f);
    const FSOLSolidButtonLook instant = flat.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, 0.0f);
    TestEqual(TEXT("s = 0: Lift at once"), instant.Lift, 1.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("s = 0: Edge at once"), instant.Edge, 1.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("s = 0: no glow"), instant.Glow, 0.0f, SOLIDKITTEST_TOLERANCE);
    TestTrue(TEXT("s = 0: settled at once"), flat.IsSettled(SOLIDKITTEST_T1, 0.0f));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitReversalTest, "SOLTest.SolidMenuKit.Anim.ReversalNoPop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A target change mid-ramp starts the new ramp from the look shown at that moment (evaluated at the change's time),
// so a hover that reverses continues without a pop, and a second reversal continues again
bool FSOLSolidKitReversalTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    const FSOLSolidButtonLook normal = SolidKitTestSettled(ESOLSolidButtonState::Normal);
    FSOLSolidButtonAnim anim;
    anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1 + 0.3 * HOVER_RAMP_S, SOLIDKITTEST_S);

    // Leave at 0.5 of the ramp (no update between 0.3 and 0.5): the look at the change is the hover ramp at 0.5
    const double leave = SOLIDKITTEST_T1 + 0.5 * HOVER_RAMP_S;
    const FSOLSolidButtonLook atLeave = SolidKitTestFrom(ESOLSolidButtonState::Hovered, normal, 0.5 * HOVER_RAMP_S);
    const FSOLSolidButtonLook shownAtLeave = anim.Update(ESOLSolidButtonState::Normal, false, leave, SOLIDKITTEST_S);
    TestTrue(TEXT("no pop at the reversal"), SolidKitTestMaxAmountDelta(shownAtLeave, atLeave) <= 1.0e-3f);
    TestEqual(TEXT("shown Normal"), static_cast<int32>(anim.GetShownState()),
        static_cast<int32>(ESOLSolidButtonState::Normal));

    // The fall continues from there
    const double fall = leave + 0.25 * HOVER_RAMP_S;
    const FSOLSolidButtonLook fallLook = anim.Update(ESOLSolidButtonState::Normal, false, fall, SOLIDKITTEST_S);
    SolidKitTestLookEqual(*this, TEXT("falling from the look at the reversal"), fallLook,
        SolidKitTestFrom(ESOLSolidButtonState::Normal, atLeave, 0.25 * HOVER_RAMP_S));
    TestTrue(TEXT("falling: below the look at the reversal"), fallLook.Edge < atLeave.Edge);
    TestTrue(TEXT("falling: not yet at rest"), fallLook.Edge > 0.0f);

    // Reverse again: the hover ramp restarts from the fallen look, still without a pop
    const FSOLSolidButtonLook shownAtBack = anim.Update(ESOLSolidButtonState::Hovered, false, fall, SOLIDKITTEST_S);
    TestTrue(TEXT("no pop at the second reversal"), SolidKitTestMaxAmountDelta(shownAtBack, fallLook) <= 1.0e-3f);
    const FSOLSolidButtonLook rising = anim.Update(ESOLSolidButtonState::Hovered, false,
        fall + 0.25 * HOVER_RAMP_S, SOLIDKITTEST_S);
    SolidKitTestLookEqual(*this, TEXT("rising from the fallen look"), rising,
        SolidKitTestFrom(ESOLSolidButtonState::Hovered, fallLook, 0.25 * HOVER_RAMP_S));
    TestTrue(TEXT("rising: above the fallen look"), rising.Edge > fallLook.Edge);

    // Settles at the full hover look
    const double done = fall + 2.0 * HOVER_RAMP_S;
    SolidKitTestLookEqual(*this, TEXT("settled hovered"),
        anim.Update(ESOLSolidButtonState::Hovered, false, done, SOLIDKITTEST_S),
        SolidKitTestSettled(ESOLSolidButtonState::Hovered));
    TestTrue(TEXT("settled"), anim.IsSettled(done, SOLIDKITTEST_S));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitLingerTest, "SOLTest.SolidMenuKit.Anim.PressLingerQuickClick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press shows Pressed at once and keeps it until PRESSED_LINGER_S after the press even when released sooner; the
// ramp to the new target starts at the linger's end, from the pressed look
bool FSOLSolidKitLingerTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;

    // Released back to hover
    {
        FSOLSolidButtonAnim anim;
        anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
        anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, SOLIDKITTEST_S);
        anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1 + 2.0 * HOVER_RAMP_S, SOLIDKITTEST_S);

        const FSOLSolidButtonLook pressed = anim.Update(ESOLSolidButtonState::Pressed, false, SOLIDKITTEST_T2,
            SOLIDKITTEST_S);
        TestTrue(TEXT("press: pressed hull at once"), pressed.bPressedHull);
        TestEqual(TEXT("press: Lift 0"), pressed.Lift, 0.0f, SOLIDKITTEST_TOLERANCE);
        TestEqual(TEXT("press: Edge 1"), pressed.Edge, 1.0f, SOLIDKITTEST_TOLERANCE);
        TestEqual(TEXT("press: shown Pressed"), static_cast<int32>(anim.GetShownState()),
            static_cast<int32>(ESOLSolidButtonState::Pressed));

        // Released within the linger: still Pressed
        const double early = SOLIDKITTEST_T2 + 0.3 * PRESSED_LINGER_S;
        TestTrue(TEXT("released early: still the pressed hull"),
            anim.Update(ESOLSolidButtonState::Hovered, false, early, SOLIDKITTEST_S).bPressedHull);
        TestEqual(TEXT("released early: shown Pressed"), static_cast<int32>(anim.GetShownState()),
            static_cast<int32>(ESOLSolidButtonState::Pressed));
        TestFalse(TEXT("released early: not settled (the linger runs)"), anim.IsSettled(early, SOLIDKITTEST_S));
        TestTrue(TEXT("near the linger's end: still pressed"), anim.Update(ESOLSolidButtonState::Hovered, false,
            SOLIDKITTEST_T2 + 0.9 * PRESSED_LINGER_S, SOLIDKITTEST_S).bPressedHull);

        // After the linger: the hover ramp from the pressed look, started at the linger's end
        const double after = SOLIDKITTEST_T2 + PRESSED_LINGER_S + 0.25 * HOVER_RAMP_S;
        const FSOLSolidButtonLook look = anim.Update(ESOLSolidButtonState::Hovered, false, after, SOLIDKITTEST_S);
        SolidKitTestLookEqual(*this, TEXT("after the linger: hover ramp from the pressed look"), look,
            SolidKitTestFrom(ESOLSolidButtonState::Hovered, pressed, 0.25 * HOVER_RAMP_S));
        TestFalse(TEXT("after the linger: no pressed hull"), look.bPressedHull);
        TestEqual(TEXT("after the linger: shown Hovered"), static_cast<int32>(anim.GetShownState()),
            static_cast<int32>(ESOLSolidButtonState::Hovered));
        const double done = SOLIDKITTEST_T2 + PRESSED_LINGER_S + 2.0 * HOVER_RAMP_S;
        SolidKitTestLookEqual(*this, TEXT("settles hovered"),
            anim.Update(ESOLSolidButtonState::Hovered, false, done, SOLIDKITTEST_S),
            SolidKitTestSettled(ESOLSolidButtonState::Hovered));
        TestTrue(TEXT("settled"), anim.IsSettled(done, SOLIDKITTEST_S));
    }

    // Released to Normal (the pointer left): pressed through the linger, then falls from the pressed look
    {
        FSOLSolidButtonAnim anim;
        anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
        const FSOLSolidButtonLook pressed = anim.Update(ESOLSolidButtonState::Pressed, false, SOLIDKITTEST_T2,
            SOLIDKITTEST_S);
        TestTrue(TEXT("to Normal within the linger: still pressed"), anim.Update(ESOLSolidButtonState::Normal, false,
            SOLIDKITTEST_T2 + 0.3 * PRESSED_LINGER_S, SOLIDKITTEST_S).bPressedHull);
        const FSOLSolidButtonLook look = anim.Update(ESOLSolidButtonState::Normal, false,
            SOLIDKITTEST_T2 + PRESSED_LINGER_S + 0.25 * HOVER_RAMP_S, SOLIDKITTEST_S);
        SolidKitTestLookEqual(*this, TEXT("to Normal after the linger: the fall from the pressed look"), look,
            SolidKitTestFrom(ESOLSolidButtonState::Normal, pressed, 0.25 * HOVER_RAMP_S));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitLongPressTest, "SOLTest.SolidMenuKit.Anim.LongPressRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A press held past the linger stays Pressed while held; the release changes the state at once and the ramp starts at
// the release
bool FSOLSolidKitLongPressTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidButtonAnim anim;
    anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    const FSOLSolidButtonLook pressed = anim.Update(ESOLSolidButtonState::Pressed, false, SOLIDKITTEST_T2,
        SOLIDKITTEST_S);
    const double release = SOLIDKITTEST_T2 + 5.0 * PRESSED_LINGER_S;
    const FSOLSolidButtonLook held = anim.Update(ESOLSolidButtonState::Pressed, false, release, SOLIDKITTEST_S);
    SolidKitTestLookEqual(*this, TEXT("held past the linger: still the pressed look"), held, pressed);

    // Release at the same time: the state changes at once
    const FSOLSolidButtonLook released = anim.Update(ESOLSolidButtonState::Hovered, false, release, SOLIDKITTEST_S);
    TestFalse(TEXT("released: no pressed hull"), released.bPressedHull);
    TestEqual(TEXT("released: shown Hovered"), static_cast<int32>(anim.GetShownState()),
        static_cast<int32>(ESOLSolidButtonState::Hovered));
    TestTrue(TEXT("released: no pop"), SolidKitTestMaxAmountDelta(released, pressed) <= 1.0e-3f);
    SolidKitTestLookEqual(*this, TEXT("the ramp starts at the release"),
        anim.Update(ESOLSolidButtonState::Hovered, false, release + 0.25 * HOVER_RAMP_S, SOLIDKITTEST_S),
        SolidKitTestFrom(ESOLSolidButtonState::Hovered, pressed, 0.25 * HOVER_RAMP_S));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitSelectedTest, "SOLTest.SolidMenuKit.Anim.SelectedOrthogonal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// bSelected is orthogonal: a selected button ramps and settles with exactly the unselected amounts plus the cyan mark
// (hovered: the full amber hover look and the mark; pressed: the pressed hull and the mark); toggling it on a settled
// button shows or hides the mark at once
bool FSOLSolidKitSelectedTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;

    // Two buttons, one selected, through the same hover
    FSOLSolidButtonAnim selected;
    FSOLSolidButtonAnim plain;
    selected.Update(ESOLSolidButtonState::Normal, true, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    plain.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    selected.Update(ESOLSolidButtonState::Hovered, true, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    plain.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    const double times[] = { 0.25 * HOVER_RAMP_S, 0.5 * HOVER_RAMP_S, 2.0 * HOVER_RAMP_S };
    for (const double t : times)
    {
        const FString name = FString::Printf(TEXT("hover at %.3f s"), t);
        const FSOLSolidButtonLook a = selected.Update(ESOLSolidButtonState::Hovered, true, SOLIDKITTEST_T1 + t,
            SOLIDKITTEST_S);
        FSOLSolidButtonLook b = plain.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1 + t,
            SOLIDKITTEST_S);
        TestTrue(name + TEXT(": selected has the mark"), a.bSelectedMark);
        TestFalse(name + TEXT(": unselected has no mark"), b.bSelectedMark);
        b.bSelectedMark = true;
        SolidKitTestLookEqual(*this, name + TEXT(": the same amounts"), a, b);
    }
    const FSOLSolidButtonLook hoveredSelected = selected.GetLook();
    TestEqual(TEXT("hovered + selected: full lift"), hoveredSelected.Lift, 1.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("hovered + selected: full amber edge"), hoveredSelected.Edge, 1.0f, SOLIDKITTEST_TOLERANCE);
    const FSOLSolidButtonLook decoded = SOLSolidMenu::DecodeButtonTint(SOLSolidStyle::UnpackVertexTint(
        SolidKitTestQuantizeSrgb(SOLSolidStyle::BrushTint(hoveredSelected))));
    TestTrue(TEXT("hovered + selected tint: the mark"), decoded.bSelectedMark);
    TestTrue(TEXT("hovered + selected tint: lit"), decoded.Edge >= 1.0f - SOLIDKITTEST_SRGB_8BIT_TOLERANCE);

    // Pressed and selected
    const FSOLSolidButtonLook pressedSelected = selected.Update(ESOLSolidButtonState::Pressed, true, SOLIDKITTEST_T2,
        SOLIDKITTEST_S);
    TestTrue(TEXT("pressed + selected: the pressed hull"), pressedSelected.bPressedHull);
    TestTrue(TEXT("pressed + selected: the mark"), pressedSelected.bSelectedMark);

    // Toggling the selection on a settled Normal button: the mark at once, amounts untouched, still settled
    FSOLSolidButtonAnim toggled;
    toggled.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    FSOLSolidButtonLook on = toggled.Update(ESOLSolidButtonState::Normal, true, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    TestTrue(TEXT("selected on: the mark at once"), on.bSelectedMark);
    TestTrue(TEXT("selected on: still settled"), toggled.IsSettled(SOLIDKITTEST_T1, SOLIDKITTEST_S));
    on.bSelectedMark = false;
    SolidKitTestLookEqual(*this, TEXT("selected on: Normal amounts"), on, SolidKitTestSettled(
        ESOLSolidButtonState::Normal));
    TestFalse(TEXT("selected off: the mark gone at once"),
        toggled.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T2, SOLIDKITTEST_S).bSelectedMark);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitDisabledTest, "SOLTest.SolidMenuKit.Anim.DisabledIgnoresHoverAndSelected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Disabled shows the disabled look at once (all amounts 0, bDisabled, no mark even when selected) and its brush tint
// carries the disabled code; re-enabling clears it
bool FSOLSolidKitDisabledTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidButtonAnim anim;
    anim.Update(ESOLSolidButtonState::Normal, true, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    anim.Update(ESOLSolidButtonState::Hovered, true, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    anim.Update(ESOLSolidButtonState::Hovered, true, SOLIDKITTEST_T1 + 2.0 * HOVER_RAMP_S, SOLIDKITTEST_S);

    // Focused, pressed and selected still resolve to Disabled
    const ESOLSolidButtonState state = FSOLSolidButtonAnim::ResolveState(true, true, true);
    TestEqual(TEXT("resolves Disabled"), static_cast<int32>(state), static_cast<int32>(ESOLSolidButtonState::Disabled));
    const FSOLSolidButtonLook disabled = anim.Update(state, true, SOLIDKITTEST_T2, SOLIDKITTEST_S);
    SolidKitTestLookEqual(*this, TEXT("the disabled look at once"), disabled,
        SolidKitTestSettled(ESOLSolidButtonState::Disabled));
    TestTrue(TEXT("bDisabled"), disabled.bDisabled);
    TestFalse(TEXT("no mark although selected"), disabled.bSelectedMark);
    TestEqual(TEXT("shown Disabled"), static_cast<int32>(anim.GetShownState()),
        static_cast<int32>(ESOLSolidButtonState::Disabled));
    SolidKitTestLookEqual(*this, TEXT("later: unchanged"),
        anim.Update(state, true, SOLIDKITTEST_T2 + 0.5 * HOVER_RAMP_S, SOLIDKITTEST_S),
        SolidKitTestSettled(ESOLSolidButtonState::Disabled));
    TestTrue(TEXT("settled"), anim.IsSettled(SOLIDKITTEST_T2 + 0.5 * HOVER_RAMP_S, SOLIDKITTEST_S));

    // The tint carries the disabled code and the dim
    const FLinearColor tint = SOLSolidStyle::BrushTint(disabled);
    const FSOLSolidButtonLook decoded = SOLSolidMenu::DecodeButtonTint(SOLSolidStyle::UnpackVertexTint(
        SolidKitTestQuantizeSrgb(tint)));
    TestTrue(TEXT("tint decodes disabled"), decoded.bDisabled);
    TestFalse(TEXT("tint decodes no mark"), decoded.bSelectedMark);
    TestEqual(TEXT("tint A: DISABLED_ALPHA"), tint.A, DISABLED_ALPHA, SOLIDKITTEST_TOLERANCE);

    // Re-enabled
    const FSOLSolidButtonLook enabled = anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T2 + 1.0,
        SOLIDKITTEST_S);
    TestFalse(TEXT("re-enabled: no bDisabled"), enabled.bDisabled);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitSettledTest, "SOLTest.SolidMenuKit.Anim.SettledNoChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A settled button reports IsSettled and its later looks do not differ on screen (a tick then costs a compare); a
// ramp is not settled and its frames differ
bool FSOLSolidKitSettledTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;

    // Every settled target and selection
    struct FSOLSolidKitTarget
    {
        ESOLSolidButtonState State;
        bool bSelected;
    };
    const FSOLSolidKitTarget targets[] = { { ESOLSolidButtonState::Normal, false },
        { ESOLSolidButtonState::Normal, true }, { ESOLSolidButtonState::Hovered, false },
        { ESOLSolidButtonState::Hovered, true }, { ESOLSolidButtonState::Disabled, false },
        { ESOLSolidButtonState::Disabled, true } };
    for (const FSOLSolidKitTarget& target : targets)
    {
        const FString name = FString::Printf(TEXT("state %d selected %d"), static_cast<int32>(target.State),
            target.bSelected ? 1 : 0);
        FSOLSolidButtonAnim anim;
        anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
        anim.Update(target.State, target.bSelected, SOLIDKITTEST_T1, SOLIDKITTEST_S);
        double now = SOLIDKITTEST_T1 + 2.0 * HOVER_RAMP_S;
        FSOLSolidButtonLook previous = anim.Update(target.State, target.bSelected, now, SOLIDKITTEST_S);
        for (int32 frame = 0; frame < 3; ++frame)
        {
            now += 1.0 / 60.0;
            TestTrue(name + FString::Printf(TEXT(": frame %d settled"), frame),
                anim.IsSettled(now, SOLIDKITTEST_S));
            const FSOLSolidButtonLook current = anim.Update(target.State, target.bSelected, now, SOLIDKITTEST_S);
            TestFalse(name + FString::Printf(TEXT(": frame %d no change on screen"), frame),
                FSOLSolidButtonAnim::DiffersOnScreen(previous, current));
            previous = current;
        }
    }

    // A ramp: not settled, consecutive frames differ
    FSOLSolidButtonAnim anim;
    anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    const FSOLSolidButtonLook quarter = anim.Update(ESOLSolidButtonState::Hovered, false,
        SOLIDKITTEST_T1 + 0.25 * HOVER_RAMP_S, SOLIDKITTEST_S);
    TestFalse(TEXT("ramp: not settled"), anim.IsSettled(SOLIDKITTEST_T1 + 0.25 * HOVER_RAMP_S, SOLIDKITTEST_S));
    const FSOLSolidButtonLook half = anim.Update(ESOLSolidButtonState::Hovered, false,
        SOLIDKITTEST_T1 + 0.5 * HOVER_RAMP_S, SOLIDKITTEST_S);
    TestTrue(TEXT("ramp: frames differ on screen"), FSOLSolidButtonAnim::DiffersOnScreen(quarter, half));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitNonFiniteTest, "SOLTest.SolidMenuKit.Anim.NonFiniteTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A non-finite time reads as no time passed: the look stays the last one, and finite times continue the ramp
bool FSOLSolidKitNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    const FSOLSolidButtonLook normal = SolidKitTestSettled(ESOLSolidButtonState::Normal);
    FSOLSolidButtonAnim anim;
    anim.Update(ESOLSolidButtonState::Normal, false, SOLIDKITTEST_T0, SOLIDKITTEST_S);
    anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    const FSOLSolidButtonLook last = anim.Update(ESOLSolidButtonState::Hovered, false,
        SOLIDKITTEST_T1 + 0.3 * HOVER_RAMP_S, SOLIDKITTEST_S);
    SolidKitTestLookEqual(*this, TEXT("NaN: the last look"),
        anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_NAN_D, SOLIDKITTEST_S), last);
    SolidKitTestLookEqual(*this, TEXT("+inf: the last look"),
        anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_INF_D, SOLIDKITTEST_S), last);
    SolidKitTestLookEqual(*this, TEXT("-inf: the last look"),
        anim.Update(ESOLSolidButtonState::Hovered, false, -SOLIDKITTEST_INF_D, SOLIDKITTEST_S), last);
    SolidKitTestLookEqual(*this, TEXT("a finite time continues the ramp"),
        anim.Update(ESOLSolidButtonState::Hovered, false, SOLIDKITTEST_T1 + 0.6 * HOVER_RAMP_S, SOLIDKITTEST_S),
        SolidKitTestFrom(ESOLSolidButtonState::Hovered, normal, 0.6 * HOVER_RAMP_S));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitPassAlphaTest, "SOLTest.SolidMenuKit.TraceLayer.PassAlphas",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// GlowPassAlpha is TRACE_GLOW_ALPHA x min(s, 2) x pulse in [0, 1], 0 at s <= 0 (and NaN); CorePassAlpha is the pulse
// clamped to [0, 1], non-finite reading 1
bool FSOLSolidKitPassAlphaTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    TestEqual(TEXT("glow s 1"), SSOLSolidTraceLayer::GlowPassAlpha(1.0f, 1.0f), TRACE_GLOW_ALPHA,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("glow s 2: doubled"), SSOLSolidTraceLayer::GlowPassAlpha(2.0f, 1.0f), 2.0f * TRACE_GLOW_ALPHA,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("glow s 3: capped at 2"), SSOLSolidTraceLayer::GlowPassAlpha(3.0f, 1.0f), 2.0f * TRACE_GLOW_ALPHA,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("glow s 0.5"), SSOLSolidTraceLayer::GlowPassAlpha(0.5f, 1.0f), 0.5f * TRACE_GLOW_ALPHA,
        SOLIDKITTEST_TOLERANCE);
    const float pulse = 1.0f + IDLE_PULSE_DEPTH;
    TestEqual(TEXT("glow with a pulse"), SSOLSolidTraceLayer::GlowPassAlpha(1.0f, pulse), TRACE_GLOW_ALPHA * pulse,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("glow s 0"), SSOLSolidTraceLayer::GlowPassAlpha(0.0f, 1.0f), 0.0f);
    TestEqual(TEXT("glow s < 0"), SSOLSolidTraceLayer::GlowPassAlpha(-1.0f, 1.0f), 0.0f);
    TestEqual(TEXT("glow s NaN"), SSOLSolidTraceLayer::GlowPassAlpha(SOLIDKITTEST_NAN, 1.0f), 0.0f);
    TestEqual(TEXT("glow clamped to 1"), SSOLSolidTraceLayer::GlowPassAlpha(2.0f, 100.0f), 1.0f,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("glow with a negative pulse clamped to 0"), SSOLSolidTraceLayer::GlowPassAlpha(1.0f, -1.0f), 0.0f,
        SOLIDKITTEST_TOLERANCE);

    TestEqual(TEXT("core 0.5"), SSOLSolidTraceLayer::CorePassAlpha(0.5f), 0.5f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("core 1"), SSOLSolidTraceLayer::CorePassAlpha(1.0f), 1.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("core above 1 clamped"), SSOLSolidTraceLayer::CorePassAlpha(pulse), 1.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("core below 0 clamped"), SSOLSolidTraceLayer::CorePassAlpha(-0.2f), 0.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("core NaN reads 1"), SSOLSolidTraceLayer::CorePassAlpha(SOLIDKITTEST_NAN), 1.0f);
    TestEqual(TEXT("core +inf reads 1"), SSOLSolidTraceLayer::CorePassAlpha(SOLIDKITTEST_INF), 1.0f);
    TestEqual(TEXT("core -inf reads 1"), SSOLSolidTraceLayer::CorePassAlpha(-SOLIDKITTEST_INF), 1.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitBareSubsystemTest, "SOLTest.SolidMenuKit.Subsystem.BareNoAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A bare, never-initialized subsystem has no assets (every style flat), counts parameter pushes by the cache rule,
// makes no unique material and never crashes
bool FSOLSolidKitBareSubsystemTest::RunTest(const FString& /*parameters*/)
{
    TestNull(TEXT("Get(nullptr)"), USOLSolidMenuSubsystem::Get(nullptr));
    USOLSolidMenuSubsystem* const subsystem = NewObject<USOLSolidMenuSubsystem>();
    if (!TestNotNull(TEXT("subsystem"), subsystem))
    {
        return false;
    }

    // No assets
    TestEqual(TEXT("strength: the default with no settings subsystem"), subsystem->GetStrength(),
        SOL::Settings::HUD_EFFECTS_DEFAULT);
    TestEqual(TEXT("DPI scale 1 before any push"), subsystem->GetDpiScale(), 1.0f);
    TestEqual(TEXT("viewport DPI scale 1 without a viewport"), subsystem->GetViewportDpiScale(), 1.0f);
    TestNull(TEXT("no menu font"), subsystem->GetMenuFont());
    for (const ESOLSolidStyle style : SOLIDKITTEST_STYLES)
    {
        const FString name = FString::Printf(TEXT("style %d"), static_cast<int32>(style));
        TestFalse(name + TEXT(": no style material"), subsystem->HasStyleMaterial(style));
        TestNull(name + TEXT(": null material"), subsystem->GetStyleMaterial(style));
    }
    for (const ESOLSolidTexture texture : SOLIDKITTEST_TEXTURES)
    {
        TestNull(FString::Printf(TEXT("texture %d: null"), static_cast<int32>(texture)),
            subsystem->GetTexture(texture));
    }
    TestNull(TEXT("no unique material"), subsystem->CreateUniqueMaterial(ESOLSolidStyle::Panel, subsystem));
    TestEqual(TEXT("no unique materials alive"), subsystem->GetUniqueMaterialCount(), 0);

    // Pushes follow the cache rule with no material
    TestEqual(TEXT("no push yet"), subsystem->GetParamPushCount(), 0);
    subsystem->PushParams(1.0f, 1.0f);
    TestEqual(TEXT("first push"), subsystem->GetParamPushCount(), 1);
    subsystem->PushParams(1.0f, 1.0f);
    TestEqual(TEXT("unchanged: no push"), subsystem->GetParamPushCount(), 1);
    subsystem->PushParams(0.5f, 1.0f);
    TestEqual(TEXT("strength change pushes"), subsystem->GetParamPushCount(), 2);
    subsystem->PushParams(0.5f, 1.5f);
    TestEqual(TEXT("DPI change pushes"), subsystem->GetParamPushCount(), 3);
    TestEqual(TEXT("DPI scale stored"), subsystem->GetDpiScale(), 1.5f);
    subsystem->PushParams(0.5f, SOLIDKITTEST_NAN);
    TestEqual(TEXT("invalid DPI: no push"), subsystem->GetParamPushCount(), 3);
    TestEqual(TEXT("invalid DPI: the last valid kept"), subsystem->GetDpiScale(), 1.5f);
    subsystem->PushParams(SOLIDKITTEST_NAN, 1.5f);
    TestEqual(TEXT("NaN strength (reads 0) pushes"), subsystem->GetParamPushCount(), 4);
    subsystem->PushParams(-5.0f, 0.0f);
    TestEqual(TEXT("-5 (reads 0) with an invalid DPI: no push"), subsystem->GetParamPushCount(), 4);

    // UpdateForFrame without a world or viewport: the first call pushes, a second in the same frame does nothing
    USOLSolidMenuSubsystem* const fresh = NewObject<USOLSolidMenuSubsystem>();
    fresh->UpdateForFrame();
    TestEqual(TEXT("UpdateForFrame: the first push"), fresh->GetParamPushCount(), 1);
    fresh->UpdateForFrame();
    TestEqual(TEXT("UpdateForFrame twice in a frame: one push"), fresh->GetParamPushCount(), 1);
    TestEqual(TEXT("UpdateForFrame: DPI scale 1 without a viewport"), fresh->GetDpiScale(), 1.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitWorldSubsystemTest, "SOLTest.SolidMenuKit.Subsystem.WorldLoadsAndPushes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In a game world the subsystem loads every asset (no missing one), owns one distinct shared material per style, and
// writes the sanitized strength and the DPI scale into every material it made, shared and unique, only on a change
bool FSOLSolidKitWorldSubsystemTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenuMaterialParams;
    FSOLSolidKitTestWorld testWorld;
    USOLSolidMenuSubsystem* const subsystem = USOLSolidMenuSubsystem::Get(testWorld.World);
    if (!TestNotNull(TEXT("the world's subsystem"), subsystem))
    {
        return false;
    }
    TestTrue(TEXT("Get is the world's subsystem"),
        subsystem == testWorld.World->GetSubsystem<USOLSolidMenuSubsystem>());

    // Every asset loaded
    TestEqual(TEXT("no missing asset"), subsystem->GetMissingAssetCount(), 0);
    TestNotNull(TEXT("menu font"), subsystem->GetMenuFont());
    for (const ESOLSolidTexture texture : SOLIDKITTEST_TEXTURES)
    {
        TestNotNull(FString::Printf(TEXT("texture %d"), static_cast<int32>(texture)), subsystem->GetTexture(texture));
    }
    TArray<UMaterialInstanceDynamic*> materials;
    for (const ESOLSolidStyle style : SOLIDKITTEST_STYLES)
    {
        const FString name = FString::Printf(TEXT("style %d"), static_cast<int32>(style));
        TestTrue(name + TEXT(": has its material"), subsystem->HasStyleMaterial(style));
        UMaterialInstanceDynamic* const material = subsystem->GetStyleMaterial(style);
        TestNotNull(name + TEXT(": material"), material);
        TestFalse(name + TEXT(": distinct from the other styles'"), material != nullptr
            && materials.Contains(material));
        materials.Add(material);
    }

    // The disabled cue style is baked into the material by create_solid_menu_materials.py from
    // SOL::SolidMenu::DISABLED_CUE_STYLE (C++ never pushes it): every style's material carries the constant
    for (int32 i = 0; i < materials.Num(); ++i)
    {
        TestEqual(FString::Printf(TEXT("style %d: disabled cue style is the constant"), i),
            SolidKitTestScalar(materials[i], DISABLED_CUE_STYLE), SOL::SolidMenu::DISABLED_CUE_STYLE,
            SOLIDKITTEST_TOLERANCE);
    }

    // A push reaches every shared material, once per change
    subsystem->PushParams(1.5f, 2.0f);
    const int32 pushes = subsystem->GetParamPushCount();
    for (int32 i = 0; i < materials.Num(); ++i)
    {
        TestEqual(FString::Printf(TEXT("style %d: strength pushed"), i), SolidKitTestScalar(materials[i],
            EFFECTS_STRENGTH), 1.5f, SOLIDKITTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("style %d: DPI scale pushed"), i), SolidKitTestScalar(materials[i], DPI_SCALE),
            2.0f, SOLIDKITTEST_TOLERANCE);
    }
    subsystem->PushParams(1.5f, 2.0f);
    TestEqual(TEXT("unchanged: no push"), subsystem->GetParamPushCount(), pushes);

    // A unique material starts current and follows later pushes
    UMaterialInstanceDynamic* const unique = subsystem->CreateUniqueMaterial(ESOLSolidStyle::Panel, testWorld.World);
    if (!TestNotNull(TEXT("unique panel material"), unique))
    {
        return false;
    }
    TestTrue(TEXT("unique: outered to its owner"), unique->GetOuter() == testWorld.World);
    TestFalse(TEXT("unique: not a shared material"), materials.Contains(unique));
    TestEqual(TEXT("one unique material alive"), subsystem->GetUniqueMaterialCount(), 1);
    TestEqual(TEXT("unique: the current strength"), SolidKitTestScalar(unique, EFFECTS_STRENGTH), 1.5f,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("unique: the current DPI scale"), SolidKitTestScalar(unique, DPI_SCALE), 2.0f,
        SOLIDKITTEST_TOLERANCE);

    // Sanitized values reach shared and unique materials
    subsystem->PushParams(5.0f, 1.25f);
    TestEqual(TEXT("unique: strength 5 written as the maximum"), SolidKitTestScalar(unique, EFFECTS_STRENGTH),
        SOL::Settings::HUD_EFFECTS_MAX, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("unique: new DPI scale"), SolidKitTestScalar(unique, DPI_SCALE), 1.25f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("shared: strength 5 written as the maximum"), SolidKitTestScalar(materials[0], EFFECTS_STRENGTH),
        SOL::Settings::HUD_EFFECTS_MAX, SOLIDKITTEST_TOLERANCE);
    subsystem->PushParams(SOLIDKITTEST_NAN, -1.0f);
    TestEqual(TEXT("NaN strength written as 0"), SolidKitTestScalar(materials[0], EFFECTS_STRENGTH), 0.0f,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("an invalid DPI scale keeps the last valid"), SolidKitTestScalar(materials[0], DPI_SCALE), 1.25f,
        SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("unique: NaN strength written as 0"), SolidKitTestScalar(unique, EFFECTS_STRENGTH), 0.0f,
        SOLIDKITTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitMaterialPathTest, "SOLTest.SolidMenuKit.Factory.MaterialPath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the loaded subsystem the factories draw with the shared style materials, the look in the tint (BrushTint):
// hull brushes, the button styles' Normal-look state brushes and disabled brush, the slider, the toggle (mark when
// checked, lit when hovered), the disabled art, and the menu font's typefaces
bool FSOLSolidKitMaterialPathTest::RunTest(const FString& /*parameters*/)
{
    FSOLSolidKitTestWorld testWorld;
    const USOLSolidMenuSubsystem* const subsystem = USOLSolidMenuSubsystem::Get(testWorld.World);
    if (!TestNotNull(TEXT("the world's subsystem"), subsystem))
    {
        return false;
    }

    // Hull brushes: the style's shared material, the look's brush tint, the image size
    const FVector2f size(SOLIDKITTEST_BRUSH_W, SOLIDKITTEST_BRUSH_H);
    const FSOLSolidButtonLook looks[] = { SolidKitTestSettled(ESOLSolidButtonState::Normal),
        SolidKitTestSettled(ESOLSolidButtonState::Hovered, true), SolidKitTestSettled(ESOLSolidButtonState::Disabled) };
    for (const ESOLSolidStyle style : SOLIDKITTEST_STYLES)
    {
        for (int32 l = 0; l < static_cast<int32>(UE_ARRAY_COUNT(looks)); ++l)
        {
            const FString name = FString::Printf(TEXT("hull style %d look %d"), static_cast<int32>(style), l);
            const FSlateBrush brush = SOLSolidStyle::MakeHullBrush(subsystem, style, size, looks[l]);
            TestTrue(name + TEXT(": the shared style material"),
                brush.GetResourceObject() == subsystem->GetStyleMaterial(style));
            SolidKitTestColorEqual(*this, name + TEXT(": BrushTint"), SolidKitTestTint(brush),
                SOLSolidStyle::BrushTint(looks[l]), SOLIDKITTEST_PACK_TOLERANCE);
            const FVector2f imageSize = brush.GetImageSize();
            TestTrue(name + TEXT(": image size"), imageSize.Equals(size, SOLIDKITTEST_TOLERANCE));
        }
    }

    // Button styles: Normal, Hovered and Pressed carry the Normal look; Disabled the disabled look
    const FLinearColor normalTint = SOLSolidStyle::BrushTint(FSOLSolidButtonLook());
    for (const ESOLSolidButtonVariant variant : SOLIDKITTEST_VARIANTS)
    {
        const FString name = FString::Printf(TEXT("button variant %d"), static_cast<int32>(variant));
        const FButtonStyle style = SOLSolidStyle::MakeButtonStyle(subsystem, variant);
        UMaterialInstanceDynamic* const material = subsystem->GetStyleMaterial(SOLSolidStyle::ButtonStyleOf(variant));
        const FSlateBrush* brushes[] = { &style.Normal, &style.Hovered, &style.Pressed, &style.Disabled };
        const TCHAR* brushNames[] = { TEXT("normal"), TEXT("hovered"), TEXT("pressed"), TEXT("disabled") };
        for (int32 b = 0; b < static_cast<int32>(UE_ARRAY_COUNT(brushes)); ++b)
        {
            TestTrue(name + TEXT(": ") + brushNames[b] + TEXT(" uses the variant's material"),
                brushes[b]->GetResourceObject() == material);
        }
        SolidKitTestColorEqual(*this, name + TEXT(": normal brush, the Normal look"), SolidKitTestTint(style.Normal),
            normalTint, SOLIDKITTEST_PACK_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(": hovered brush, the Normal look"),
            SolidKitTestTint(style.Hovered), normalTint, SOLIDKITTEST_PACK_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(": pressed brush, the Normal look"),
            SolidKitTestTint(style.Pressed), normalTint, SOLIDKITTEST_PACK_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(": disabled brush, the disabled look"),
            SolidKitTestTint(style.Disabled), SOLSolidStyle::BrushTint(SolidKitTestSettled(
            ESOLSolidButtonState::Disabled)), SOLIDKITTEST_PACK_TOLERANCE);
    }

    // Slider: groove and knob materials, the hover look on the hovered brushes
    {
        const FSliderStyle style = SOLSolidStyle::MakeSliderStyle(subsystem);
        UMaterialInstanceDynamic* const groove = subsystem->GetStyleMaterial(ESOLSolidStyle::SliderGroove);
        UMaterialInstanceDynamic* const knob = subsystem->GetStyleMaterial(ESOLSolidStyle::Knob);
        TestTrue(TEXT("slider: normal bar is the groove"), style.NormalBarImage.GetResourceObject() == groove);
        TestTrue(TEXT("slider: disabled bar is the groove"), style.DisabledBarImage.GetResourceObject() == groove);
        TestTrue(TEXT("slider: normal thumb is the knob"), style.NormalThumbImage.GetResourceObject() == knob);
        TestTrue(TEXT("slider: disabled thumb is the knob"), style.DisabledThumbImage.GetResourceObject() == knob);
        TestFalse(TEXT("slider: normal bar not disabled"), SolidKitTestDecodeBrush(style.NormalBarImage).bDisabled);
        TestTrue(TEXT("slider: hovered bar lit"),
            SolidKitTestDecodeBrush(style.HoveredBarImage).Edge >= SOLIDKITTEST_LIT_EDGE_MIN);
        TestTrue(TEXT("slider: hovered thumb lit"),
            SolidKitTestDecodeBrush(style.HoveredThumbImage).Edge >= SOLIDKITTEST_LIT_EDGE_MIN);
    }

    // Toggle: slot and puck materials, the mark when checked, lit when hovered
    {
        const FCheckBoxStyle style = SOLSolidStyle::MakeCheckBoxStyle(subsystem, false);
        UMaterialInstanceDynamic* const slot = subsystem->GetStyleMaterial(ESOLSolidStyle::ToggleSlot);
        UMaterialInstanceDynamic* const puck = subsystem->GetStyleMaterial(ESOLSolidStyle::Puck);
        TestTrue(TEXT("toggle: background is the slot"), style.BackgroundImage.GetResourceObject() == slot);
        TestTrue(TEXT("toggle: unchecked is the puck"), style.UncheckedImage.GetResourceObject() == puck);
        TestTrue(TEXT("toggle: checked is the puck"), style.CheckedImage.GetResourceObject() == puck);
        TestFalse(TEXT("toggle: unchecked has no mark (puck left)"),
            SolidKitTestDecodeBrush(style.UncheckedImage).bSelectedMark);
        TestTrue(TEXT("toggle: checked has the mark (puck right, cyan LED)"),
            SolidKitTestDecodeBrush(style.CheckedImage).bSelectedMark);
        TestTrue(TEXT("toggle: checked hovered keeps the mark"),
            SolidKitTestDecodeBrush(style.CheckedHoveredImage).bSelectedMark);
        TestTrue(TEXT("toggle: unchecked hovered lit"),
            SolidKitTestDecodeBrush(style.UncheckedHoveredImage).Edge >= SOLIDKITTEST_LIT_EDGE_MIN);
        TestTrue(TEXT("toggle: checked hovered lit"),
            SolidKitTestDecodeBrush(style.CheckedHoveredImage).Edge >= SOLIDKITTEST_LIT_EDGE_MIN);
        TestFalse(TEXT("toggle: unchecked not disabled"), SolidKitTestDecodeBrush(style.UncheckedImage).bDisabled);
    }

    // Disabled art and sizes through the materials
    SolidKitTestFactories(*this, TEXT("loaded subsystem"), subsystem, true);

    // The menu font's typefaces
    for (const ESOLMenuFontWeight weight : SOLIDKITTEST_WEIGHTS)
    {
        const FString name = FString::Printf(TEXT("font weight %d"), static_cast<int32>(weight));
        const FSlateFontInfo font = SOLSolidStyle::MenuFont(subsystem, weight, SOL::SolidMenu::FONT_BODY_SIZE);
        TestTrue(name + TEXT(": F_SOLMenu"), font.FontObject.Get() == subsystem->GetMenuFont());
        TestTrue(name + TEXT(": its typeface"), font.TypefaceFontName == SOLSolidStyle::TypefaceName(weight));
        TestEqual(name + TEXT(": size"), static_cast<float>(font.Size),
            static_cast<float>(SOL::SolidMenu::FONT_BODY_SIZE));
    }
    const FSlateFontInfo title = SOLSolidStyle::MenuFont(subsystem, ESOLMenuTextRole::Title, ESOLMenuFontWeight::Bold);
    TestTrue(TEXT("title font: F_SOLMenu"), title.FontObject.Get() == subsystem->GetMenuFont());
    TestEqual(TEXT("title font: size"), static_cast<float>(title.Size),
        static_cast<float>(SOL::SolidMenu::FONT_TITLE_SIZE));
    TestEqual(TEXT("title font: letter spacing"), static_cast<int32>(title.LetterSpacing),
        SOL::SolidMenu::LETTER_SPACING_TITLE);
    return true;
}

namespace
{
    // A written colour against an expected one: 8-bit vertex steps apart at most (a skipped write is under half a
    // tint level, the label blends with Edge)
    constexpr float SOLIDKITTEST_WRITE_TOLERANCE = 2.0f / 255.0f;
    constexpr float SOLIDKITTEST_PRODUCT_TOLERANCE = 1.0e-4f;      // Background x style tint against the written tint
    constexpr double SOLIDKITTEST_FRAME_S = 1.0 / 60.0;

    // A menu button set up as a menu builds one: in a widget tree whose owner widget is outered to a world (the hull
    // material path) or to the transient package (no world: the flat fallback)
    struct FSOLSolidKitTestButton
    {
        UUserWidget* Owner = nullptr;
        UWidgetTree* Tree = nullptr;
        USOLMenuButton* Button = nullptr;

        //////////////////////////////////////////////////////////////////////////
        // Builds the owner, its tree and the button (Setup with the button font size)
        FSOLSolidKitTestButton(UWorld* world, const ESOLSolidButtonVariant variant)
        {
            UObject* const outer = world != nullptr ? static_cast<UObject*>(world) : GetTransientPackage();
            Owner = NewObject<USOLSolidMenuSmokeBoard>(outer);
            Tree = NewObject<UWidgetTree>(Owner);
            Owner->WidgetTree = Tree;
            Button = Tree->ConstructWidget<USOLMenuButton>(USOLMenuButton::StaticClass());
            Button->Setup(*Tree, TEXT("TEST"), 3, SOLSolidStyle::FontSize(ESOLMenuTextRole::Button), variant);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the label colour a button wrote (its content text block's colour)
    FLinearColor SolidKitTestLabelColor(const USOLMenuButton& button)
    {
        const UTextBlock* const label = Cast<UTextBlock>(button.GetContent());
        return label != nullptr ? label->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Transparent;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the tint a button of a variant draws a look with: BrushTint on the material path, else FlatColor
    FLinearColor SolidKitTestExpectedTint(const USOLMenuButton& button, const FSOLSolidButtonLook& look)
    {
        return button.IsHullMaterialDrawn() ? SOLSolidStyle::BrushTint(look)
            : SOLSolidStyle::FlatColor(SOLSolidStyle::ButtonStyleOf(button.GetVariant()), look);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks what a button wrote to Slate shows a look: the look of the last refresh, the hull tint (and the
    // background colour times the style brush tint equal to it, SButton's product), the lift and the label colour
    void SolidKitTestButtonShows(FAutomationTestBase& test, const FString& label, const USOLMenuButton& button,
        const FSOLSolidButtonLook& expected)
    {
        SolidKitTestLookEqual(test, label + TEXT(": look"), button.GetAppliedLook(), expected);
        const FLinearColor tint = SolidKitTestExpectedTint(button, expected);
        SolidKitTestColorEqual(test, label + TEXT(": hull tint"), button.GetAppliedTint(), tint,
            SOLIDKITTEST_WRITE_TOLERANCE);
        const FLinearColor styleTint = button.GetStyle().Normal.TintColor.GetSpecifiedColor();
        const FLinearColor background = button.GetBackgroundColor();
        const FLinearColor product(background.R * styleTint.R, background.G * styleTint.G, background.B * styleTint.B,
            background.A * styleTint.A);
        SolidKitTestColorEqual(test, label + TEXT(": background x style tint = the written tint"), product,
            button.GetAppliedTint(), SOLIDKITTEST_PRODUCT_TOLERANCE);
        const float lift = static_cast<float>(button.GetRenderTransform().Translation.Y);
        if (!test.TestTrue(label + TEXT(": lift"), FMath::Abs(lift - SOLSolidMenu::LiftOffset(expected))
            <= SOL::SolidMenu::LIFT_EPSILON_PX))
        {
            test.AddError(FString::Printf(TEXT("%s: lift %.4f, expected %.4f"), *label, lift,
                SOLSolidMenu::LiftOffset(expected)));
        }
        SolidKitTestColorEqual(test, label + TEXT(": label colour"), SolidKitTestLabelColor(button),
            SOLSolidStyle::LabelColor(expected), SOLIDKITTEST_WRITE_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a settled look at a strength (EvaluateButton long after the change)
    FSOLSolidButtonLook SolidKitTestSettledAt(const ESOLSolidButtonState state, const float s, const bool bSelected)
    {
        return SOLSolidMenu::EvaluateButton(state, state, SOLIDKITTEST_SETTLED_S, s, bSelected);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitButtonNormalTest, "SOLTest.SolidMenuKit.Button.NormalAndSelected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every variant, flat and through the hull material: the first refresh shows the settled Normal look (no ramp in);
// selecting adds the cyan mark (tint B) and nothing else, deselecting removes it
bool FSOLSolidKitButtonNormalTest::RunTest(const FString& /*parameters*/)
{
    FSOLSolidKitTestWorld testWorld;
    UWorld* const worlds[] = { nullptr, testWorld.World };
    for (UWorld* const world : worlds)
    {
        for (const ESOLSolidButtonVariant variant : SOLIDKITTEST_VARIANTS)
        {
            const FString name = FString::Printf(TEXT("%s variant %d"), world != nullptr ? TEXT("material")
                : TEXT("flat"), static_cast<int32>(variant));
            FSOLSolidKitTestButton made(world, variant);
            USOLMenuButton& button = *made.Button;
            TestEqual(name + TEXT(": draws with the hull material"), button.IsHullMaterialDrawn(), world != nullptr);
            TestTrue(name + TEXT(": the variant"), button.GetVariant() == variant);
            TestEqual(name + TEXT(": the index"), button.GetIndex(), 3);

            button.RefreshHighlightAt(SOLIDKITTEST_T0, SOLIDKITTEST_S);
            SolidKitTestButtonShows(*this, name + TEXT(": normal"), button,
                SolidKitTestSettledAt(ESOLSolidButtonState::Normal, SOLIDKITTEST_S, false));

            button.SetSelected(true);
            TestTrue(name + TEXT(": IsSelected"), button.IsSelected());
            button.RefreshHighlightAt(SOLIDKITTEST_T0 + SOLIDKITTEST_FRAME_S, SOLIDKITTEST_S);
            const FSOLSolidButtonLook selected = SolidKitTestSettledAt(ESOLSolidButtonState::Normal, SOLIDKITTEST_S,
                true);
            SolidKitTestButtonShows(*this, name + TEXT(": selected"), button, selected);
            TestTrue(name + TEXT(": selected shows the mark"), button.GetAppliedLook().bSelectedMark);
            SolidKitTestColorEqual(*this, name + TEXT(": selected keeps the body text colour"),
                SolidKitTestLabelColor(button), SOLSolidStyle::TextColor(), SOLIDKITTEST_COLOR_TOLERANCE);

            button.SetSelected(false);
            button.RefreshHighlightAt(SOLIDKITTEST_T0 + 2.0 * SOLIDKITTEST_FRAME_S, SOLIDKITTEST_S);
            SolidKitTestButtonShows(*this, name + TEXT(": deselected"), button,
                SolidKitTestSettledAt(ESOLSolidButtonState::Normal, SOLIDKITTEST_S, false));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitButtonSelectedHoverTest, "SOLTest.SolidMenuKit.Button.SelectedWithHover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A selected button that is hovered ramps the amber hover look in and keeps the cyan mark (selected is orthogonal):
// mid-ramp the driver's look, settled the full hover with the mark, lifted by HOVER_LIFT_PX, the label bright; leaving
// hover ramps back to selected
bool FSOLSolidKitButtonSelectedHoverTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidKitTestWorld testWorld;
    FSOLSolidKitTestButton made(testWorld.World, ESOLSolidButtonVariant::Stack);
    USOLMenuButton& button = *made.Button;
    button.SetSelected(true);
    button.RefreshHighlightAt(SOLIDKITTEST_T0, SOLIDKITTEST_S);
    const FSOLSolidButtonLook selected = SolidKitTestSettledAt(ESOLSolidButtonState::Normal, SOLIDKITTEST_S, true);

    // Hover begins at T1; halfway through the ramp
    button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T1, SOLIDKITTEST_S);
    button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T1 + 0.5 * HOVER_RAMP_S, SOLIDKITTEST_S);
    SolidKitTestButtonShows(*this, TEXT("half hovered"), button,
        SolidKitTestFrom(ESOLSolidButtonState::Hovered, selected, 0.5 * HOVER_RAMP_S, true));

    // Settled hover with the mark
    button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T1 + 2.0 * HOVER_RAMP_S, SOLIDKITTEST_S);
    const FSOLSolidButtonLook hovered = SolidKitTestSettledAt(ESOLSolidButtonState::Hovered, SOLIDKITTEST_S, true);
    SolidKitTestButtonShows(*this, TEXT("hovered and selected"), button, hovered);
    TestTrue(TEXT("hovered and selected: the mark"), button.GetAppliedLook().bSelectedMark);
    TestEqual(TEXT("hovered and selected: full edge"), button.GetAppliedLook().Edge, 1.0f, SOLIDKITTEST_TOLERANCE);
    TestEqual(TEXT("hovered and selected: lifted"), static_cast<float>(button.GetRenderTransform().Translation.Y),
        -HOVER_LIFT_PX, LIFT_EPSILON_PX);
    SolidKitTestColorEqual(*this, TEXT("hovered and selected: bright label"), SolidKitTestLabelColor(button),
        SOLSolidStyle::BrightTextColor(), SOLIDKITTEST_COLOR_TOLERANCE);

    // Leaving hover ramps back to the selected look
    button.RefreshHighlightFor(ESOLSolidButtonState::Normal, SOLIDKITTEST_T2, SOLIDKITTEST_S);
    button.RefreshHighlightFor(ESOLSolidButtonState::Normal, SOLIDKITTEST_T2 + 2.0 * HOVER_RAMP_S, SOLIDKITTEST_S);
    SolidKitTestButtonShows(*this, TEXT("unhovered"), button, selected);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitButtonDisabledTest, "SOLTest.SolidMenuKit.Button.Disabled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetSolidDisabled gives the disabled art at once (no mark even when selected, DISABLED_ALPHA, the disabled label),
// makes the button hit-test invisible and not focusable and ignores hover; enabling restores visibility,
// focusability and the selected look
bool FSOLSolidKitButtonDisabledTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidKitTestWorld testWorld;
    UWorld* const worlds[] = { nullptr, testWorld.World };
    for (UWorld* const world : worlds)
    {
        const FString name = world != nullptr ? TEXT("material") : TEXT("flat");
        FSOLSolidKitTestButton made(world, ESOLSolidButtonVariant::Small);
        USOLMenuButton& button = *made.Button;
        TestTrue(name + TEXT(": focusable before"), button.GetIsFocusable());
        button.SetSelected(true);
        button.RefreshHighlightAt(SOLIDKITTEST_T0, SOLIDKITTEST_S);

        button.SetSolidDisabled(true);
        TestTrue(name + TEXT(": IsSolidDisabled"), button.IsSolidDisabled());
        TestFalse(name + TEXT(": not focusable while disabled"), button.GetIsFocusable());
        TestTrue(name + TEXT(": hit-test invisible"), button.GetVisibility() == ESlateVisibility::HitTestInvisible);
        TestTrue(name + TEXT(": still Slate-enabled (no Slate disabled dim)"), button.GetIsEnabled());
        button.RefreshHighlightAt(SOLIDKITTEST_T1, SOLIDKITTEST_S);
        const FSOLSolidButtonLook disabled = SolidKitTestSettledAt(ESOLSolidButtonState::Disabled, SOLIDKITTEST_S,
            true);
        SolidKitTestButtonShows(*this, name + TEXT(": disabled"), button, disabled);
        TestTrue(name + TEXT(": disabled flag"), button.GetAppliedLook().bDisabled);
        TestFalse(name + TEXT(": no mark while disabled"), button.GetAppliedLook().bSelectedMark);
        TestEqual(name + TEXT(": tint alpha DISABLED_ALPHA"), button.GetAppliedTint().A, DISABLED_ALPHA,
            SOLIDKITTEST_WRITE_TOLERANCE);
        SolidKitTestColorEqual(*this, name + TEXT(": disabled label"), SolidKitTestLabelColor(button),
            SOLSolidStyle::Linear(DISABLED_TEXT_COLOR), SOLIDKITTEST_COLOR_TOLERANCE);

        // Hover is ignored: the resolved state stays Disabled
        button.NotifyHovered();
        button.RefreshHighlightAt(SOLIDKITTEST_T1 + HOVER_RAMP_S, SOLIDKITTEST_S);
        SolidKitTestButtonShows(*this, name + TEXT(": disabled after a hover"), button, disabled);

        // Enabled again
        button.SetSolidDisabled(false);
        TestFalse(name + TEXT(": IsSolidDisabled cleared"), button.IsSolidDisabled());
        TestTrue(name + TEXT(": focusable again"), button.GetIsFocusable());
        TestTrue(name + TEXT(": visible again"), button.GetVisibility() == ESlateVisibility::Visible);
        button.RefreshHighlightAt(SOLIDKITTEST_T2, SOLIDKITTEST_S);
        button.RefreshHighlightAt(SOLIDKITTEST_T2 + 2.0 * HOVER_RAMP_S, SOLIDKITTEST_S);
        SolidKitTestButtonShows(*this, name + TEXT(": enabled"), button,
            SolidKitTestSettledAt(ESOLSolidButtonState::Normal, SOLIDKITTEST_S, true));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitButtonStrengthTest, "SOLTest.SolidMenuKit.Button.Strength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The strength paths: at 0 the hover look is there at once, flat but fully present; at 1 and 2 it ramps over
// HOVER_RAMP_S (the same amounts, the glow doubled at 2) and settles on the full hover
bool FSOLSolidKitButtonStrengthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidKitTestWorld testWorld;
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    for (const float s : strengths)
    {
        const FString name = FString::Printf(TEXT("s %.0f"), s);
        FSOLSolidKitTestButton made(testWorld.World, ESOLSolidButtonVariant::Tab);
        USOLMenuButton& button = *made.Button;
        button.RefreshHighlightAt(SOLIDKITTEST_T0, s);
        const FSOLSolidButtonLook normal = SolidKitTestSettledAt(ESOLSolidButtonState::Normal, s, false);
        button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T1, s);
        button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T1 + 0.25 * HOVER_RAMP_S, s);
        const FSOLSolidButtonLook quarter = SOLSolidMenu::EvaluateButtonFrom(ESOLSolidButtonState::Hovered, normal,
            0.25 * HOVER_RAMP_S, s, false);
        SolidKitTestButtonShows(*this, name + TEXT(": a quarter into the hover"), button, quarter);
        if (s == 0.0f)
        {
            TestEqual(name + TEXT(": instant full edge"), button.GetAppliedLook().Edge, 1.0f, SOLIDKITTEST_TOLERANCE);
            TestEqual(name + TEXT(": no glow"), button.GetAppliedLook().Glow, 0.0f, SOLIDKITTEST_TOLERANCE);
        }
        else
        {
            TestEqual(name + TEXT(": a quarter of the edge"), button.GetAppliedLook().Edge, 0.25f,
                SOLIDKITTEST_TOLERANCE);
            TestEqual(name + TEXT(": glow s x edge"), button.GetAppliedLook().Glow, 0.25f * s, SOLIDKITTEST_TOLERANCE);
        }
        button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T1 + 2.0 * HOVER_RAMP_S, s);
        SolidKitTestButtonShows(*this, name + TEXT(": settled hover"), button,
            SolidKitTestSettledAt(ESOLSolidButtonState::Hovered, s, false));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitButtonSettledTest, "SOLTest.SolidMenuKit.Button.SettledNoWrites",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A settled button's refreshes write nothing to Slate (normal, selected, hovered, pressed after its linger, disabled,
// at strengths 0, 1 and 2); a change of input writes again
bool FSOLSolidKitButtonSettledTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidKitTestWorld testWorld;
    struct FSOLSolidKitSettleCase
    {
        ESOLSolidButtonState State;
        bool bSelected;
        bool bDisabled;
    };
    const FSOLSolidKitSettleCase cases[] = { { ESOLSolidButtonState::Normal, false, false },
        { ESOLSolidButtonState::Normal, true, false }, { ESOLSolidButtonState::Hovered, false, false },
        { ESOLSolidButtonState::Hovered, true, false }, { ESOLSolidButtonState::Pressed, false, false },
        { ESOLSolidButtonState::Disabled, true, true } };
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    for (const float s : strengths)
    {
        for (const FSOLSolidKitSettleCase& settle : cases)
        {
            const FString name = FString::Printf(TEXT("s %.0f state %d selected %d"), s,
                static_cast<int32>(settle.State), settle.bSelected ? 1 : 0);
            FSOLSolidKitTestButton made(testWorld.World, ESOLSolidButtonVariant::Stack);
            USOLMenuButton& button = *made.Button;
            button.RefreshHighlightAt(SOLIDKITTEST_T0, s);
            button.SetSelected(settle.bSelected);
            button.SetSolidDisabled(settle.bDisabled);

            // Reach the state and let every ramp and linger end
            double now = SOLIDKITTEST_T1;
            button.RefreshHighlightFor(settle.State, now, s);
            now += 2.0 * (HOVER_RAMP_S + PRESSED_LINGER_S);
            button.RefreshHighlightFor(settle.State, now, s);
            const int32 writes = button.GetWriteCount();
            for (int32 frame = 0; frame < 10; ++frame)
            {
                now += SOLIDKITTEST_FRAME_S;
                button.RefreshHighlightFor(settle.State, now, s);
            }
            TestEqual(name + TEXT(": settled refreshes write nothing"), button.GetWriteCount(), writes);
        }
    }

    // A new input writes again
    FSOLSolidKitTestButton made(testWorld.World, ESOLSolidButtonVariant::Stack);
    USOLMenuButton& button = *made.Button;
    button.RefreshHighlightAt(SOLIDKITTEST_T0, SOLIDKITTEST_S);
    button.RefreshHighlightAt(SOLIDKITTEST_T0 + SOLIDKITTEST_FRAME_S, SOLIDKITTEST_S);
    const int32 writes = button.GetWriteCount();
    button.SetSelected(true);
    button.RefreshHighlightAt(SOLIDKITTEST_T1, SOLIDKITTEST_S);
    TestTrue(TEXT("selecting writes the mark"), button.GetWriteCount() > writes);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitButtonFineRampTest, "SOLTest.SolidMenuKit.Button.LiftFollowsFineRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The lift written always follows the look within LIFT_EPSILON_PX, however small each refresh's step: a release from
// a press to hover keeps Edge 1 (the tint does not change after the first frame) while the lift ramps 0 -> 1, here in
// steps of half LIFT_EPSILON_PX each (a frame step under the epsilon must not be compared against the previous,
// unwritten look); 1 ms steps for a Normal -> Hovered ramp likewise
bool FSOLSolidKitButtonFineRampTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidKitTestWorld testWorld;
    UWorld* const worlds[] = { nullptr, testWorld.World };
    for (UWorld* const world : worlds)
    {
        const FString name = world != nullptr ? TEXT("material") : TEXT("flat");

        // Press, then release to hover once the linger is over
        {
            FSOLSolidKitTestButton made(world, ESOLSolidButtonVariant::Stack);
            USOLMenuButton& button = *made.Button;
            button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T0, SOLIDKITTEST_S);
            button.RefreshHighlightFor(ESOLSolidButtonState::Pressed, SOLIDKITTEST_T1, SOLIDKITTEST_S);
            SolidKitTestButtonShows(*this, name + TEXT(": pressed"), button,
                SolidKitTestSettledAt(ESOLSolidButtonState::Pressed, SOLIDKITTEST_S, false));
            const double releaseS = SOLIDKITTEST_T1 + 2.0 * PRESSED_LINGER_S;
            const double stepS = 0.5 * LIFT_EPSILON_PX / HOVER_LIFT_PX * HOVER_RAMP_S;
            const int32 steps = FMath::CeilToInt32(2.0 * HOVER_RAMP_S / stepS);
            float worstLiftError = 0.0f;
            for (int32 step = 0; step <= steps; ++step)
            {
                button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, releaseS + step * stepS, SOLIDKITTEST_S);
                const float written = static_cast<float>(button.GetRenderTransform().Translation.Y);
                worstLiftError = FMath::Max(worstLiftError,
                    FMath::Abs(written - SOLSolidMenu::LiftOffset(button.GetAppliedLook())));
            }
            if (!TestTrue(name + TEXT(": release ramp: the written lift follows the look"),
                worstLiftError <= LIFT_EPSILON_PX + SOLIDKITTEST_TOLERANCE))
            {
                AddError(FString::Printf(TEXT("%s: release ramp: worst lift error %.4f px"), *name, worstLiftError));
            }
            SolidKitTestButtonShows(*this, name + TEXT(": release ramp settled"), button,
                SolidKitTestSettledAt(ESOLSolidButtonState::Hovered, SOLIDKITTEST_S, false));
            TestEqual(name + TEXT(": release ramp settled lifted"),
                static_cast<float>(button.GetRenderTransform().Translation.Y), -HOVER_LIFT_PX, LIFT_EPSILON_PX);
        }

        // Normal -> Hovered in 1 ms steps
        {
            FSOLSolidKitTestButton made(world, ESOLSolidButtonVariant::Tab);
            USOLMenuButton& button = *made.Button;
            button.RefreshHighlightAt(SOLIDKITTEST_T0, SOLIDKITTEST_S);
            const double stepS = 0.001;
            const int32 steps = FMath::CeilToInt32(2.0 * HOVER_RAMP_S / stepS);
            float worstLiftError = 0.0f;
            for (int32 step = 0; step <= steps; ++step)
            {
                button.RefreshHighlightFor(ESOLSolidButtonState::Hovered, SOLIDKITTEST_T1 + step * stepS,
                    SOLIDKITTEST_S);
                const float written = static_cast<float>(button.GetRenderTransform().Translation.Y);
                worstLiftError = FMath::Max(worstLiftError,
                    FMath::Abs(written - SOLSolidMenu::LiftOffset(button.GetAppliedLook())));
            }
            TestTrue(name + TEXT(": 1 ms ramp: the written lift follows the look"),
                worstLiftError <= LIFT_EPSILON_PX + SOLIDKITTEST_TOLERANCE);
            SolidKitTestButtonShows(*this, name + TEXT(": 1 ms ramp settled"), button,
                SolidKitTestSettledAt(ESOLSolidButtonState::Hovered, SOLIDKITTEST_S, false));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitButtonDivideTest, "SOLTest.SolidMenuKit.Button.BackgroundDividesStyleTint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The style brushes carry the Normal look's tint (BrushTint(Normal), or FlatColor(Normal) flat) and SButton multiplies
// it by the background colour, so the button writes BrushTint(look) / BrushTint(Normal) per channel: the product is
// the look's tint for every state, and the Normal look's background is white
bool FSOLSolidKitButtonDivideTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::SolidMenu;
    FSOLSolidKitTestWorld testWorld;
    UWorld* const worlds[] = { nullptr, testWorld.World };
    for (UWorld* const world : worlds)
    {
        const FString name = world != nullptr ? TEXT("material") : TEXT("flat");
        FSOLSolidKitTestButton made(world, ESOLSolidButtonVariant::Stack);
        USOLMenuButton& button = *made.Button;
        const FSOLSolidButtonLook normalLook;
        const FLinearColor styleTint = button.GetStyle().Normal.TintColor.GetSpecifiedColor();
        SolidKitTestColorEqual(*this, name + TEXT(": the style tint is the Normal look's"), styleTint,
            SolidKitTestExpectedTint(button, normalLook), SOLIDKITTEST_PACK_TOLERANCE);
        button.RefreshHighlightAt(SOLIDKITTEST_T0, SOLIDKITTEST_S);
        SolidKitTestColorEqual(*this, name + TEXT(": Normal background white"), button.GetBackgroundColor(),
            FLinearColor::White, SOLIDKITTEST_PRODUCT_TOLERANCE);

        // Each state: background = tint / style tint
        const ESOLSolidButtonState states[] = { ESOLSolidButtonState::Hovered, ESOLSolidButtonState::Pressed,
            ESOLSolidButtonState::Selected, ESOLSolidButtonState::Normal };
        double now = SOLIDKITTEST_T1;
        for (const ESOLSolidButtonState state : states)
        {
            const FString stateName = FString::Printf(TEXT("%s state %d"), *name, static_cast<int32>(state));
            button.RefreshHighlightFor(state, now, SOLIDKITTEST_S);
            now += 2.0 * (HOVER_RAMP_S + PRESSED_LINGER_S);
            button.RefreshHighlightFor(state, now, SOLIDKITTEST_S);
            now += 1.0;
            const FSOLSolidButtonLook look = SolidKitTestSettledAt(state, SOLIDKITTEST_S, false);
            SolidKitTestButtonShows(*this, stateName, button, look);
            const FLinearColor tint = SolidKitTestExpectedTint(button, look);
            const FLinearColor expected(tint.R / styleTint.R, tint.G / styleTint.G, tint.B / styleTint.B,
                tint.A / styleTint.A);
            SolidKitTestColorEqual(*this, stateName + TEXT(": background tint / style tint"),
                button.GetBackgroundColor(), expected, SOLIDKITTEST_PRODUCT_TOLERANCE);
        }
        button.SetSolidDisabled(true);
        button.RefreshHighlightAt(now, SOLIDKITTEST_S);
        SolidKitTestButtonShows(*this, name + TEXT(": disabled"), button,
            SolidKitTestSettledAt(ESOLSolidButtonState::Disabled, SOLIDKITTEST_S, false));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidKitPanelStyleTest, "SOLTest.SolidMenuKit.Panel.UniqueMaterialFollowsStyle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A hull with a unique shape gets its own material from its style; the same shape in another style gets a new one
// from that style (its parent is that style's instance), and a plain hull draws with the shared material
bool FSOLSolidKitPanelStyleTest::RunTest(const FString& /*parameters*/)
{
    FSOLSolidKitTestWorld testWorld;
    const USOLSolidMenuSubsystem* const subsystem = USOLSolidMenuSubsystem::Get(testWorld.World);
    if (!TestNotNull(TEXT("the world's subsystem"), subsystem))
    {
        return false;
    }
    UUserWidget* const owner = NewObject<USOLSolidMenuSmokeBoard>(testWorld.World);
    owner->WidgetTree = NewObject<UWidgetTree>(owner);
    USOLSolidPanel* const panel = owner->WidgetTree->ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass());

    // Returns the parent of a style's shared material
    const auto parentOf = [subsystem](const ESOLSolidStyle style) -> const UMaterialInterface*
    {
        const UMaterialInstanceDynamic* const shared = subsystem->GetStyleMaterial(style);
        return shared != nullptr ? shared->Parent.Get() : nullptr;
    };

    FSOLSolidHull chin;
    chin.ChamferTL = 12.0f;
    chin.ChinWidth = 120.0f;
    chin.ChinHeight = 16.0f;
    panel->SetHull(ESOLSolidStyle::Panel, chin);
    TestTrue(TEXT("panel with a chin: its own material"), panel->HasUniqueMaterial());
    TestTrue(TEXT("panel with a chin: drawn with the material"), panel->IsHullMaterialDrawn());

    panel->SetHull(ESOLSolidStyle::Recess, chin);
    TestTrue(TEXT("recess with a chin: its own material"), panel->HasUniqueMaterial());
    TestEqual(TEXT("recess style"), static_cast<int32>(panel->GetStyle()), static_cast<int32>(ESOLSolidStyle::Recess));
    TArray<UObject*> children;
    GetObjectsWithOuter(panel, children, EGetObjectsFlags::None);
    const UMaterialInstanceDynamic* unique = nullptr;
    for (UObject* const child : children)
    {
        const UMaterialInstanceDynamic* const material = Cast<UMaterialInstanceDynamic>(child);
        if (material != nullptr && material->Parent == parentOf(ESOLSolidStyle::Recess))
        {
            unique = material;
        }
    }
    TestNotNull(TEXT("recess with a chin: a material made from the recess instance"), unique);

    FSOLSolidHull plain;
    plain.ChamferTL = 12.0f;
    panel->SetHull(ESOLSolidStyle::Panel, plain);
    TestFalse(TEXT("plain panel: the shared material"), panel->HasUniqueMaterial());
    TestTrue(TEXT("plain panel: drawn with the material"), panel->IsHullMaterialDrawn());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
