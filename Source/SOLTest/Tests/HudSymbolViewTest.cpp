/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLHudSymbolView.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

// Test-after cases for the issue #33 Part 33d-ii glossary symbol view (Menu/SOLHudSymbolView.h): its state without a
// Slate widget (headless). Building and painting the batch need a laid-out layer and are covered by the
// -SOLSmokeSolidMenu Help checks.

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolViewStateTest, "SOLTest.HudSymbolView.State",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new view clips to its bounds, takes no hit test and shows nothing; SetSymbols keeps up to
// GLOSSARY_MAX_SYMBOLS_PER_ENTRY symbols (none for a null list), SetSample switches it to a sample; with no Slate layer
// UpdateBatch never builds, every count reads 0 and no batch memory is held (the layer sizes it at the first build)
bool FSOLHudSymbolViewStateTest::RunTest(const FString& /*parameters*/)
{
    USOLHudSymbolView* const view = NewObject<USOLHudSymbolView>(GetTransientPackage());
    TestEqual(TEXT("clips to its bounds"), static_cast<int32>(view->GetClipping()),
        static_cast<int32>(EWidgetClipping::ClipToBounds));
    TestEqual(TEXT("hit-test invisible"), static_cast<int32>(view->GetVisibility()),
        static_cast<int32>(ESlateVisibility::HitTestInvisible));
    TestEqual(TEXT("no symbol at first"), view->GetSymbolCount(), 0);
    TestFalse(TEXT("no sample at first"), view->IsSample());

    // Symbols: three kept, a list past the limit clamped, a null list none
    const ESOLHudSymbol three[] = { ESOLHudSymbol::SteerFree, ESOLHudSymbol::Reticle, ESOLHudSymbol::Joystick };
    view->SetSymbols(three, static_cast<int32>(UE_ARRAY_COUNT(three)));
    TestEqual(TEXT("three symbols"), view->GetSymbolCount(), 3);
    TestFalse(TEXT("symbols: not a sample"), view->IsSample());
    ESOLHudSymbol many[SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY + 2];
    for (ESOLHudSymbol& symbol : many)
    {
        symbol = ESOLHudSymbol::Reticle;
    }
    view->SetSymbols(many, static_cast<int32>(UE_ARRAY_COUNT(many)));
    TestEqual(TEXT("clamped to the per-entry limit"), view->GetSymbolCount(),
        SOL::Help::GLOSSARY_MAX_SYMBOLS_PER_ENTRY);
    view->SetSymbols(nullptr, 3);
    TestEqual(TEXT("a null list: none"), view->GetSymbolCount(), 0);
    view->SetSymbols(three, -1);
    TestEqual(TEXT("a negative count: none"), view->GetSymbolCount(), 0);

    // A sample replaces the symbols, and symbols the sample
    view->SetSymbols(three, static_cast<int32>(UE_ARRAY_COUNT(three)));
    view->SetSample(TEXT("MANUAL|ASSISTED|AUTO"));
    TestTrue(TEXT("sample shown"), view->IsSample());
    TestEqual(TEXT("sample: no symbols"), view->GetSymbolCount(), 0);
    view->SetSymbols(three, 1);
    TestFalse(TEXT("symbols again: not a sample"), view->IsSample());
    TestEqual(TEXT("one symbol"), view->GetSymbolCount(), 1);

    // No Slate layer: nothing is built or painted
    FSOLHudSymbolScratch scratch;
    TestFalse(TEXT("no layer: no build"), view->UpdateBatch(1.0f, scratch));
    TestEqual(TEXT("build count 0"), view->GetBuildCount(), 0);
    TestEqual(TEXT("no shapes"), view->GetShapeCount(), 0);
    TestEqual(TEXT("no painted shapes"), view->GetPaintedShapeCount(), 0);
    TestEqual(TEXT("no texts"), view->GetTextCount(), 0);
    TestEqual(TEXT("no painted texts"), view->GetPaintedTextCount(), 0);
    TestEqual(TEXT("no painted backings"), view->GetPaintedBackingCount(), 0);
    TestEqual(TEXT("nothing dropped"), view->GetDroppedShapeCount(), 0);
    TestNull(TEXT("no material"), view->GetMaterial());
    TestTrue(TEXT("no built size"), view->GetBuiltSizePx() == FIntPoint::ZeroValue);
    TestEqual(TEXT("no fit scale"), view->GetFitScale(), 0.0f);
    TestEqual(TEXT("no batch capacity"), view->GetShapeCapacity(), 0);
    TestTrue(TEXT("no paint origin"), view->GetLastPaintOrigin() == FVector2f::ZeroVector);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
