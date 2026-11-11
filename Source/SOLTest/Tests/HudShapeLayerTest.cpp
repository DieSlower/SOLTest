/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "UI/SOLHudShapeLayer.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33d-i retain mode of the HUD shape layer (Docs/SDDs/33-player-documentation.md
// sections 4.2 and 8.3), written against the header contract (UI/SOLHudShapeLayer.h) before the production code
// existed: the paint gate ShouldPaintBatch and the mode's state on a layer. Painting itself needs a Slate renderer and
// is covered by the 33d-ii smoke; these run headless (nullrhi).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr int32 HUDSHAPELAYERTEST_MAX_SHAPES = 8;          // The test layer's capacity

    //////////////////////////////////////////////////////////////////////////
    // Asserts ShouldPaintBatch for one mode, batch stamp and frame
    void HudShapeLayerTestCheckPaint(FAutomationTestBase& test, const bool bRetain, const uint64 batchFrame,
        const uint64 frameCounter, const bool bExpected)
    {
        test.TestEqual(FString::Printf(TEXT("retain %d, batch %llu, frame %llu"), bRetain ? 1 : 0, batchFrame,
            frameCounter), SSOLHudShapeLayer::ShouldPaintBatch(bRetain, batchFrame, frameCounter), bExpected);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapeLayerPaintGateTest, "SOLTest.HudShapeLayer.ShouldPaintBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Per-frame mode paints a batch stamped this frame or the last (frameCounter <= batchFrame + 1) and nothing older;
// retain mode paints in every case
bool FSOLHudShapeLayerPaintGateTest::RunTest(const FString& /*parameters*/)
{
    const uint64 stamp = 1000;
    const uint64 large = 1000000;

    // Per-frame (the HUD)
    HudShapeLayerTestCheckPaint(*this, false, stamp, stamp, true);
    HudShapeLayerTestCheckPaint(*this, false, stamp, stamp + 1, true);
    HudShapeLayerTestCheckPaint(*this, false, stamp, stamp + 2, false);
    HudShapeLayerTestCheckPaint(*this, false, stamp, large, false);
    HudShapeLayerTestCheckPaint(*this, false, 0, large, false);
    HudShapeLayerTestCheckPaint(*this, false, 0, 0, true);
    HudShapeLayerTestCheckPaint(*this, false, 0, 1, true);
    HudShapeLayerTestCheckPaint(*this, false, 0, 2, false);
    HudShapeLayerTestCheckPaint(*this, false, stamp, stamp - 1, true);

    // Retained (a glossary picture): always
    const uint64 frames[] = { 0, 1, 2, stamp - 1, stamp, stamp + 1, stamp + 2, large };
    for (const uint64 frame : frames)
    {
        HudShapeLayerTestCheckPaint(*this, true, stamp, frame, true);
        HudShapeLayerTestCheckPaint(*this, true, 0, frame, true);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapeLayerRetainTest, "SOLTest.HudShapeLayer.RetainMode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new layer is per-frame (and volatile); SetRetainBatch switches the mode both ways, the widget volatile exactly when
// not retained (IsVolatile() == !retain); ResetFrame empties the batch and the backings in either mode and keeps the
// mode; MarkBatchChanged keeps the batch and the mode
bool FSOLHudShapeLayerRetainTest::RunTest(const FString& /*parameters*/)
{
    const TSharedRef<SSOLHudShapeLayer> layer = SNew(SSOLHudShapeLayer).MaxShapes(HUDSHAPELAYERTEST_MAX_SHAPES);
    TestEqual(TEXT("the asked capacity"), layer->GetBatch().GetMaxShapes(), HUDSHAPELAYERTEST_MAX_SHAPES);
    TestFalse(TEXT("per-frame by default"), layer->IsRetainBatch());
    TestTrue(TEXT("volatile by default"), layer->IsVolatile());

    const FBox2f backing(FVector2f(10.0f, 10.0f), FVector2f(60.0f, 40.0f));
    for (const bool bRetain : { true, false })
    {
        const FString mode = bRetain ? TEXT("retained") : TEXT("per-frame");
        layer->SetRetainBatch(bRetain);
        TestEqual(mode + TEXT(": the mode set"), layer->IsRetainBatch(), bRetain);
        TestEqual(mode + TEXT(": volatile exactly when not retained"), layer->IsVolatile(), !bRetain);

        // Fill, mark changed (retain mode's refill signal), then reset
        layer->GetBatch().AddLine(FVector2f(1.0f, 2.0f), FVector2f(30.0f, 4.0f),
            SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, SOL::HudPalette::PRIMARY, 1.0f, 2.0f);
        TestTrue(mode + TEXT(": a backing queued"), layer->AddBacking(backing, 4.0f, 2.0f));
        TestEqual(mode + TEXT(": one shape"), layer->GetBatch().GetShapeCount(), 1);
        layer->MarkBatchChanged();
        TestEqual(mode + TEXT(": marking keeps the shape"), layer->GetBatch().GetShapeCount(), 1);
        TestEqual(mode + TEXT(": marking keeps the mode"), layer->IsRetainBatch(), bRetain);
        layer->ResetFrame();
        TestEqual(mode + TEXT(": reset empties the batch"), layer->GetBatch().GetShapeCount(), 0);
        TestEqual(mode + TEXT(": reset empties the backings"), layer->GetBackingCount(), 0);
        TestEqual(mode + TEXT(": reset keeps the mode"), layer->IsRetainBatch(), bRetain);
        TestEqual(mode + TEXT(": reset keeps the volatility"), layer->IsVolatile(), !bRetain);
    }
    TestFalse(TEXT("back to per-frame"), layer->IsRetainBatch());
    TestTrue(TEXT("back to volatile"), layer->IsVolatile());

    // Setting the same mode twice keeps the volatility
    layer->SetRetainBatch(true);
    layer->SetRetainBatch(true);
    TestFalse(TEXT("retained twice: not volatile"), layer->IsVolatile());
    layer->SetRetainBatch(false);
    TestTrue(TEXT("per-frame again: volatile"), layer->IsVolatile());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapeLayerCapacityTest, "SOLTest.HudShapeLayer.Capacities",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (33d-ii, test-after) The text and backing capacities default to the HUD's; a glossary view's smaller ones (MaxTexts,
// MaxBackings) bound what the layer queues, the rest dropped; a negative capacity reads 0
bool FSOLHudShapeLayerCapacityTest::RunTest(const FString& /*parameters*/)
{
    const TSharedRef<SSOLHudShapeLayer> hud = SNew(SSOLHudShapeLayer);
    TestEqual(TEXT("default texts: the HUD's"), hud->GetTexts().GetMaxItems(), SOL::Hologram::HUD_MAX_TEXTS);
    TestEqual(TEXT("default backings: the HUD's"), hud->GetMaxBackings(), SOL::Hologram::HUD_MAX_PANELS);
    TestEqual(TEXT("default shapes: the HUD's"), hud->GetBatch().GetMaxShapes(), SOL::Hologram::HUD_MAX_SHAPES);

    // A glossary-sized layer: two lines and one backing
    const TSharedRef<SSOLHudShapeLayer> view = SNew(SSOLHudShapeLayer).MaxShapes(HUDSHAPELAYERTEST_MAX_SHAPES)
        .MaxTexts(2).MaxBackings(1);
    TestEqual(TEXT("asked texts"), view->GetTexts().GetMaxItems(), 2);
    TestEqual(TEXT("asked backings"), view->GetMaxBackings(), 1);
    const FBox2f backing(FVector2f(0.0f, 0.0f), FVector2f(88.0f, 88.0f));
    TestTrue(TEXT("the one backing fits"), view->AddBacking(backing, 6.0f, 8.0f));
    TestFalse(TEXT("a second backing is refused"), view->AddBacking(backing, 6.0f, 8.0f));
    TestEqual(TEXT("one backing queued"), view->GetBackingCount(), 1);
    const FText line = FText::FromString(TEXT("AUTO"));
    for (int32 index = 0; index < 3; ++index)
    {
        view->GetTexts().Add(line, FVector2f(0.0f, 10.0f * index), 1.0f, SOL::HudPalette::PRIMARY,
            ESOLHudFont::Medium);
    }
    TestEqual(TEXT("two lines kept"), view->GetTexts().GetItems().Num(), 2);
    TestEqual(TEXT("the third dropped"), view->GetTexts().GetDroppedCount(), 1);
    view->ResetFrame();
    TestTrue(TEXT("after a reset the backing fits again"), view->AddBacking(backing, 6.0f, 8.0f));

    // Negative capacities read 0: nothing is queued
    const TSharedRef<SSOLHudShapeLayer> none = SNew(SSOLHudShapeLayer).MaxTexts(-4).MaxBackings(-1);
    TestEqual(TEXT("negative texts read 0"), none->GetTexts().GetMaxItems(), 0);
    TestEqual(TEXT("negative backings read 0"), none->GetMaxBackings(), 0);
    TestFalse(TEXT("no backing fits"), none->AddBacking(backing, 6.0f, 8.0f));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapeLayerPaintScratchTest, "SOLTest.HudShapeLayer.SharedPaintScratch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (33d-ii, test-after) A layer reserves its own paint buffer for MaxShapes quads (the HUD's way); with a shared
// PaintScratch it reserves none and leaves the shared buffer as the owner sized it; a MaxShapes(0) layer holds no
// batch until its owner sizes it; nothing is painted yet, so the last paint origin reads (0, 0)
bool FSOLHudShapeLayerPaintScratchTest::RunTest(const FString& /*parameters*/)
{
    const TSharedRef<SSOLHudShapeLayer> own = SNew(SSOLHudShapeLayer).MaxShapes(HUDSHAPELAYERTEST_MAX_SHAPES);
    TestTrue(TEXT("own paint buffer reserved"), own->GetOwnPaintVertexCapacity()
        >= HUDSHAPELAYERTEST_MAX_SHAPES * SOL::Hologram::HUD_VERTICES_PER_SHAPE);
    TestTrue(TEXT("no paint yet: origin (0, 0)"), own->GetLastPaintOrigin() == FVector2f::ZeroVector);

    TArray<FSlateVertex> shared;
    shared.Reserve(HUDSHAPELAYERTEST_MAX_SHAPES * SOL::Hologram::HUD_VERTICES_PER_SHAPE);
    const int32 sharedCapacity = shared.Max();
    const TSharedRef<SSOLHudShapeLayer> lean = SNew(SSOLHudShapeLayer).MaxShapes(0).PaintScratch(&shared);
    TestEqual(TEXT("shared scratch: no own paint buffer"), lean->GetOwnPaintVertexCapacity(), 0);
    TestEqual(TEXT("shared scratch untouched"), shared.Max(), sharedCapacity);
    TestEqual(TEXT("no batch until sized"), lean->GetBatch().GetMaxShapes(), 0);
    lean->GetBatch().Initialize(3);
    TestEqual(TEXT("sized by its owner"), lean->GetBatch().GetMaxShapes(), 3);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudShapeLayerTextGlowAlphaTest, "SOLTest.HudShapeLayer.TextGlowAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (Issue #37 SDD case 19, the 37a review) A new layer's glow-text outline alpha is HUD_TEXT_GLOW_ALPHA (so a layer that
// never sets it, such as the menu layer, keeps the issue #31 look); SetTextGlowAlpha clamps to [0, 1], NaN reads as the
// default, and setting it keeps the retain mode and the queued shapes in either mode
bool FSOLHudShapeLayerTextGlowAlphaTest::RunTest(const FString& /*parameters*/)
{
    constexpr float tolerance = 1.0e-6f;
    const TSharedRef<SSOLHudShapeLayer> layer = SNew(SSOLHudShapeLayer).MaxShapes(HUDSHAPELAYERTEST_MAX_SHAPES);
    TestEqual(TEXT("new: the default"), layer->GetTextGlowAlpha(), SOL::Hologram::HUD_TEXT_GLOW_ALPHA, tolerance);

    // Values in range are kept, out of range clamped, NaN the default
    const struct { float In; float Want; const TCHAR* Name; } cases[] = {
        { 0.09f, 0.09f, TEXT("0.09 kept") },
        { 0.0f, 0.0f, TEXT("0 kept (no outline)") },
        { 1.0f, 1.0f, TEXT("1 kept") },
        { -1.0f, 0.0f, TEXT("-1 clamps to 0") },
        { 3.0f, 1.0f, TEXT("3 clamps to 1") },
        { std::numeric_limits<float>::quiet_NaN(), SOL::Hologram::HUD_TEXT_GLOW_ALPHA, TEXT("NaN reads the default") },
    };
    for (const auto& item : cases)
    {
        layer->SetTextGlowAlpha(item.In);
        TestEqual(item.Name, layer->GetTextGlowAlpha(), item.Want, tolerance);
    }

    // Setting it keeps the mode and the queued shape in either mode
    for (const bool bRetain : { true, false })
    {
        const FString mode = bRetain ? TEXT("retained") : TEXT("per-frame");
        layer->SetRetainBatch(bRetain);
        layer->ResetFrame();
        layer->GetBatch().AddLine(FVector2f(1.0f, 2.0f), FVector2f(30.0f, 4.0f),
            SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, SOL::HudPalette::PRIMARY, 1.0f, 2.0f);
        layer->SetTextGlowAlpha(0.05f);
        layer->SetTextGlowAlpha(0.3f);
        TestEqual(mode + TEXT(": the mode kept"), layer->IsRetainBatch(), bRetain);
        TestEqual(mode + TEXT(": the shape kept"), layer->GetBatch().GetShapeCount(), 1);
        TestEqual(mode + TEXT(": the last alpha"), layer->GetTextGlowAlpha(), 0.3f, tolerance);
    }
    return true;
}

#endif
