/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLGlossary.h"
#include "Menu/SOLHudSymbolView.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyboardDiagram.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLKeyDiagram.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLReadOnlyPageWidget.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for SOLMenuContent's Help page refresh (issue #33, Docs/SDDs/33-player-documentation.md sections 4, 5 and 20).
// The page is built for real (BuildHelpPage in a widget tree outered to the transient package: no world, no Slate
// widget taken), so RefreshHelpPage runs over the same rows the menus show; the fonts and crop textures fall back
// without a world, which these checks do not depend on

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float MENUCONTENTTEST_SENTINEL = 1.0f;   // A box width no sample row ever has (far below the symbol box)

    //////////////////////////////////////////////////////////////////////////
    // Returns the first binding whose '{Id}' token a glossary description names, or INDEX_NONE
    int32 MenuContentTestBindingInADescription()
    {
        for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
        {
            const FString token = FString::Printf(TEXT("{%s}"), SOLKeyBindings::GetDef(binding).Id);
            for (int32 entry = 0; entry < SOLGlossary::EntryCount(); ++entry)
            {
                const FString description = SOLMenuText::GLOSSARY_ENTRIES[entry].Description;
                if (description.Contains(token, ESearchCase::CaseSensitive))
                {
                    return binding;
                }
            }
        }
        return INDEX_NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMenuContentRefreshHelpPageTest, "SOLTest.MenuContent.RefreshHelpPage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RefreshHelpPage after a rebind: the diagram's model puts the action on its new key, every glossary description reads
// the new key (written text and last-written record), and a sample row whose recorded sample is stale gets the
// formatter's sample and its box resized to SampleBoxSize (today no sample names a key, so a stale record stands in
// for a changed one). A second refresh with nothing changed writes nothing: the description texts keep their FText
// data and a sample box keeps a sentinel width. Crop rows hold their crop image and no symbol view
bool FSOLMenuContentRefreshHelpPageTest::RunTest(const FString& /*parameters*/)
{
    UWidgetTree* const tree = NewObject<UWidgetTree>(GetTransientPackage());
    USOLReadOnlyPageWidget* const page = SOLMenuContent::BuildHelpPage(*tree);
    if (!TestNotNull(TEXT("the Help page was built"), page) || !TestNotNull(TEXT("the page has its diagram"),
        page->GetDiagram()))
    {
        return false;
    }
    TestEqual(TEXT("one row per glossary entry"), page->GetGlossaryRows().Num(), SOLGlossary::EntryCount());

    // A binding a description names, moved from its default key onto K (free at the defaults, on the diagram)
    const int32 binding = MenuContentTestBindingInADescription();
    if (!TestTrue(TEXT("some description names a binding"), binding != INDEX_NONE))
    {
        return false;
    }
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    TArray<FString> rebound = defaults;
    TestNotEqual(TEXT("the binding is not on K already"), defaults[binding], FString(TEXT("K")));
    rebound[binding] = TEXT("K");

    // Crop rows: the image, no view; every other row: no image
    int32 cropRows = 0;
    for (const FSOLHelpGlossaryRow& row : page->GetGlossaryRows())
    {
        const FString what = FString::Printf(TEXT("entry %d"), row.Entry);
        if (SOLGlossary::EntryArt(row.Entry) == ESOLGlossaryArt::Crop)
        {
            ++cropRows;
            TestNotNull(what + TEXT(": a crop row has its crop image"), row.CropImage.Get());
            TestNull(what + TEXT(": a crop row has no symbol view"), row.View.Get());
        }
        else
        {
            TestNull(what + TEXT(": only a crop row has a crop image"), row.CropImage.Get());
        }
    }
    TestTrue(TEXT("the glossary has crop rows"), cropRows > 0);

    // Make every sample row's record stale (and its box wrong) so the refresh must rewrite the sample path
    int32 sampleRows = 0;
    for (FSOLHelpGlossaryRow& row : page->GetGlossaryRowsForRefresh())
    {
        if (row.View != nullptr && row.View->IsSample() && row.Box != nullptr)
        {
            ++sampleRows;
            row.LastSample = TEXT("STALE SAMPLE");
            row.Box->SetWidthOverride(MENUCONTENTTEST_SENTINEL);
        }
    }
    TestTrue(TEXT("the glossary has sample rows"), sampleRows > 0);

    // The refresh at the rebound keys
    SOLMenuContent::RefreshHelpPage(*page, rebound);
    const FSOLKeyDiagramModel& model = page->GetDiagram()->GetModel();
    TestEqual(TEXT("the diagram puts the binding on K"), SOLKeyDiagram::KeyOfBinding(model, binding),
        SOLKeyboardLayout::FindKey(TEXT("K")));
    FString expected;
    FString atDefaults;
    int32 changedDescriptions = 0;
    for (const FSOLHelpGlossaryRow& row : page->GetGlossaryRows())
    {
        const FString what = FString::Printf(TEXT("entry %d"), row.Entry);
        const SOLMenuText::FGlossaryEntry& source = SOLMenuText::GLOSSARY_ENTRIES[row.Entry];
        SOLGlossary::FormatEntryText(source.Description, rebound, expected);
        SOLGlossary::FormatEntryText(source.Description, defaults, atDefaults);
        changedDescriptions += expected != atDefaults ? 1 : 0;
        if (TestNotNull(what + TEXT(": a description text"), row.Description.Get()))
        {
            TestEqual(what + TEXT(": the description reads the live keys"), row.Description->GetText().ToString(),
                expected);
        }
        TestEqual(what + TEXT(": the last description recorded"), row.LastDescription, expected);
        if (row.View != nullptr && row.View->IsSample() && row.Box != nullptr)
        {
            SOLGlossary::FormatEntryText(source.Sample, rebound, expected);
            TestEqual(what + TEXT(": the sample rewritten from the formatter"), row.LastSample, expected);
            const FVector2f box = USOLHudSymbolView::SampleBoxSize(row.LastSample);
            TestEqual(what + TEXT(": the sample box width"), row.Box->GetWidthOverride(), box.X);
            TestEqual(what + TEXT(": the sample box height"), row.Box->GetHeightOverride(), box.Y);
        }
    }
    TestTrue(TEXT("the rebind changed some description"), changedDescriptions > 0);

    // A second refresh with the same keys: nothing rewritten (same FText data, the sentinel box width kept)
    TArray<FText> textsBefore;
    textsBefore.Reserve(page->GetGlossaryRows().Num());
    for (FSOLHelpGlossaryRow& row : page->GetGlossaryRowsForRefresh())
    {
        textsBefore.Add(row.Description != nullptr ? row.Description->GetText() : FText::GetEmpty());
        if (row.View != nullptr && row.View->IsSample() && row.Box != nullptr)
        {
            row.Box->SetWidthOverride(MENUCONTENTTEST_SENTINEL);
        }
    }
    const int32 assignments = page->GetDiagram()->GetAssignmentCount();
    SOLMenuContent::RefreshHelpPage(*page, rebound);
    TestEqual(TEXT("second refresh: the diagram model kept"), page->GetDiagram()->GetAssignmentCount(), assignments);
    const TArray<FSOLHelpGlossaryRow>& rows = page->GetGlossaryRows();
    for (int32 index = 0; index < rows.Num() && index < textsBefore.Num(); ++index)
    {
        const FString what = FString::Printf(TEXT("second refresh, entry %d"), rows[index].Entry);
        if (rows[index].Description != nullptr)
        {
            TestTrue(what + TEXT(": the description not rewritten"),
                rows[index].Description->GetText().IdenticalTo(textsBefore[index]));
        }
        if (rows[index].View != nullptr && rows[index].View->IsSample() && rows[index].Box != nullptr)
        {
            TestEqual(what + TEXT(": the sample box not resized"), rows[index].Box->GetWidthOverride(),
                MENUCONTENTTEST_SENTINEL);
        }
    }

    // Back to the defaults: the binding leaves K and the descriptions read the defaults again
    SOLMenuContent::RefreshHelpPage(*page, defaults);
    TestNotEqual(TEXT("defaults again: the binding off K"), SOLKeyDiagram::KeyOfBinding(page->GetDiagram()->GetModel(),
        binding), SOLKeyboardLayout::FindKey(TEXT("K")));
    for (const FSOLHelpGlossaryRow& row : page->GetGlossaryRows())
    {
        SOLGlossary::FormatEntryText(SOLMenuText::GLOSSARY_ENTRIES[row.Entry].Description, defaults, expected);
        TestEqual(FString::Printf(TEXT("defaults again, entry %d: the last description"), row.Entry),
            row.LastDescription, expected);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
