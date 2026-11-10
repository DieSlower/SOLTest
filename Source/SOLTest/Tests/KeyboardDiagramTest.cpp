/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyDiagram.h"
#include "Menu/SOLKeyboardDiagram.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33c keyboard diagram widget (Docs/SDDs/33-player-documentation.md sections 7, 10 and 11,
// D13, D32), written against the header contract (Menu/SOLKeyboardDiagram.h) before the production code existed. They
// pin USOLKeyboardDiagram's headless state: the model rebuilt only when the keys change, the panel laid out in the
// layout width, the hover from a local point or an index (a mirror key hovers its main key), the hovered key's detail
// lines and the paint verification before any paint. No test calls TakeWidget; the Slate painting is checked by the
// smoke. The 33c revision (contract section E, D34-D37), also test-first: the panel fixed while hovering and the
// measured shell width stacking. Part 33e-iii (D45): the hover strip gives way to a popup, so the panel is
// LayoutPanel(width, overflow, unbound) with the hint line and no line budget.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float KEYBOARDDIAGRAMTEST_TOLERANCE = 1.0e-3f;    // Unit and pixel comparisons
    constexpr float KEYBOARDDIAGRAMTEST_WIDE = 1000.0f;         // A wide width: the one-row layout, mouse beside
    constexpr float KEYBOARDDIAGRAMTEST_NARROW = 658.0f;        // The measured 1280x720 shell width: stacked (D34)
    constexpr float KEYBOARDDIAGRAMTEST_MAIN_WIDTH = 15.0f;     // The main block's key units (the stacked unit's span)
    constexpr float KEYBOARDDIAGRAMTEST_GRID_STEP = 0.1f;       // Gap search spacing (key units)
    constexpr float KEYBOARDDIAGRAMTEST_GRID_OFFSET = 0.0333f;  // Keeps the samples off the key edges
    constexpr int32 KEYBOARDDIAGRAMTEST_FIRE = static_cast<int32>(SOLKeyBindings::EBinding::Fire);
    constexpr int32 KEYBOARDDIAGRAMTEST_DROP = static_cast<int32>(SOLKeyBindings::EBinding::DropTarget);
    constexpr int32 KEYBOARDDIAGRAMTEST_WARP_RESET = static_cast<int32>(SOLKeyBindings::EBinding::WarpReset);

    //////////////////////////////////////////////////////////////////////////
    // Returns a fresh diagram widget in the transient package (never given a Slate widget)
    USOLKeyboardDiagram* KeyboardDiagramTestNew()
    {
        return NewObject<USOLKeyboardDiagram>(GetTransientPackage());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the default keys with Fire moved from the left mouse button to K
    TArray<FString> KeyboardDiagramTestFireOnK()
    {
        TArray<FString> keys = SOLKeyBindings::DefaultKeys();
        keys[KEYBOARDDIAGRAMTEST_FIRE] = TEXT("K");
        return keys;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the default keys with DropTarget on a key off the layout and WarpReset unbound
    TArray<FString> KeyboardDiagramTestOverflowAndUnbound()
    {
        TArray<FString> keys = SOLKeyBindings::DefaultKeys();
        keys[KEYBOARDDIAGRAMTEST_DROP] = TEXT("F13");
        keys[KEYBOARDDIAGRAMTEST_WARP_RESET] = FString();
        return keys;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a model key holds an assignment of a binding
    bool KeyboardDiagramTestHolds(const FSOLKeyDiagramModel& model, const int32 keyIndex, const int32 binding)
    {
        if (!model.Keys.IsValidIndex(keyIndex))
        {
            return false;
        }
        const FSOLDiagramKey& key = model.Keys[keyIndex];
        for (int32 index = key.FirstAssignment; index < key.FirstAssignment + key.AssignmentCount; ++index)
        {
            if (model.Assignments.IsValidIndex(index) && model.Assignments[index].Binding == binding)
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts two panel boxes match (corners within the tolerance)
    void KeyboardDiagramTestCheckBox(FAutomationTestBase& test, const FString& what, const FBox2f& actual,
        const FBox2f& expected)
    {
        test.TestTrue(what + TEXT(": min"), actual.Min.Equals(expected.Min, KEYBOARDDIAGRAMTEST_TOLERANCE));
        test.TestTrue(what + TEXT(": max"), actual.Max.Equals(expected.Max, KEYBOARDDIAGRAMTEST_TOLERANCE));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts two panels are the same in every box and field (D36: hovering never changes the panel)
    void KeyboardDiagramTestSamePanel(FAutomationTestBase& test, const FString& what, const FSOLDiagramPanel& actual,
        const FSOLDiagramPanel& expected)
    {
        test.TestTrue(what + TEXT(": placement"), actual.Scale.Placement == expected.Scale.Placement);
        test.TestEqual(what + TEXT(": unit"), actual.Scale.UnitPx, expected.Scale.UnitPx);
        test.TestTrue(what + TEXT(": origin"), actual.Scale.OriginPx == expected.Scale.OriginPx);
        test.TestTrue(what + TEXT(": short labels"), actual.Scale.bShortLabels == expected.Scale.bShortLabels);
        test.TestEqual(what + TEXT(": legend columns"), actual.LegendColumns, expected.LegendColumns);
        test.TestEqual(what + TEXT(": legend rows"), actual.LegendRows, expected.LegendRows);
        const FBox2f* actualBoxes[] = { &actual.Legend, &actual.Hint, &actual.Diagram, &actual.Other,
            &actual.Unbound };
        const FBox2f* expectedBoxes[] = { &expected.Legend, &expected.Hint, &expected.Diagram, &expected.Other,
            &expected.Unbound };
        const TCHAR* boxNames[] = { TEXT("legend"), TEXT("hint"), TEXT("diagram"), TEXT("other keys"),
            TEXT("unbound") };
        for (int32 box = 0; box < static_cast<int32>(UE_ARRAY_COUNT(boxNames)); ++box)
        {
            test.TestTrue(FString::Printf(TEXT("%s: %s unchanged"), *what, boxNames[box]),
                actualBoxes[box]->Min == expectedBoxes[box]->Min && actualBoxes[box]->Max == expectedBoxes[box]->Max);
        }
        test.TestEqual(what + TEXT(": height"), actual.Height, expected.Height);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts the widget's panel is SOLKeyboardLayout::LayoutPanel's for a width and the shown strips (D45: no line
    // budget, the hint in place of the hover strip)
    void KeyboardDiagramTestCheckPanel(FAutomationTestBase& test, const FString& what,
        const USOLKeyboardDiagram& widget, const float width, const bool bOtherKeys, const bool bUnbound)
    {
        const FSOLDiagramPanel& actual = widget.GetPanel();
        const FSOLDiagramPanel expected = SOLKeyboardLayout::LayoutPanel(width, bOtherKeys, bUnbound);
        test.TestTrue(what + TEXT(": placement"), actual.Scale.Placement == expected.Scale.Placement);
        test.TestEqual(what + TEXT(": unit"), actual.Scale.UnitPx, expected.Scale.UnitPx,
            KEYBOARDDIAGRAMTEST_TOLERANCE);
        test.TestTrue(what + TEXT(": origin"), actual.Scale.OriginPx.Equals(expected.Scale.OriginPx,
            KEYBOARDDIAGRAMTEST_TOLERANCE));
        test.TestTrue(what + TEXT(": short labels"), actual.Scale.bShortLabels == expected.Scale.bShortLabels);
        test.TestEqual(what + TEXT(": legend columns"), actual.LegendColumns, expected.LegendColumns);
        test.TestEqual(what + TEXT(": legend rows"), actual.LegendRows, expected.LegendRows);
        KeyboardDiagramTestCheckBox(test, what + TEXT(": legend"), actual.Legend, expected.Legend);
        KeyboardDiagramTestCheckBox(test, what + TEXT(": hint"), actual.Hint, expected.Hint);
        KeyboardDiagramTestCheckBox(test, what + TEXT(": diagram"), actual.Diagram, expected.Diagram);
        KeyboardDiagramTestCheckBox(test, what + TEXT(": other keys"), actual.Other, expected.Other);
        KeyboardDiagramTestCheckBox(test, what + TEXT(": unbound"), actual.Unbound, expected.Unbound);
        test.TestEqual(what + TEXT(": height"), actual.Height, expected.Height, KEYBOARDDIAGRAMTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts the widget's detail lines (the hover popup's) are DetailAssignments + DetailLine of its hovered key over
    // its model
    void KeyboardDiagramTestCheckLines(FAutomationTestBase& test, const FString& what,
        const USOLKeyboardDiagram& widget, const TArray<FString>& keys)
    {
        const TArray<FSOLDiagramDetailLine>& lines = widget.GetDetailLines();
        TArray<int32> assignments;
        SOLKeyDiagram::DetailAssignments(widget.GetModel(), widget.GetHoveredKey(), assignments);
        test.TestEqual(what + TEXT(": line count"), lines.Num(), assignments.Num());
        if (lines.Num() != assignments.Num())
        {
            return;
        }
        FSOLDiagramDetailLine expected;
        for (int32 index = 0; index < lines.Num(); ++index)
        {
            SOLKeyDiagram::DetailLine(widget.GetModel(), assignments[index], keys, expected);
            const FString lineWhat = FString::Printf(TEXT("%s: line %d"), *what, index);
            test.TestEqual(lineWhat + TEXT(" keys"), lines[index].Keys, expected.Keys);
            test.TestEqual(lineWhat + TEXT(" action"), lines[index].Action, expected.Action);
            test.TestTrue(lineWhat + TEXT(" area"), lines[index].Area == expected.Area);
            test.TestTrue(lineWhat + TEXT(" fixed"), lines[index].bFixed == expected.bFixed);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a point in diagram key units inside the extent that lies on no key (a gap), or false if there is none
    bool KeyboardDiagramTestFindGap(const ESOLMousePlacement placement, FVector2f& outUnits)
    {
        const FVector2f extent = SOLKeyboardLayout::Extent(placement);
        for (float y = KEYBOARDDIAGRAMTEST_GRID_OFFSET; y < extent.Y; y += KEYBOARDDIAGRAMTEST_GRID_STEP)
        {
            for (float x = KEYBOARDDIAGRAMTEST_GRID_OFFSET; x < extent.X; x += KEYBOARDDIAGRAMTEST_GRID_STEP)
            {
                if (SOLKeyboardLayout::KeyAt(FVector2f(x, y), placement) == INDEX_NONE)
                {
                    outUnits = FVector2f(x, y);
                    return true;
                }
            }
        }
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramSetKeysTest, "SOLTest.KeyboardDiagram.SetKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetKeys rebuilds the model (SOLKeyDiagram::Build over CONTROL_ROWS) only when the keys differ from the last ones set
bool FSOLKeyboardDiagramSetKeysTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel expected;
    SOLKeyDiagram::Build(defaults, expected);

    TestTrue(TEXT("first keys: rebuilt"), widget->SetKeys(defaults));
    TestEqual(TEXT("assignment count as Build's"), widget->GetAssignmentCount(), expected.Assignments.Num());
    TestEqual(TEXT("GetAssignmentCount is the model's"), widget->GetAssignmentCount(),
        widget->GetModel().Assignments.Num());
    TestEqual(TEXT("one model key per layout key"), widget->GetModel().Keys.Num(), expected.Keys.Num());
    TestEqual(TEXT("chords as Build's"), widget->GetModel().Chords.Num(), expected.Chords.Num());
    TestEqual(TEXT("overflow as Build's"), widget->GetModel().Overflow.Num(), expected.Overflow.Num());
    TestEqual(TEXT("unbound as Build's"), widget->GetModel().Unbound.Num(), expected.Unbound.Num());

    // The same keys again (a fresh array with the same names): no rebuild
    TestFalse(TEXT("same keys: no rebuild"), widget->SetKeys(SOLKeyBindings::DefaultKeys()));
    TestEqual(TEXT("same keys: the model kept"), widget->GetAssignmentCount(), expected.Assignments.Num());

    // Different keys rebuild; the defaults again rebuild back
    const TArray<FString> overflow = KeyboardDiagramTestOverflowAndUnbound();
    TestTrue(TEXT("different keys: rebuilt"), widget->SetKeys(overflow));
    SOLKeyDiagram::Build(overflow, expected);
    TestEqual(TEXT("different keys: assignment count as Build's"), widget->GetAssignmentCount(),
        expected.Assignments.Num());
    TestEqual(TEXT("different keys: overflow as Build's"), widget->GetModel().Overflow.Num(), expected.Overflow.Num());
    TestEqual(TEXT("different keys: unbound as Build's"), widget->GetModel().Unbound.Num(), expected.Unbound.Num());
    TestFalse(TEXT("different keys again: no rebuild"), widget->SetKeys(overflow));
    TestTrue(TEXT("the defaults again: rebuilt"), widget->SetKeys(defaults));
    SOLKeyDiagram::Build(defaults, expected);
    TestEqual(TEXT("the defaults again: assignment count as Build's"), widget->GetAssignmentCount(),
        expected.Assignments.Num());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramRebindTest, "SOLTest.KeyboardDiagram.Rebind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A rebind moves the action's area: Fire from the left mouse button to K tints K Combat, and the mouse button no
// longer holds Fire
bool FSOLKeyboardDiagramRebindTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    const int32 k = SOLKeyboardLayout::FindKey(TEXT("K"));
    const int32 leftMouse = SOLKeyboardLayout::FindKey(TEXT("LeftMouseButton"));
    widget->SetKeys(SOLKeyBindings::DefaultKeys());
    const FSOLKeyDiagramModel& model = widget->GetModel();
    TestTrue(TEXT("K free at the defaults"), model.Keys.IsValidIndex(k) && model.Keys[k].AssignmentCount == 0);
    TestTrue(TEXT("the left mouse button holds Fire at the defaults"),
        KeyboardDiagramTestHolds(model, leftMouse, KEYBOARDDIAGRAMTEST_FIRE));

    TestTrue(TEXT("the rebind rebuilt"), widget->SetKeys(KeyboardDiagramTestFireOnK()));
    const FSOLKeyDiagramModel& rebound = widget->GetModel();
    TestEqual(TEXT("Fire on K"), SOLKeyDiagram::KeyOfBinding(rebound, KEYBOARDDIAGRAMTEST_FIRE), k);
    TestTrue(TEXT("K holds Fire"), KeyboardDiagramTestHolds(rebound, k, KEYBOARDDIAGRAMTEST_FIRE));
    TestTrue(TEXT("K tinted Combat"), rebound.Keys.IsValidIndex(k) && rebound.Keys[k].Area == ESOLControlArea::Combat);
    TestFalse(TEXT("the left mouse button no longer holds Fire"),
        KeyboardDiagramTestHolds(rebound, leftMouse, KEYBOARDDIAGRAMTEST_FIRE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramPanelTest, "SOLTest.KeyboardDiagram.Panel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The panel is LayoutPanel's in the layout width with "Other keys" and "Unbound" shown when the model has them; a new
// width or new keys lay it out again
bool FSOLKeyboardDiagramPanelTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    widget->SetKeys(SOLKeyBindings::DefaultKeys());
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_WIDE);
    TestEqual(TEXT("the width reads back"), widget->GetLayoutWidth(), KEYBOARDDIAGRAMTEST_WIDE);
    KeyboardDiagramTestCheckPanel(*this, TEXT("defaults, wide"), *widget, KEYBOARDDIAGRAMTEST_WIDE,
        widget->GetModel().Overflow.Num() > 0, widget->GetModel().Unbound.Num() > 0);
    KeyboardDiagramTestCheckPanel(*this, TEXT("defaults, wide: no strips"), *widget,
        KEYBOARDDIAGRAMTEST_WIDE, false, false);

    // Keys with an off-layout key and an unbound binding: both strips shown
    widget->SetKeys(KeyboardDiagramTestOverflowAndUnbound());
    TestTrue(TEXT("the model has overflow"), widget->GetModel().Overflow.Num() > 0);
    TestTrue(TEXT("the model has unbound"), widget->GetModel().Unbound.Num() > 0);
    KeyboardDiagramTestCheckPanel(*this, TEXT("strips, wide"), *widget, KEYBOARDDIAGRAMTEST_WIDE, true,
        true);

    // A narrower width
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_NARROW);
    TestEqual(TEXT("the narrow width reads back"), widget->GetLayoutWidth(), KEYBOARDDIAGRAMTEST_NARROW);
    KeyboardDiagramTestCheckPanel(*this, TEXT("strips, narrow"), *widget, KEYBOARDDIAGRAMTEST_NARROW, true,
        true);
    TestTrue(TEXT("narrow: stacked"), widget->GetPanel().Scale.Placement == ESOLMousePlacement::Stacked);

    // Back to the defaults: the strips hidden again
    widget->SetKeys(SOLKeyBindings::DefaultKeys());
    KeyboardDiagramTestCheckPanel(*this, TEXT("defaults, narrow"), *widget, KEYBOARDDIAGRAMTEST_NARROW,
        false, false);

    // The width set before any keys is kept when the keys arrive
    USOLKeyboardDiagram* const early = KeyboardDiagramTestNew();
    if (TestNotNull(TEXT("the second widget was created"), early))
    {
        early->SetLayoutWidth(KEYBOARDDIAGRAMTEST_WIDE);
        early->SetKeys(KeyboardDiagramTestOverflowAndUnbound());
        TestEqual(TEXT("width first: the width kept"), early->GetLayoutWidth(), KEYBOARDDIAGRAMTEST_WIDE);
        KeyboardDiagramTestCheckPanel(*this, TEXT("width first"), *early, KEYBOARDDIAGRAMTEST_WIDE, true,
            true);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramHoverAtTest, "SOLTest.KeyboardDiagram.HoverAt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// HoverAt hovers the key under a local point (through the panel's scale): each key at its centre at a one-row width
// and the stacked shell width;
// a point in a gap, in the legend or outside the diagram clears the hover
bool FSOLKeyboardDiagramHoverAtTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    widget->SetKeys(SOLKeyBindings::DefaultKeys());
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    const float widths[] = { KEYBOARDDIAGRAMTEST_WIDE, KEYBOARDDIAGRAMTEST_NARROW };
    for (const float width : widths)
    {
        widget->SetLayoutWidth(width);
        const FSOLDiagramScale scale = widget->GetPanel().Scale;
        const FString what = FString::Printf(TEXT("width %.0f"), width);

        // Each key that is not a mirror at its centre
        for (int32 index = 0; index < layout.Num(); ++index)
        {
            if (layout[index].bMirror)
            {
                continue;
            }
            const FVector2f centre = SOLKeyboardLayout::KeyRect(index, scale.Placement).GetCenter();
            const FVector2f point = SOLKeyboardLayout::FromUnits(centre, scale);
            TestEqual(FString::Printf(TEXT("%s: key %d '%s' hovered at its centre"), *what, index,
                layout[index].KeyName), widget->HoverAt(point), index);
            TestEqual(FString::Printf(TEXT("%s: key %d is the hovered key"), *what, index), widget->GetHoveredKey(),
                index);
        }

        // A gap clears the hover
        FVector2f gap = FVector2f::ZeroVector;
        TestTrue(what + TEXT(": the layout has a gap"), KeyboardDiagramTestFindGap(scale.Placement, gap));
        widget->SetHoveredKey(SOLKeyboardLayout::FindKey(TEXT("O")));
        TestEqual(what + TEXT(": a gap hovers nothing"), widget->HoverAt(SOLKeyboardLayout::FromUnits(gap, scale)),
            static_cast<int32>(INDEX_NONE));
        TestEqual(what + TEXT(": a gap clears the hover"), widget->GetHoveredKey(), static_cast<int32>(INDEX_NONE));
        TestEqual(what + TEXT(": a gap leaves no lines"), widget->GetDetailLines().Num(), 0);

        // The legend (above the diagram) and points outside it
        const FVector2f outside[] = { widget->GetPanel().Legend.GetCenter(),
            SOLKeyboardLayout::FromUnits(FVector2f(-1.0f, -1.0f), scale),
            SOLKeyboardLayout::FromUnits(SOLKeyboardLayout::Extent(scale.Placement) + FVector2f(1.0f, 1.0f), scale) };
        for (const FVector2f& point : outside)
        {
            widget->SetHoveredKey(SOLKeyboardLayout::FindKey(TEXT("O")));
            TestEqual(FString::Printf(TEXT("%s: (%.1f, %.1f) hovers nothing"), *what, point.X, point.Y),
                widget->HoverAt(point), static_cast<int32>(INDEX_NONE));
            TestEqual(FString::Printf(TEXT("%s: (%.1f, %.1f) clears the hover"), *what, point.X, point.Y),
                widget->GetHoveredKey(), static_cast<int32>(INDEX_NONE));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramSetHoveredTest, "SOLTest.KeyboardDiagram.SetHoveredKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetHoveredKey hovers a key in range; INDEX_NONE or an index out of range clears; a mirror key hovers its main key.
// D45 hover anchor (the cap the popup is placed by): the cap given, a mirror's own cap included; cleared with the
// hover; moved to the main cap when the main key is hovered by its own cap; set by HoverAt at a mirror's centre; and
// kept across SetKeys and a new width, which rebuild the model and panel but not the hover
bool FSOLKeyboardDiagramSetHoveredTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    constexpr int32 none = static_cast<int32>(INDEX_NONE);
    TestEqual(TEXT("fresh: nothing hovered"), widget->GetHoveredKey(), none);
    TestEqual(TEXT("fresh: no anchor"), widget->GetHoverAnchor(), none);
    TestEqual(TEXT("fresh: no lines"), widget->GetDetailLines().Num(), 0);
    widget->SetKeys(SOLKeyBindings::DefaultKeys());
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_WIDE);
    TestEqual(TEXT("keys set: nothing hovered"), widget->GetHoveredKey(), none);
    TestEqual(TEXT("keys set: no anchor"), widget->GetHoverAnchor(), none);

    const int32 o = SOLKeyboardLayout::FindKey(TEXT("O"));
    widget->SetHoveredKey(o);
    TestEqual(TEXT("O hovered"), widget->GetHoveredKey(), o);
    TestEqual(TEXT("O is its own anchor"), widget->GetHoverAnchor(), o);
    const int32 clears[] = { none, -5, widget->GetModel().Keys.Num(), MAX_int32 };
    for (const int32 key : clears)
    {
        widget->SetHoveredKey(o);
        widget->SetHoveredKey(key);
        TestEqual(FString::Printf(TEXT("key %d clears the hover"), key), widget->GetHoveredKey(), none);
        TestEqual(FString::Printf(TEXT("key %d clears the anchor"), key), widget->GetHoverAnchor(), none);
        TestEqual(FString::Printf(TEXT("key %d leaves no lines"), key), widget->GetDetailLines().Num(), 0);
    }

    // Every mirror key hovers its main key and anchors on its own cap; the main cap then moves the anchor there
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    int32 mirrors = 0;
    int32 firstMirror = none;
    for (int32 index = 0; index < layout.Num(); ++index)
    {
        if (!layout[index].bMirror)
        {
            continue;
        }
        ++mirrors;
        firstMirror = firstMirror == none ? index : firstMirror;
        const int32 main = SOLKeyboardLayout::FindKey(layout[index].KeyName);
        const FString what = FString::Printf(TEXT("mirror %d '%s'"), index, layout[index].KeyName);
        widget->SetHoveredKey(index);
        TestEqual(what + TEXT(" hovers its main key"), widget->GetHoveredKey(), main);
        TestEqual(what + TEXT(" anchors on its own cap"), widget->GetHoverAnchor(), index);
        widget->SetHoveredKey(main);
        TestEqual(what + TEXT(": the main cap keeps the main key hovered"), widget->GetHoveredKey(), main);
        TestEqual(what + TEXT(": the main cap moves the anchor"), widget->GetHoverAnchor(), main);
        widget->SetHoveredKey(index);
        TestEqual(what + TEXT(": back on the mirror cap"), widget->GetHoverAnchor(), index);
        widget->SetHoveredKey(none);
        TestEqual(what + TEXT(": unhovered, no anchor"), widget->GetHoverAnchor(), none);
    }
    TestTrue(TEXT("the layout has a mirror key"), mirrors > 0);
    if (firstMirror == none)
    {
        return true;
    }

    // HoverAt at the mirror cap's centre anchors on the mirror; a point off every key clears the anchor
    const int32 mirrorMain = SOLKeyboardLayout::FindKey(layout[firstMirror].KeyName);
    const FSOLDiagramScale scale = widget->GetPanel().Scale;
    const FVector2f mirrorCentre = SOLKeyboardLayout::KeyRect(firstMirror, scale.Placement).GetCenter();
    TestEqual(TEXT("HoverAt the mirror: its main key"),
        widget->HoverAt(SOLKeyboardLayout::FromUnits(mirrorCentre, scale)), mirrorMain);
    TestEqual(TEXT("HoverAt the mirror: anchored on the mirror cap"), widget->GetHoverAnchor(), firstMirror);
    widget->HoverAt(SOLKeyboardLayout::FromUnits(FVector2f(-1.0f, -1.0f), scale));
    TestEqual(TEXT("HoverAt off the keys: no anchor"), widget->GetHoverAnchor(), none);

    // SetKeys (a rebuild, then the same keys) and a new width keep the hover and its mirror anchor
    widget->SetHoveredKey(firstMirror);
    TestTrue(TEXT("new keys: rebuilt"), widget->SetKeys(KeyboardDiagramTestOverflowAndUnbound()));
    TestEqual(TEXT("new keys: the main key still hovered"), widget->GetHoveredKey(), mirrorMain);
    TestEqual(TEXT("new keys: the mirror anchor kept"), widget->GetHoverAnchor(), firstMirror);
    TestFalse(TEXT("same keys: no rebuild"), widget->SetKeys(KeyboardDiagramTestOverflowAndUnbound()));
    TestEqual(TEXT("same keys: the mirror anchor kept"), widget->GetHoverAnchor(), firstMirror);
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_NARROW);
    TestEqual(TEXT("new width: the mirror anchor kept"), widget->GetHoverAnchor(), firstMirror);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramDetailLinesTest, "SOLTest.KeyboardDiagram.DetailLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hovered key's detail lines are SOLKeyDiagram::DetailAssignments + DetailLine over the widget's model, for every
// key (a free key none, a modifier its own actions then its chords, D32 revised; Right Shift, Right Ctrl and Left Ctrl
// checked by name); no hover, no lines
bool FSOLKeyboardDiagramDetailLinesTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    const TArray<FString> keySets[] = { SOLKeyBindings::DefaultKeys(), KeyboardDiagramTestFireOnK() };
    const TCHAR* keySetNames[] = { TEXT("defaults"), TEXT("fire on K") };
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    for (int32 set = 0; set < static_cast<int32>(UE_ARRAY_COUNT(keySets)); ++set)
    {
        widget->SetKeys(keySets[set]);
        widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_WIDE);
        for (int32 index = 0; index < layout.Num(); ++index)
        {
            if (layout[index].bMirror)
            {
                continue;
            }
            widget->SetHoveredKey(index);
            KeyboardDiagramTestCheckLines(*this, FString::Printf(TEXT("%s: key %d '%s'"), keySetNames[set], index,
                layout[index].KeyName), *widget, keySets[set]);
        }
    }

    // Spot checks at the defaults: O four lines; Right Shift, Right Ctrl (modifiers with no action of their own) and
    // Left Ctrl (a modifier with ThrustDown) a line per own action then per chord they start (D32 revised); K none
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    widget->SetKeys(defaults);
    widget->SetHoveredKey(SOLKeyboardLayout::FindKey(TEXT("O")));
    TestEqual(TEXT("O: four lines"), widget->GetDetailLines().Num(), 4);
    const TCHAR* modifierKeys[] = { TEXT("RightShift"), TEXT("RightControl"), TEXT("LeftControl") };
    for (const TCHAR* keyName : modifierKeys)
    {
        const int32 key = SOLKeyboardLayout::FindKey(keyName);
        widget->SetHoveredKey(key);
        TestEqual(FString::Printf(TEXT("%s hovered"), keyName), widget->GetHoveredKey(), key);
        const FSOLKeyDiagramModel& model = widget->GetModel();
        const bool bValid = model.Keys.IsValidIndex(key);
        TestTrue(FString::Printf(TEXT("%s is a modifier"), keyName), bValid && model.Keys[key].bModifier);
        TestTrue(FString::Printf(TEXT("%s starts a chord"), keyName), bValid && model.Keys[key].ChordCount > 0);
        TestEqual(FString::Printf(TEXT("%s: a line per own action and per chord it starts"), keyName),
            widget->GetDetailLines().Num(), bValid ? model.Keys[key].AssignmentCount + model.Keys[key].ChordCount
            : -1);
        KeyboardDiagramTestCheckLines(*this, FString::Printf(TEXT("defaults: %s"), keyName), *widget, defaults);
    }
    widget->SetHoveredKey(SOLKeyboardLayout::FindKey(TEXT("K")));
    TestEqual(TEXT("K: no lines"), widget->GetDetailLines().Num(), 0);
    widget->SetHoveredKey(INDEX_NONE);
    TestEqual(TEXT("no hover: no lines"), widget->GetDetailLines().Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramHoverKeptTest, "SOLTest.KeyboardDiagram.HoverKeptAcrossSetKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A hovered key stays hovered across SetKeys, its lines rebuilt from the new model: K (free) shows Fire once Fire moves
// onto it, and loses it when Fire moves back
bool FSOLKeyboardDiagramHoverKeptTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    const TArray<FString> fireOnK = KeyboardDiagramTestFireOnK();
    const int32 k = SOLKeyboardLayout::FindKey(TEXT("K"));
    widget->SetKeys(defaults);
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_WIDE);
    widget->SetHoveredKey(k);
    TestEqual(TEXT("K hovered"), widget->GetHoveredKey(), k);
    TestEqual(TEXT("K free: no lines"), widget->GetDetailLines().Num(), 0);

    // Fire onto K: still hovered, one Combat line
    TestTrue(TEXT("fire on K: rebuilt"), widget->SetKeys(fireOnK));
    TestEqual(TEXT("fire on K: K still hovered"), widget->GetHoveredKey(), k);
    TestEqual(TEXT("fire on K: one line"), widget->GetDetailLines().Num(), 1);
    if (widget->GetDetailLines().Num() == 1)
    {
        TestTrue(TEXT("fire on K: a Combat line"), widget->GetDetailLines()[0].Area == ESOLControlArea::Combat);
    }
    KeyboardDiagramTestCheckLines(*this, TEXT("fire on K"), *widget, fireOnK);

    // The same keys again: no rebuild, the hover and lines kept
    TestFalse(TEXT("same keys: no rebuild"), widget->SetKeys(fireOnK));
    TestEqual(TEXT("same keys: K still hovered"), widget->GetHoveredKey(), k);
    TestEqual(TEXT("same keys: one line"), widget->GetDetailLines().Num(), 1);

    // Fire back: still hovered, no lines
    TestTrue(TEXT("fire back: rebuilt"), widget->SetKeys(defaults));
    TestEqual(TEXT("fire back: K still hovered"), widget->GetHoveredKey(), k);
    TestEqual(TEXT("fire back: no lines"), widget->GetDetailLines().Num(), 0);

    // A key with actions keeps its lines matching the new model
    const int32 leftMouse = SOLKeyboardLayout::FindKey(TEXT("LeftMouseButton"));
    widget->SetHoveredKey(leftMouse);
    const int32 before = widget->GetDetailLines().Num();
    widget->SetKeys(fireOnK);
    TestEqual(TEXT("LMB still hovered"), widget->GetHoveredKey(), leftMouse);
    TestEqual(TEXT("LMB loses the Fire line"), widget->GetDetailLines().Num(), before - 1);
    KeyboardDiagramTestCheckLines(*this, TEXT("LMB after the rebind"), *widget, fireOnK);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramHeadlessTest, "SOLTest.KeyboardDiagram.HeadlessNotPainted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Without a leaf the paint verification reads false, 0 and null, before and after the state is set: no legend
// clipped or abbreviated, no badge hidden, no wide legend, no popup rows or caption, no key material
bool FSOLKeyboardDiagramHeadlessTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    TestFalse(TEXT("fresh: not painted whole"), widget->WasPaintedWhole());
    TestEqual(TEXT("fresh: no keys painted"), widget->GetPaintedKeyCount(), 0);
    TestNull(TEXT("fresh: no key material"), widget->GetKeyMaterial());
    widget->SetKeys(SOLKeyBindings::DefaultKeys());
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_WIDE);
    widget->SetHoveredKey(SOLKeyboardLayout::FindKey(TEXT("O")));
    TestFalse(TEXT("state set: not painted whole"), widget->WasPaintedWhole());
    TestEqual(TEXT("state set: no keys painted"), widget->GetPaintedKeyCount(), 0);
    TestEqual(TEXT("state set: no badges hidden"), widget->GetPaintedHiddenBadges(), 0);
    TestEqual(TEXT("state set: no wide legends"), widget->GetWideLegendCount(), 0);
    TestEqual(TEXT("state set: no popup rows"), widget->GetDetailRowCount(), 0);
    TestEqual(TEXT("state set: no popup caption"), widget->GetDetailCapWidth(), 0.0f);
    TestNull(TEXT("state set: no key material"), widget->GetKeyMaterial());

    // A few keys, the hovered one, a long legend and the numpad Enter mirror among them: nothing drawn, nothing fitted
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    int32 mirrorEnter = INDEX_NONE;
    for (int32 index = 0; index < layout.Num(); ++index)
    {
        mirrorEnter = layout[index].bMirror && FCString::Strcmp(layout[index].KeyName, TEXT("Enter")) == 0
            ? index : mirrorEnter;
    }
    const int32 keys[] = { SOLKeyboardLayout::FindKey(TEXT("O")), SOLKeyboardLayout::FindKey(TEXT("Enter")),
        SOLKeyboardLayout::FindKey(TEXT("Pause")), mirrorEnter };
    for (const int32 key : keys)
    {
        if (!TestTrue(FString::Printf(TEXT("key %d is a layout key"), key), layout.IsValidIndex(key)))
        {
            continue;
        }
        const FString what = FString::Printf(TEXT("headless key %d '%s'"), key, layout[key].KeyName);
        TestFalse(what + TEXT(": legend not clipped"), widget->IsLegendClipped(key));
        TestFalse(what + TEXT(": legend not abbreviated"), widget->IsLegendAbbreviated(key));
        TestFalse(what + TEXT(": badge not hidden"), widget->IsBadgeHidden(key));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramPanelFixedTest, "SOLTest.KeyboardDiagram.PanelFixedWhileHovering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D36 after D45: the hover is a popup on its own layer, so the panel, and the leaf's desired size read from it (the
// widest of the legend, hint and diagram by the Height), stays the same when a key is hovered or the hover cleared.
// Checked on a plain key, a modifier with chords (the most popup lines), a mirror cap and a cleared hover, with the
// strips shown, at the stacked shell width and a wide one
bool FSOLKeyboardDiagramPanelFixedTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }

    // Returns the desired size SSOLKeyboardDiagram::ComputeDesiredSize gives for a panel
    auto desiredSize = [](const FSOLDiagramPanel& panel)
    {
        const float width = FMath::Max3(panel.Legend.Max.X, panel.Diagram.Max.X, panel.Hint.Max.X);
        return FVector2f(FMath::Max(width, 0.0f), FMath::Max(panel.Height, 0.0f));
    };
    widget->SetKeys(KeyboardDiagramTestOverflowAndUnbound());
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    int32 mirror = INDEX_NONE;
    for (int32 index = 0; index < layout.Num() && mirror == INDEX_NONE; ++index)
    {
        mirror = layout[index].bMirror ? index : INDEX_NONE;
    }
    const int32 hovers[] = { SOLKeyboardLayout::FindKey(TEXT("O")), SOLKeyboardLayout::FindKey(TEXT("LeftControl")),
        mirror, static_cast<int32>(INDEX_NONE) };
    const float widths[] = { KEYBOARDDIAGRAMTEST_NARROW, KEYBOARDDIAGRAMTEST_WIDE };
    for (const float width : widths)
    {
        widget->SetLayoutWidth(width);
        widget->SetHoveredKey(INDEX_NONE);
        const FSOLDiagramPanel before = widget->GetPanel();
        const FVector2f sizeBefore = desiredSize(before);
        TestTrue(FString::Printf(TEXT("width %.0f: the panel has a size"), width), sizeBefore.X > 0.0f
            && sizeBefore.Y > 0.0f);
        for (const int32 key : hovers)
        {
            widget->SetHoveredKey(key);
            const FString what = FString::Printf(TEXT("width %.0f: key %d hovered"), width, key);
            TestTrue(what + TEXT(": desired size unchanged"), desiredSize(widget->GetPanel()) == sizeBefore);
            KeyboardDiagramTestSamePanel(*this, what, widget->GetPanel(), before);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramArrangementTest, "SOLTest.KeyboardDiagram.Arrangement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The measured 1280x720 shell width (658, the same at 1920x1080 and 3840x2160) gives a Stacked panel at the width over
// the main block (D34, D37); a wide width keeps the one-row layout with the mouse beside
bool FSOLKeyboardDiagramArrangementTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyboardDiagram* const widget = KeyboardDiagramTestNew();
    if (!TestNotNull(TEXT("the widget was created"), widget))
    {
        return true;
    }
    widget->SetKeys(SOLKeyBindings::DefaultKeys());
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_NARROW);
    TestTrue(TEXT("658: stacked"), widget->GetPanel().Scale.Placement == ESOLMousePlacement::Stacked);
    TestEqual(TEXT("658: the unit is the width over the main block"), widget->GetPanel().Scale.UnitPx,
        KEYBOARDDIAGRAMTEST_NARROW / KEYBOARDDIAGRAMTEST_MAIN_WIDTH, KEYBOARDDIAGRAMTEST_TOLERANCE);
    widget->SetLayoutWidth(KEYBOARDDIAGRAMTEST_WIDE);
    TestTrue(TEXT("1000: one row, mouse beside"), widget->GetPanel().Scale.Placement == ESOLMousePlacement::Beside);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// The hover ramp runs on the menu clock (33g): the leaf reads its clock source's (pinned) clock, the ramp counts from
// the hover change on that clock, a time before the change (a clock that went back) counts as settled, and without a
// source the leaf reads the real application time
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardDiagramClockTest, "SOLTest.KeyboardDiagram.MenuClockRamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Runs the menu clock ramp checks on a bare leaf and a bare subsystem
bool FSOLKeyboardDiagramClockTest::RunTest(const FString& /*parameters*/)
{
    USOLSolidMenuSubsystem* const clock = NewObject<USOLSolidMenuSubsystem>();
    if (!TestNotNull(TEXT("bare subsystem"), clock))
    {
        return false;
    }
    const TSharedRef<SSOLKeyboardDiagram> leaf = SNew(SSOLKeyboardDiagram);

    // Without a source, the real time
    double startS = FPlatformTime::Seconds();
    const double unsourced = leaf->GetNowS();
    double endS = FPlatformTime::Seconds();
    TestTrue(TEXT("Without a clock source the leaf reads the real time"), unsourced >= startS && unsourced <= endS);

    // With a pinned source, exactly the pin
    const double pinS = FPlatformTime::Seconds() + 1000.0;
    clock->PinClock(pinS);
    leaf->SetClockSource(clock);
    TestTrue(TEXT("With a clock source the leaf reads the pinned menu clock"), leaf->GetNowS() == clock->GetClockS());

    // A hover change at the pin ramps from the pin on the menu clock
    leaf->SetHover(0, TArray<FSOLDiagramDetailLine>(), 1.0f);
    const double changeS = clock->GetClockS();
    TestTrue(TEXT("Ramping just after the change"), leaf->IsRamping(changeS + 0.25 * SOL::SolidMenu::HOVER_RAMP_S));
    TestFalse(TEXT("Settled after the ramp time"), leaf->IsRamping(changeS + 2.0 * SOL::SolidMenu::HOVER_RAMP_S));
    TestFalse(TEXT("A time before the change counts as settled"), leaf->IsRamping(changeS - 500.0));

    // Source cleared: back on the real time
    leaf->SetClockSource(nullptr);
    clock->ReleaseClock();
    startS = FPlatformTime::Seconds();
    const double cleared = leaf->GetNowS();
    endS = FPlatformTime::Seconds();
    TestTrue(TEXT("A cleared source falls back to the real time"), cleared >= startS && cleared <= endS);
    return true;
}

#endif
