/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyCaps.h"

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"

#include "String/ParseTokens.h"

namespace
{
    // The modifier cap of a chord binding: its engine key name and its legend (the BindingDisplayName prefix without
    // its "+")
    struct FKeyCapsModifier
    {
        const TCHAR* KeyName;
        const TCHAR* Label;
    };

    // One modifier cap per SOLKeyBindings::EKeyChord (None has none)
    constexpr FKeyCapsModifier KEYCAPS_CHORD_MODIFIERS[] =
    {
        { nullptr, nullptr },
        { TEXT("LeftShift"), TEXT("Shift") },
        { TEXT("LeftAlt"), TEXT("Alt") },
        { TEXT("LeftControl"), TEXT("Ctrl") },
        { TEXT("RightShift"), TEXT("RShift") },
    };
    static_assert(UE_ARRAY_COUNT(KEYCAPS_CHORD_MODIFIERS)
        == static_cast<int32>(SOLKeyBindings::EKeyChord::RightShift) + 1, "one modifier cap per chord");

    // The keys drawn as wide caps (SpaceBar is the widest, KEYCAP_SPACE_UNITS)
    const TCHAR* const KEYCAPS_WIDE_KEYS[] =
    {
        TEXT("SpaceBar"), TEXT("BackSpace"), TEXT("Tab"), TEXT("Enter"), TEXT("LeftShift"), TEXT("RightShift"),
        TEXT("LeftControl"), TEXT("RightControl"), TEXT("LeftAlt"), TEXT("RightAlt"), TEXT("CapsLock"),
    };
    const TCHAR* const KEYCAPS_SPACE_KEY = TEXT("SpaceBar");

    // A mouse button or axis and the part of the mouse glyph it lights
    struct FKeyCapsMouseKey
    {
        const TCHAR* KeyName;
        ESOLMouseButton Button;
    };

    constexpr FKeyCapsMouseKey KEYCAPS_MOUSE_KEYS[] =
    {
        { TEXT("LeftMouseButton"), ESOLMouseButton::Left },
        { TEXT("RightMouseButton"), ESOLMouseButton::Right },
        { TEXT("MiddleMouseButton"), ESOLMouseButton::Middle },
        { TEXT("MouseWheelAxis"), ESOLMouseButton::Wheel },
        { TEXT("MouseScrollUp"), ESOLMouseButton::Wheel },
        { TEXT("MouseScrollDown"), ESOLMouseButton::Wheel },
        { TEXT("Mouse2D"), ESOLMouseButton::Move },
        { TEXT("MouseX"), ESOLMouseButton::Move },
        { TEXT("MouseY"), ESOLMouseButton::Move },
    };

    // Fills a model in place: caps and chords are overwritten from the front (their strings keep their buffers), and
    // Finish drops the rest, so parsing into the same model again allocates nothing
    struct FKeyCapsWriter
    {
        FSOLKeyCapModel& Model;
        int32 CapCount = 0;
        int32 ChordCount = 0;

        //////////////////////////////////////////////////////////////////////////
        // Starts a writer over a model
        explicit FKeyCapsWriter(FSOLKeyCapModel& model)
            : Model(model)
        {
        }

