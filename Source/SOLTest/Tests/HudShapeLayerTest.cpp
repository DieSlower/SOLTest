/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "UI/SOLHudShapeLayer.h"

#include "Misc/AutomationTest.h"

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
// A new layer is per-frame; SetRetainBatch switches the mode both ways; ResetFrame empties the batch and the backings
// in either mode and keeps the mode; MarkBatchChanged keeps the batch and the mode
bool FSOLHudShapeLayerRetainTest::RunTest(const FString& /*parameters*/)
{
    const TSharedRef<SSOLHudShapeLayer> layer = SNew(SSOLHudShapeLayer).MaxShapes(HUDSHAPELAYERTEST_MAX_SHAPES);
    TestEqual(TEXT("the asked capacity"), layer->GetBatch().GetMaxShapes(), HUDSHAPELAYERTEST_MAX_SHAPES);
    TestFalse(TEXT("per-frame by default"), layer->IsRetainBatch());

    const FBox2f backing(FVector2f(10.0f, 10.0f), FVector2f(60.0f, 40.0f));
    for (const bool bRetain : { true, false })
    {
        const FString mode = bRetain ? TEXT("retained") : TEXT("per-frame");
        layer->SetRetainBatch(bRetain);
        TestEqual(mode + TEXT(": the mode set"), layer->IsRetainBatch(), bRetain);

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
    }
    TestFalse(TEXT("back to per-frame"), layer->IsRetainBatch());
    return true;
}

#endif
