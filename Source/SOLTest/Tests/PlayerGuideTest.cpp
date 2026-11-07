/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLGlossary.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLPlayerGuide.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a generated player guide (Docs/SDDs/33-player-documentation.md sections 9 and 10,
// D8, D24, D27), written against the header contract (Menu/SOLPlayerGuide.h) before the production code existed: the
// markdown is deterministic and built only from the Controls rows, the glossary table and the key array, and the
// staleness comparison ignores line endings. SOLTest.PlayerGuide.UpToDate (the committed file against the generated
// text) is added in part 33e, when the guide file is first generated.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr int32 PLAYERGUIDETEST_MIN_IMAGES = 5;             // One image per Help tab (D24)
    const TCHAR* const PLAYERGUIDETEST_BANNER = TEXT("<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->");
    const TCHAR* const PLAYERGUIDETEST_FLAG = TEXT("-SOLWritePlayerGuide");
    constexpr int32 PLAYERGUIDETEST_FIRE = static_cast<int32>(SOLKeyBindings::EBinding::Fire);

    // The ten Controls areas, one table each in the guide
    const TCHAR* const PLAYERGUIDETEST_AREAS[] = { TEXT("Flight"), TEXT("Targeting"), TEXT("Combat"), TEXT("Time"),
        TEXT("Overlays"), TEXT("HUD"), TEXT("Map"), TEXT("Routes"), TEXT("Menu"), TEXT("Demo") };

    //////////////////////////////////////////////////////////////////////////
    // Returns a shown key text as <kbd> tags: each '+' part in its own tag, joined by '+'
    FString PlayerGuideTestKbd(const FString& shownKey)
    {
        TArray<FString> parts;
        shownKey.ParseIntoArray(parts, TEXT("+"));
        FString out;
        for (int32 index = 0; index < parts.Num(); ++index)
        {
            out += (index > 0 ? TEXT("+") : TEXT("")) + FString::Printf(TEXT("<kbd>%s</kbd>"), *parts[index]);
        }
        return out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of times a needle occurs in a text
    int32 PlayerGuideTestCount(const FString& text, const TCHAR* needle)
    {
        int32 count = 0;
        int32 at = text.Find(needle, ESearchCase::CaseSensitive);
        while (at != INDEX_NONE)
        {
            ++count;
            // FString::Find clamps a start at or past the end to the last character, which would find this match again
            at = at + 1 < text.Len()
                ? text.Find(needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, at + 1) : INDEX_NONE;
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the first '{<Token>}' (letters and digits) left in a text, or an empty string
    FString PlayerGuideTestFirstToken(const FString& text)
    {
        int32 open = text.Find(TEXT("{"));
        while (open != INDEX_NONE)
        {
            const int32 close = text.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, open + 1);
            if (close == INDEX_NONE)
            {
                break;
            }
            const FString name = text.Mid(open + 1, close - open - 1);
            bool bWord = !name.IsEmpty();
            for (const TCHAR character : name)
            {
                bWord &= FChar::IsAlnum(character);
            }
            if (bWord)
            {
                return text.Mid(open, close - open + 1);
            }
            // FString::Find clamps a start at or past the end to the last character, so stop there
            open = open + 1 < text.Len()
                ? text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, open + 1) : INDEX_NONE;
        }
        return FString();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the line of a text that contains a needle, or an empty string
    FString PlayerGuideTestLineWith(const FString& text, const TCHAR* needle)
    {
        TArray<FString> lines;
        text.ParseIntoArray(lines, TEXT("\n"), false);
        for (const FString& line : lines)
        {
            if (line.Contains(needle, ESearchCase::CaseSensitive))
            {
                return line;
            }
        }
        return FString();
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideDeterministicTest, "SOLTest.PlayerGuide.Deterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same keys give the same text, with "\n" line endings only
bool FSOLPlayerGuideDeterministicTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString first = SOLPlayerGuide::BuildMarkdown(keys);
    const FString second = SOLPlayerGuide::BuildMarkdown(keys);
    TestFalse(TEXT("the guide has text"), first.IsEmpty());
    TestTrue(TEXT("the same text twice"), first.Equals(second, ESearchCase::CaseSensitive));
    TestFalse(TEXT("no carriage returns"), first.Contains(TEXT("\r")));
    TestTrue(TEXT("several lines"), PlayerGuideTestCount(first, TEXT("\n")) > 10);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideHeaderTest, "SOLTest.PlayerGuide.Header",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The guide starts with the docs banner and carries the "generated by -SOLWritePlayerGuide, do not edit" line with the
// input hash
bool FSOLPlayerGuideHeaderTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString guide = SOLPlayerGuide::BuildMarkdown(keys);
    TestTrue(TEXT("starts with the docs banner"), guide.StartsWith(PLAYERGUIDETEST_BANNER, ESearchCase::CaseSensitive));
    const FString generated = PlayerGuideTestLineWith(guide, PLAYERGUIDETEST_FLAG);
    TestFalse(TEXT("a generated-file line names the flag"), generated.IsEmpty());
    TestTrue(TEXT("the generated-file line says do not edit"), generated.Contains(TEXT("do not edit"),
        ESearchCase::IgnoreCase));
    const FString hash = SOLPlayerGuide::InputHash(keys);
    TestFalse(TEXT("the input hash has text"), hash.IsEmpty());
    TestTrue(TEXT("the generated-file line holds the input hash"), !hash.IsEmpty()
        && generated.Contains(hash, ESearchCase::IgnoreCase));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideControlsTest, "SOLTest.PlayerGuide.ControlsContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every Controls action is in the guide, every binding's default key and every fixed key as <kbd> tags (chords as
// <kbd>Shift</kbd>+<kbd>M</kbd>)
bool FSOLPlayerGuideControlsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TArray<FString> keys = DefaultKeys();
    const FString guide = SOLPlayerGuide::BuildMarkdown(keys);
    for (int32 row = 0; row < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)); ++row)
    {
        const FString action = SOLMenuContent::FormatControlAction(SOLMenuText::CONTROL_ROWS[row].Action);
        TestTrue(FString::Printf(TEXT("row %d action in the guide"), row),
            guide.Contains(action, ESearchCase::CaseSensitive));
    }
    for (const TCHAR* area : PLAYERGUIDETEST_AREAS)
    {
        TestTrue(FString::Printf(TEXT("area %s in the guide"), area), guide.Contains(area, ESearchCase::CaseSensitive));
    }

    // Every binding's default key
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        const FString kbd = PlayerGuideTestKbd(BindingDisplayName(binding, keys));
        TestTrue(FString::Printf(TEXT("%s as %s"), GetDef(binding).Id, *kbd),
            guide.Contains(kbd, ESearchCase::CaseSensitive));
    }
    TestTrue(TEXT("the map chord"), guide.Contains(TEXT("<kbd>Shift</kbd>+<kbd>M</kbd>"), ESearchCase::CaseSensitive));

    // Every fixed key and chord of the rows
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        TArray<FString> alternatives;
        FString(row.Bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
        for (const FString& alternative : alternatives)
        {
            if (alternative.StartsWith(TEXT("@")))
            {
                continue;
            }
            TArray<FString> chordKeys;
            alternative.ParseIntoArray(chordKeys, SOLMenuText::BINDING_CHORD_DELIMITER);
            FString kbd;
            for (int32 index = 0; index < chordKeys.Num(); ++index)
            {
                kbd += (index > 0 ? TEXT("+") : TEXT(""))
                    + FString::Printf(TEXT("<kbd>%s</kbd>"), *SOLBindingLabel::KeyDisplayName(chordKeys[index]));
            }
            TestTrue(FString::Printf(TEXT("fixed '%s' as %s"), *alternative, *kbd),
                guide.Contains(kbd, ESearchCase::CaseSensitive));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideGlossaryTest, "SOLTest.PlayerGuide.GlossaryContent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every glossary entry's name and formatted description is in the guide, with one image per Help tab
bool FSOLPlayerGuideGlossaryTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString guide = SOLPlayerGuide::BuildMarkdown(keys);
    FString description;
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_ENTRIES)); ++index)
    {
        const SOLMenuText::FGlossaryEntry& entry = SOLMenuText::GLOSSARY_ENTRIES[index];
        TestTrue(FString::Printf(TEXT("entry %d name '%s' in the guide"), index, entry.Name),
            guide.Contains(entry.Name, ESearchCase::CaseSensitive));
        SOLGlossary::FormatEntryText(entry.Description, keys, description);
        if (!description.Contains(TEXT("|")))
        {
            TestTrue(FString::Printf(TEXT("entry %d '%s' description in the guide"), index, entry.Name),
                guide.Contains(description, ESearchCase::CaseSensitive));
        }
    }
    for (const TCHAR* heading : SOLMenuText::GLOSSARY_TABS)
    {
        TestTrue(FString::Printf(TEXT("tab '%s' in the guide"), heading),
            guide.Contains(heading, ESearchCase::CaseSensitive));
    }
    const int32 images = PlayerGuideTestCount(guide, TEXT("!["));
    TestTrue(FString::Printf(TEXT("one image per Help tab (%d)"), images), images >= PLAYERGUIDETEST_MIN_IMAGES);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideTokensTest, "SOLTest.PlayerGuide.NoTokensLeft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No '{<Token>}' is left in the guide, at the default keys or rebound ones
bool FSOLPlayerGuideTokensTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString token = PlayerGuideTestFirstToken(SOLPlayerGuide::BuildMarkdown(keys));
    TestTrue(FString::Printf(TEXT("defaults: no token left (found '%s')"), *token), token.IsEmpty());
    keys[PLAYERGUIDETEST_FIRE] = TEXT("K");
    const FString rebound = PlayerGuideTestFirstToken(SOLPlayerGuide::BuildMarkdown(keys));
    TestTrue(FString::Printf(TEXT("rebound: no token left (found '%s')"), *rebound), rebound.IsEmpty());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideKeysTest, "SOLTest.PlayerGuide.KeysChangeOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The guide and its input hash follow the key array
bool FSOLPlayerGuideKeysTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    TArray<FString> rebound = defaults;
    rebound[PLAYERGUIDETEST_FIRE] = TEXT("K");
    const FString defaultGuide = SOLPlayerGuide::BuildMarkdown(defaults);
    const FString reboundGuide = SOLPlayerGuide::BuildMarkdown(rebound);
    TestFalse(TEXT("a rebind changes the guide"), defaultGuide.Equals(reboundGuide, ESearchCase::CaseSensitive));
    TestTrue(TEXT("the rebound key is shown"), reboundGuide.Contains(TEXT("<kbd>K</kbd>"), ESearchCase::CaseSensitive));
    TestFalse(TEXT("a rebind changes the hash"), SOLPlayerGuide::InputHash(defaults).Equals(
        SOLPlayerGuide::InputHash(rebound)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideHashTest, "SOLTest.PlayerGuide.InputHash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The input hash is deterministic hex text
bool FSOLPlayerGuideHashTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString hash = SOLPlayerGuide::InputHash(keys);
    TestFalse(TEXT("has text"), hash.IsEmpty());
    TestEqual(TEXT("deterministic"), SOLPlayerGuide::InputHash(keys), hash);
    bool bHex = true;
    for (const TCHAR character : hash)
    {
        bHex &= FChar::IsHexDigit(character);
    }
    TestTrue(FString::Printf(TEXT("'%s' is hex"), *hash), bHex);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideCurrentTest, "SOLTest.PlayerGuide.IsGuideCurrent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The staleness check ignores CRLF / LF differences and trailing newlines, and catches any other change
bool FSOLPlayerGuideCurrentTest::RunTest(const FString& /*parameters*/)
{
    const FString generated = TEXT("# Guide\nline one\nline two\n");
    TestTrue(TEXT("identical"), SOLPlayerGuide::IsGuideCurrent(generated, generated));
    TestTrue(TEXT("CRLF endings"), SOLPlayerGuide::IsGuideCurrent(TEXT("# Guide\r\nline one\r\nline two\r\n"),
        generated));
    TestTrue(TEXT("no trailing newline"), SOLPlayerGuide::IsGuideCurrent(TEXT("# Guide\nline one\nline two"),
        generated));
    TestTrue(TEXT("extra trailing newlines"), SOLPlayerGuide::IsGuideCurrent(TEXT("# Guide\nline one\nline two\n\n\n"),
        generated));
    TestTrue(TEXT("CRLF and no trailing newline"), SOLPlayerGuide::IsGuideCurrent(
        TEXT("# Guide\r\nline one\r\nline two"), generated));
    TestFalse(TEXT("a changed line"), SOLPlayerGuide::IsGuideCurrent(TEXT("# Guide\nline one\nline 2\n"), generated));
    TestFalse(TEXT("a missing line"), SOLPlayerGuide::IsGuideCurrent(TEXT("# Guide\nline one\n"), generated));
    TestFalse(TEXT("an extra line"), SOLPlayerGuide::IsGuideCurrent(TEXT("# Guide\nline one\nline two\nthree\n"),
        generated));
    TestFalse(TEXT("an empty file"), SOLPlayerGuide::IsGuideCurrent(FString(), generated));
    TestFalse(TEXT("a case change"), SOLPlayerGuide::IsGuideCurrent(TEXT("# guide\nline one\nline two\n"), generated));

    // The real guide written with Windows line endings is still current
    const FString guide = SOLPlayerGuide::BuildMarkdown(SOLKeyBindings::DefaultKeys());
    const FString windows = guide.Replace(TEXT("\n"), TEXT("\r\n"));
    TestTrue(TEXT("the guide with CRLF endings"), SOLPlayerGuide::IsGuideCurrent(windows, guide));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuidePathTest, "SOLTest.PlayerGuide.GuideFilePath",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The guide file sits at Docs/PLAYER_GUIDE.md under the project directory
bool FSOLPlayerGuidePathTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("project dir + PLAYER_GUIDE_FILE"), SOLPlayerGuide::GuideFilePath(),
        FPaths::ProjectDir() + SOL::Paths::PLAYER_GUIDE_FILE);
    TestTrue(TEXT("ends in Docs/PLAYER_GUIDE.md"),
        SOLPlayerGuide::GuideFilePath().EndsWith(TEXT("Docs/PLAYER_GUIDE.md")));
    return true;
}

#endif