        //////////////////////////////////////////////////////////////////////////
        // Starts a new chord (alternative) naming a binding (INDEX_NONE for a fixed chord or an unknown id)
        void BeginChord(const int32 binding)
        {
            if (ChordCount == Model.Chords.Num())
            {
                Model.Chords.AddDefaulted();
            }
            FSOLKeyCapChord& chord = Model.Chords[ChordCount++];
            chord.FirstCap = CapCount;
            chord.CapCount = 0;
            chord.Binding = binding;
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns the next cap of the current chord, reusing an old cap's strings
        FSOLKeyCap& NextCap()
        {
            if (CapCount == Model.Caps.Num())
            {
                Model.Caps.AddDefaulted();
            }
            ++Model.Chords[ChordCount - 1].CapCount;
            return Model.Caps[CapCount++];
        }

        //////////////////////////////////////////////////////////////////////////
        // Returns the current chord's cap count
        int32 CurrentChordCaps() const
        {
            return ChordCount > 0 ? Model.Chords[ChordCount - 1].CapCount : 0;
        }

        //////////////////////////////////////////////////////////////////////////
        // Drops the caps and chords past the written ones, keeping the buffers
        void Finish()
        {
            Model.Caps.SetNum(CapCount, EAllowShrinking::No);
            Model.Chords.SetNum(ChordCount, EAllowShrinking::No);
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Writes a string view into a string, keeping its buffer
    void KeyCapsAssign(FString& out, const FStringView text)
    {
        out.Reset();
        out.Append(text.GetData(), text.Len());
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets a cap's shape, mouse part and width from its key name, as a bound cap
    void KeyCapsShape(FSOLKeyCap& cap)
    {
        cap.Shape = SOLKeyCaps::WidthClass(cap.KeyName);
        cap.Mouse = SOLKeyCaps::MouseButtonOf(cap.KeyName);
        cap.WidthUnits = SOLKeyCaps::WidthUnits(cap.KeyName);
        cap.bUnbound = false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds an engine key's cap to the current chord (its legend from SOLBindingLabel)
    void KeyCapsAddKey(FKeyCapsWriter& writer, const FStringView keyName)
    {
        FSOLKeyCap& cap = writer.NextCap();
        KeyCapsAssign(cap.KeyName, keyName);
        SOLBindingLabel::AssignKeyDisplayName(cap.KeyName, cap.Label);
        KeyCapsShape(cap);
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds the "Unbound" cap to the current chord (no key, dim recess)
    void KeyCapsAddUnbound(FKeyCapsWriter& writer)
    {
        FSOLKeyCap& cap = writer.NextCap();
        cap.KeyName.Reset();
        KeyCapsAssign(cap.Label, SOLMenuText::KEY_UNBOUND);
        cap.Shape = ESOLKeyCapShape::Normal;
        cap.Mouse = ESOLMouseButton::None;
        cap.WidthUnits = 1.0f;
        cap.bUnbound = true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds one binding as a chord: its modifier cap then its live key's cap, or one unbound cap without a key
    void KeyCapsAddBinding(FKeyCapsWriter& writer, const int32 binding, const TArray<FString>& liveKeys)
    {
        const bool bValid = binding >= 0 && binding < SOLKeyBindings::BINDING_COUNT;
        writer.BeginChord(bValid ? binding : INDEX_NONE);
        if (!bValid || !liveKeys.IsValidIndex(binding) || liveKeys[binding].IsEmpty())
        {
            KeyCapsAddUnbound(writer);
            return;
        }

        // The chord's modifier first (Shift, Alt, Ctrl or RShift), legend as BindingDisplayName's prefix
        const FKeyCapsModifier& modifier =
            KEYCAPS_CHORD_MODIFIERS[static_cast<int32>(SOLKeyBindings::GetChord(binding))];
        if (modifier.KeyName != nullptr)
        {
            FSOLKeyCap& cap = writer.NextCap();
            KeyCapsAssign(cap.KeyName, modifier.KeyName);
            KeyCapsAssign(cap.Label, modifier.Label);
            KeyCapsShape(cap);
        }
        KeyCapsAddKey(writer, liveKeys[binding]);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a key cap width read from the caller's widths: a missing, negative or not finite one is the minimum
    float KeyCapsReadWidth(const TConstArrayView<float> capWidths, const int32 cap)
    {
        if (!capWidths.IsValidIndex(cap))
        {
            return SOL::Help::KEYCAP_MIN_WIDTH;
        }
        const float width = capWidths[cap];
        return FMath::IsFinite(width) && width >= 0.0f ? width : SOL::Help::KEYCAP_MIN_WIDTH;
    }
}

//////////////////////////////////////////////////////////////////////////
// Parses a bindings cell into caps and alternatives, reusing the model's buffers
void SOLKeyCaps::ParseCell(const TCHAR* cell, const TArray<FString>& liveKeys, FSOLKeyCapModel& out)
{
    FKeyCapsWriter writer(out);
    const FStringView text = cell != nullptr ? FStringView(cell) : FStringView();
    UE::String::ParseTokens(text, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER[0], [&](const FStringView alternative)
    {
        // '@Id': the action's live key (an unknown id or a missing key is one unbound cap)
        if (alternative[0] == SOLMenuText::BINDING_ID_PREFIX)
        {
            const int32 binding = SOLKeyBindings::FindById(alternative.RightChop(1));
            if (binding == INDEX_NONE)
            {
                writer.BeginChord(INDEX_NONE);
                KeyCapsAddUnbound(writer);
                return;
            }
            KeyCapsAddBinding(writer, binding, liveKeys);
            return;
        }

        // A '+' chord of fixed engine key names (all empty reads as unbound, as SOLBindingLabel::JoinChord)
        writer.BeginChord(INDEX_NONE);
        UE::String::ParseTokens(alternative, SOLMenuText::BINDING_CHORD_DELIMITER[0], [&](const FStringView keyName)
        {
            KeyCapsAddKey(writer, keyName);
        }, UE::String::EParseTokensOptions::SkipEmpty);
        if (writer.CurrentChordCaps() == 0)
        {
            KeyCapsAddUnbound(writer);
        }
    }, UE::String::EParseTokensOptions::SkipEmpty);

    // A null or empty cell is one unbound cap
    if (writer.ChordCount == 0)
    {
        writer.BeginChord(INDEX_NONE);
        KeyCapsAddUnbound(writer);
    }
    writer.Finish();
}

//////////////////////////////////////////////////////////////////////////
// Parses one binding into its chord's caps, reusing the model's buffers
void SOLKeyCaps::ParseBinding(const int32 binding, const TArray<FString>& liveKeys, FSOLKeyCapModel& out)
{
    FKeyCapsWriter writer(out);
    KeyCapsAddBinding(writer, binding, liveKeys);
    writer.Finish();
}

//////////////////////////////////////////////////////////////////////////
// Returns a key's cap shape: Mouse for the mouse buttons and axes, Wide for the wide keys, else Normal
ESOLKeyCapShape SOLKeyCaps::WidthClass(const FString& keyName)
{
    if (MouseButtonOf(keyName) != ESOLMouseButton::None)
    {
        return ESOLKeyCapShape::Mouse;
    }
    for (const TCHAR* wide : KEYCAPS_WIDE_KEYS)
    {
        if (FCString::Stricmp(*keyName, wide) == 0)
        {
            return ESOLKeyCapShape::Wide;
        }
    }
    return ESOLKeyCapShape::Normal;
}

//////////////////////////////////////////////////////////////////////////
// Returns the mouse part a key lights (None for a keyboard key)
ESOLMouseButton SOLKeyCaps::MouseButtonOf(const FString& keyName)
{
    for (const FKeyCapsMouseKey& mouse : KEYCAPS_MOUSE_KEYS)
    {
        if (FCString::Stricmp(*keyName, mouse.KeyName) == 0)
        {
            return mouse.Button;
        }
    }
    return ESOLMouseButton::None;
}

//////////////////////////////////////////////////////////////////////////
// Returns a key's cap width in key units: Space the widest, the other wide keys KEYCAP_WIDE_UNITS, else 1
float SOLKeyCaps::WidthUnits(const FString& keyName)
{
    if (WidthClass(keyName) != ESOLKeyCapShape::Wide)
    {
        return 1.0f;
    }
    return FCString::Stricmp(*keyName, KEYCAPS_SPACE_KEY) == 0 ? SOL::Help::KEYCAP_SPACE_UNITS
        : SOL::Help::KEYCAP_WIDE_UNITS;
}

//////////////////////////////////////////////////////////////////////////
// Returns a cap's drawn width: its legend plus the padding a side, at least the minimum times its key units
float SOLKeyCaps::CapWidth(const FSOLKeyCap& cap, const float legendWidth)
{
    using namespace SOL::Help;
    const float legend = FMath::IsFinite(legendWidth) && legendWidth > 0.0f ? legendWidth : 0.0f;
    const float units = FMath::IsFinite(cap.WidthUnits) && cap.WidthUnits > 0.0f ? cap.WidthUnits : 1.0f;
    return FMath::Max(legend + 2.0f * KEYCAP_PADDING, KEYCAP_MIN_WIDTH * units);
}

//////////////////////////////////////////////////////////////////////////
// Returns the model's text form: each chord's legends joined by "+", the chords by " / "
FString SOLKeyCaps::ModelText(const FSOLKeyCapModel& model)
{
    FString text;
    for (int32 chordIndex = 0; chordIndex < model.Chords.Num(); ++chordIndex)
    {
        if (chordIndex > 0)
        {
            text.Append(SOLMenuText::BINDING_ALTERNATIVE_SEPARATOR);
        }
        const FSOLKeyCapChord& chord = model.Chords[chordIndex];
        for (int32 cap = chord.FirstCap; cap < chord.FirstCap + chord.CapCount; ++cap)
        {
            if (!model.Caps.IsValidIndex(cap))
            {
                continue;
            }
            if (cap > chord.FirstCap)
            {
                text.Append(SOLMenuText::BINDING_CHORD_DELIMITER);
            }
            text.Append(model.Caps[cap].Label);
        }
    }
    return text;
}

//////////////////////////////////////////////////////////////////////////
// Lays a cap row out in a width, wrapping before an alternative that would cross it
float SOLKeyCaps::LayoutRow(const FSOLKeyCapModel& model, const TConstArrayView<float> capWidths,
    const FSOLKeyCapMetrics& metrics, const float availableWidth, FSOLKeyCapLayout& out)
{
    out.Reset();
    if (model.Caps.IsEmpty())
    {
        return 0.0f;
    }
    const bool bWrap = FMath::IsFinite(availableWidth) && availableWidth > 0.0f;
    const float lineStep = metrics.CapHeight + metrics.RowGap;
    const float plusAdvance = metrics.Gap + metrics.PlusWidth + metrics.Gap;
    const float separatorAdvance = metrics.Gap + metrics.SeparatorWidth + metrics.Gap;
    int32 line = 0;
    float x = 0.0f;
    bool bLineUsed = false;

    // Adds one piece at the pen and moves the pen past it
    const auto addItem = [&](const ESOLKeyCapItem kind, const int32 cap, const float itemX, const float width)
    {
        FSOLKeyCapLayoutItem& item = out.Items.AddDefaulted_GetRef();
        item.Kind = kind;
        item.Cap = cap;
        item.X = itemX;
        item.Y = line * lineStep;
        item.Width = width;
        item.Line = line;
        out.Width = FMath::Max(out.Width, itemX + width);
        bLineUsed = true;
    };

    // Starts a new line at the left edge
    const auto newLine = [&]()
    {
        ++line;
        x = 0.0f;
        bLineUsed = false;
    };

    // A model without chords is laid out as one chord of all its caps
    const int32 chordCount = FMath::Max(model.Chords.Num(), 1);
    for (int32 chordIndex = 0; chordIndex < chordCount; ++chordIndex)
    {
        int32 firstCap = 0;
        int32 lastCap = model.Caps.Num();
        if (model.Chords.IsValidIndex(chordIndex))
        {
            firstCap = FMath::Clamp(model.Chords[chordIndex].FirstCap, 0, model.Caps.Num());
            lastCap = FMath::Clamp(firstCap + model.Chords[chordIndex].CapCount, firstCap, model.Caps.Num());
        }
        if (lastCap == firstCap)
        {
            continue;
        }

        // The chord's whole width: its caps and the "+" pieces between them
        float chordWidth = 0.0f;
        for (int32 cap = firstCap; cap < lastCap; ++cap)
        {
            chordWidth += KeyCapsReadWidth(capWidths, cap) + (cap > firstCap ? plusAdvance : 0.0f);
        }

        // The " / " before an alternative, or a new line when the alternative would cross the width
        if (bLineUsed)
        {
            if (bWrap && x + separatorAdvance + chordWidth > availableWidth)
            {
                newLine();
            }
            else
            {
                addItem(ESOLKeyCapItem::Separator, INDEX_NONE, x + metrics.Gap, metrics.SeparatorWidth);
                x += separatorAdvance;
            }
        }

        // The caps; a chord wider than the width alone is split between its caps (the "+" ends the line if it fits)
        const bool bSplit = bWrap && chordWidth > availableWidth;
        for (int32 cap = firstCap; cap < lastCap; ++cap)
        {
            const float width = KeyCapsReadWidth(capWidths, cap);
            if (cap > firstCap)
            {
                if (bSplit && x + plusAdvance + width > availableWidth)
                {
                    const bool bPlusFits = x + metrics.Gap + metrics.PlusWidth <= availableWidth;
                    if (bPlusFits)
                    {
                        addItem(ESOLKeyCapItem::Plus, INDEX_NONE, x + metrics.Gap, metrics.PlusWidth);
                    }
                    newLine();
                    if (!bPlusFits)
                    {
                        addItem(ESOLKeyCapItem::Plus, INDEX_NONE, 0.0f, metrics.PlusWidth);
                        x = metrics.PlusWidth + metrics.Gap;
                    }
                }
                else
                {
                    addItem(ESOLKeyCapItem::Plus, INDEX_NONE, x + metrics.Gap, metrics.PlusWidth);
                    x += plusAdvance;
                }
            }
            addItem(ESOLKeyCapItem::Cap, cap, x, width);
            x += width;
        }
    }

    out.LineCount = out.Items.IsEmpty() ? 0 : line + 1;
    out.Height = out.LineCount > 0 ? out.LineCount * metrics.CapHeight + (out.LineCount - 1) * metrics.RowGap : 0.0f;
    return out.Height;
}

//////////////////////////////////////////////////////////////////////////
// Returns the legend drawn on a cap: none on the mouse buttons (the glyph shows them), "Move" on a mouse-move cap
FStringView SOLKeyCaps::DrawnLegend(const FSOLKeyCap& cap)
{
    if (cap.Shape == ESOLKeyCapShape::Mouse)
    {
        switch (cap.Mouse)
        {
        case ESOLMouseButton::Left:
        case ESOLMouseButton::Right:
        case ESOLMouseButton::Middle:
            return FStringView();
        case ESOLMouseButton::Move:
            return SOLMenuText::KEYCAP_MOVE_LEGEND;
        default:
            break;
        }
    }
    return cap.Label;
}

//////////////////////////////////////////////////////////////////////////
// Returns a cap's content width: the legend, after the glyph and a gap on a Mouse cap
float SOLKeyCaps::ContentWidth(const FSOLKeyCap& cap, const float legendWidth)
{
    const float legend = legendWidth > 0.0f ? legendWidth : 0.0f;
    if (cap.Shape != ESOLKeyCapShape::Mouse)
    {
        return legend;
    }
    return SOL::Help::KEYCAP_MOUSE_GLYPH_WIDTH + (legend > 0.0f ? SOL::Help::KEYCAP_GAP + legend : 0.0f);
}

//////////////////////////////////////////////////////////////////////////
// Returns where a cap's content may start: past the Small hull's LED on a bound cap, the padding on a recess cap
float SOLKeyCaps::ContentLeft(const FSOLKeyCap& cap)
{
    return cap.bUnbound ? SOL::Help::KEYCAP_PADDING : SOL::Help::KEYCAP_LED_ROOM;
}

//////////////////////////////////////////////////////////////////////////
// Returns a cap's drawn width: its content left of ContentLeft's room and the right padding, at least the minimum
float SOLKeyCaps::DrawnCapWidth(const FSOLKeyCap& cap, const float legendWidth)
{
    return CapWidth(cap, ContentWidth(cap, legendWidth) + ContentLeft(cap) - SOL::Help::KEYCAP_PADDING);
}

//////////////////////////////////////////////////////////////////////////
// Returns the glyph part a mouse button lights
ESOLMouseGlyphPart SOLKeyCaps::GlyphPartOf(const ESOLMouseButton button)
{
    switch (button)
    {
    case ESOLMouseButton::Left:
        return ESOLMouseGlyphPart::LeftButton;
    case ESOLMouseButton::Right:
        return ESOLMouseGlyphPart::RightButton;
    case ESOLMouseButton::Middle:
    case ESOLMouseButton::Wheel:
        return ESOLMouseGlyphPart::Wheel;
    case ESOLMouseButton::Move:
        return ESOLMouseGlyphPart::Body;
    default:
        return ESOLMouseGlyphPart::None;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the mouse glyph in a box: the body, the two buttons across its top, the wheel slot centred between them
FSOLMouseGlyph SOLKeyCaps::MouseGlyph(const FVector2f& topLeft, const FVector2f& size, const ESOLMouseButton button)
{
    using namespace SOL::Help;
    FSOLMouseGlyph glyph;
    glyph.Lit = GlyphPartOf(button);
    const bool bValid = FMath::IsFinite(size.X) && FMath::IsFinite(size.Y) && size.X > 0.0f && size.Y > 0.0f;
    if (!bValid)
    {
        const FBox2f point(topLeft, topLeft);
        glyph.Body = glyph.LeftButton = glyph.RightButton = glyph.Wheel = point;
        return glyph;
    }
    const float centreX = topLeft.X + 0.5f * size.X;
    const float buttonsBottom = topLeft.Y + size.Y * KEYCAP_MOUSE_BUTTON_FRACTION;
    const float wheelHalf = 0.5f * size.X * KEYCAP_MOUSE_WHEEL_WIDTH_FRACTION;
    glyph.Body = FBox2f(topLeft, topLeft + size);
    glyph.LeftButton = FBox2f(topLeft, FVector2f(centreX, buttonsBottom));
    glyph.RightButton = FBox2f(FVector2f(centreX, topLeft.Y), FVector2f(topLeft.X + size.X, buttonsBottom));
    glyph.Wheel = FBox2f(FVector2f(centreX - wheelHalf, topLeft.Y + size.Y * KEYCAP_MOUSE_WHEEL_TOP_FRACTION),
        FVector2f(centreX + wheelHalf, topLeft.Y + size.Y * KEYCAP_MOUSE_WHEEL_BOTTOM_FRACTION));
    return glyph;
}
