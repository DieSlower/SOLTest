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

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a generated player guide (Docs/SDDs/33-player-documentation.md sections 9 and 10,
// D8, D24, D27), written against the header contract (Menu/SOLPlayerGuide.h) before the production code existed: the
// markdown is deterministic and built only from the Controls rows, the glossary table and the key array, and the
// staleness comparison ignores line endings. Part 33e adds the -SOLWritePlayerGuide pieces (the flag test, the shot
// list, the image and capture paths, the image references, the BOM-less UTF-8 writer, the stale message) and the two
// checks on the committed files: SOLTest.PlayerGuide.UpToDate (the committed guide against the generated text, D27)
// and SOLTest.PlayerGuide.ImagesExist (every image the guide shows is committed). Part 33e-iii (D46), also test-first:
// a tab taller than the window is shot in scrolled pages (TabPages from PLAYER_GUIDE_TAB_PAGES, PageCount and
// PageOffset from the scroll extent, PageFileName's "-<n>" suffix), so the shot list, the guide's images and their alt
// texts ("..., part n of m") follow the page counts.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float PLAYERGUIDETEST_PAGE_TOLERANCE = 1.0e-3f;   // Scroll offset comparisons (menu units)
    constexpr float PLAYERGUIDETEST_VIEW = 556.0f;              // The cross-check view height (contract E)
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

    //////////////////////////////////////////////////////////////////////////
    // Returns a file's raw bytes, empty when it cannot be read
    TArray<uint8> PlayerGuideTestBytes(const FString& path)
    {
        TArray<uint8> bytes;
        FFileHelper::LoadFileToArray(bytes, *path, FILEREAD_Silent);
        return bytes;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the first bytes of an array as hex text ("61 0A ..."), for failure messages
    FString PlayerGuideTestHex(const TArray<uint8>& bytes)
    {
        constexpr int32 shownMax = 24;
        const int32 shown = FMath::Min(bytes.Num(), shownMax);
        FString out;
        for (int32 index = 0; index < shown; ++index)
        {
            out += FString::Printf(TEXT("%s%02X"), index > 0 ? TEXT(" ") : TEXT(""), bytes[index]);
        }
        return bytes.Num() > shown ? out + TEXT(" ...") : out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether bytes start with the UTF-8 byte order mark EF BB BF
    bool PlayerGuideTestHasBom(const TArray<uint8>& bytes)
    {
        return bytes.Num() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether bytes look like UTF-16: an FF FE or FE FF start, or a zero byte
    bool PlayerGuideTestLooksUtf16(const TArray<uint8>& bytes)
    {
        const bool bUtf16Bom = bytes.Num() >= 2
            && ((bytes[0] == 0xFF && bytes[1] == 0xFE) || (bytes[0] == 0xFE && bytes[1] == 0xFF));
        return bUtf16Bom || bytes.Contains(0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the 1-based number of the first line two texts differ at (CRLF read as LF), or 0 when none does
    int32 PlayerGuideTestFirstDifferentLine(const FString& first, const FString& second)
    {
        TArray<FString> firstLines;
        TArray<FString> secondLines;
        first.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(firstLines, TEXT("\n"), false);
        second.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(secondLines, TEXT("\n"), false);
        const int32 lineCount = FMath::Max(firstLines.Num(), secondLines.Num());
        for (int32 index = 0; index < lineCount; ++index)
        {
            const bool bBoth = firstLines.IsValidIndex(index) && secondLines.IsValidIndex(index);
            if (!bBoth || !firstLines[index].Equals(secondLines[index], ESearchCase::CaseSensitive))
            {
                return index + 1;
            }
        }
        return 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that a list of texts equals the expected one, count and every item in order
    void PlayerGuideTestExpectList(FAutomationTestBase& test, const FString& what, const TArray<FString>& actual,
        const TArray<FString>& expected)
    {
        test.TestEqual(what + TEXT(": count"), actual.Num(), expected.Num());
        const int32 common = FMath::Min(actual.Num(), expected.Num());
        for (int32 index = 0; index < common; ++index)
        {
            test.TestEqual(FString::Printf(TEXT("%s: item %d"), *what, index), actual[index], expected[index]);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a Help tab's page count from PLAYER_GUIDE_TAB_PAGES, 0 for Count or a tab past the table
    int32 PlayerGuideTestTabPages(const ESOLHelpTab tab)
    {
        const int32 index = static_cast<int32>(tab);
        const int32 known = FMath::Min(static_cast<int32>(ESOLHelpTab::Count),
            static_cast<int32>(UE_ARRAY_COUNT(SOL::Paths::PLAYER_GUIDE_TAB_PAGES)));
        return index >= 0 && index < known ? SOL::Paths::PLAYER_GUIDE_TAB_PAGES[index] : 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sum of PLAYER_GUIDE_TAB_PAGES over the Help tabs
    int32 PlayerGuideTestPageSum()
    {
        int32 sum = 0;
        for (int32 tab = 0; tab < static_cast<int32>(ESOLHelpTab::Count); ++tab)
        {
            sum += PlayerGuideTestTabPages(static_cast<ESOLHelpTab>(tab));
        }
        return sum;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a page's file name as the PageFileName contract (E) gives it: page 0 or less the base name, else the
    // base name with "-<page + 1>" before its last '.' (appended without one); null gives an empty name
    FString PlayerGuideTestPageName(const TCHAR* baseName, const int32 page)
    {
        if (baseName == nullptr)
        {
            return FString();
        }
        const FString base(baseName);
        if (page <= 0)
        {
            return base;
        }
        const FString suffix = FString::Printf(TEXT("-%d"), page + 1);
        int32 dot = INDEX_NONE;
        return base.FindLastChar(TEXT('.'), dot) ? base.Left(dot) + suffix + base.Mid(dot) : base + suffix;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a shot with every field set
    FSOLPlayerGuideShot PlayerGuideTestShot(const ESOLHelpTab tab, const bool bHover, const int32 page,
        const FString& fileName)
    {
        FSOLPlayerGuideShot shot;
        shot.Tab = tab;
        shot.bHover = bHover;
        shot.Page = page;
        shot.FileName = fileName;
        return shot;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the shots the contract (E) lists, in order: the Keyboard tab's pages, the Keyboard hover shot, then each
    // glossary tab's pages in ESOLHelpTab order, the names from SOL::Paths with the page suffix
    TArray<FSOLPlayerGuideShot> PlayerGuideTestExpectedShots()
    {
        using namespace SOL::Paths;
        TArray<FSOLPlayerGuideShot> shots;
        for (int32 page = 0; page < PlayerGuideTestTabPages(ESOLHelpTab::Keyboard); ++page)
        {
            shots.Add(PlayerGuideTestShot(ESOLHelpTab::Keyboard, false, page,
                PlayerGuideTestPageName(PLAYER_GUIDE_IMAGE_KEYBOARD, page)));
        }
        shots.Add(PlayerGuideTestShot(ESOLHelpTab::Keyboard, true, 0, PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER));

        // The glossary tabs follow Keyboard in the enum; each one's image is PLAYER_GUIDE_IMAGE_GLOSSARY[tab - 1]
        const int32 firstGlossary = static_cast<int32>(ESOLHelpTab::FlightHud);
        for (int32 tab = firstGlossary; tab < static_cast<int32>(ESOLHelpTab::Count); ++tab)
        {
            const int32 glossary = tab - firstGlossary;
            if (glossary >= static_cast<int32>(UE_ARRAY_COUNT(PLAYER_GUIDE_IMAGE_GLOSSARY)))
            {
                break;
            }
            for (int32 page = 0; page < PlayerGuideTestTabPages(static_cast<ESOLHelpTab>(tab)); ++page)
            {
                shots.Add(PlayerGuideTestShot(static_cast<ESOLHelpTab>(tab), false, page,
                    PlayerGuideTestPageName(PLAYER_GUIDE_IMAGE_GLOSSARY[glossary], page)));
            }
        }
        return shots;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the guide's image targets in the contract order: "Images/" + each expected shot's file name
    TArray<FString> PlayerGuideTestExpectedTargets()
    {
        TArray<FString> targets;
        for (const FSOLPlayerGuideShot& shot : PlayerGuideTestExpectedShots())
        {
            targets.Add(FString(TEXT("Images/")) + shot.FileName);
        }
        return targets;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an image's alt text as the contract (E) gives it: the hover shot GUIDE_KEYBOARD_HOVER_ALT; page 0 the
    // tab's alt (GUIDE_KEYBOARD_ALT, a glossary tab's heading); a later page GUIDE_IMAGE_PART_FORMAT over that alt, the
    // 1-based page and the tab's page count
    FString PlayerGuideTestAlt(const FSOLPlayerGuideShot& shot)
    {
        if (shot.bHover)
        {
            return SOLMenuText::GUIDE_KEYBOARD_HOVER_ALT;
        }
        const int32 glossary = static_cast<int32>(shot.Tab) - static_cast<int32>(ESOLHelpTab::FlightHud);
        const bool bGlossary = glossary >= 0 && glossary < static_cast<int32>(UE_ARRAY_COUNT(
            SOLMenuText::GLOSSARY_TABS));
        const FString alt = shot.Tab == ESOLHelpTab::Keyboard ? FString(SOLMenuText::GUIDE_KEYBOARD_ALT)
            : (bGlossary ? FString(SOLMenuText::GLOSSARY_TABS[glossary]) : FString());
        if (shot.Page <= 0)
        {
            return alt;
        }
        FStringFormatOrderedArguments arguments;
        arguments.Add(FStringFormatArg(alt));
        arguments.Add(FStringFormatArg(shot.Page + 1));
        arguments.Add(FStringFormatArg(PlayerGuideTestTabPages(shot.Tab)));
        return FString::Format(SOLMenuText::GUIDE_IMAGE_PART_FORMAT, arguments);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the scroll step the PageCount / PageOffset contract (E) gives a view: the view less the overlap, at
    // least 1
    float PlayerGuideTestStep(const float viewHeight)
    {
        return FMath::Max(viewHeight - SOL::Paths::PLAYER_GUIDE_PAGE_OVERLAP, 1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the page count the contract (E) gives: 1 for a bad view or no scroll (an end up to the tolerance), else
    // one page plus as many steps as reach the end less the tolerance, at most PLAYER_GUIDE_MAX_PAGES
    int32 PlayerGuideTestPageCount(const float viewHeight, const float scrollEnd)
    {
        using namespace SOL::Paths;
        if (!FMath::IsFinite(viewHeight) || viewHeight <= 0.0f || !FMath::IsFinite(scrollEnd)
            || scrollEnd <= PLAYER_GUIDE_SCROLL_TOLERANCE)
        {
            return 1;
        }
        const int32 count = 1 + FMath::CeilToInt32((scrollEnd - PLAYER_GUIDE_SCROLL_TOLERANCE)
            / PlayerGuideTestStep(viewHeight));
        return FMath::Min(count, PLAYER_GUIDE_MAX_PAGES);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a page's scroll offset the contract (E) gives: 0 for page 0 or less, a bad view or an end not finite or
    // not positive, else the page's steps capped at the end
    float PlayerGuideTestPageOffset(const int32 page, const float viewHeight, const float scrollEnd)
    {
        if (page <= 0 || !FMath::IsFinite(viewHeight) || viewHeight <= 0.0f || !FMath::IsFinite(scrollEnd)
            || scrollEnd <= 0.0f)
        {
            return 0.0f;
        }
        return FMath::Min(static_cast<float>(page) * PlayerGuideTestStep(viewHeight), scrollEnd);
    }

    //////////////////////////////////////////////////////////////////////////
    // Reports a stale-guide error unless a committed image exists and starts with the PNG signature (a Git LFS
    // pointer file, from a clone without LFS, is not an image)
    void PlayerGuideTestExpectFile(FAutomationTestBase& test, const FString& path)
    {
        static const uint8 PNG_SIGNATURE[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        TArray<uint8> bytes;
        const bool bRead = FPaths::FileExists(path) && FFileHelper::LoadFileToArray(bytes, *path);
        const bool bPng = bRead && bytes.Num() > static_cast<int32>(UE_ARRAY_COUNT(PNG_SIGNATURE))
            && FMemory::Memcmp(bytes.GetData(), PNG_SIGNATURE, UE_ARRAY_COUNT(PNG_SIGNATURE)) == 0;
        if (!bPng)
        {
            test.AddError(SOLPlayerGuide::StaleMessage() + FString::Printf(TEXT(" (missing or not a PNG: %s)"),
                *path));
        }
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
// Every glossary entry's name and formatted description is in the guide, with an image per page of each Help tab and
// the Keyboard hover image (D46)
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
    TestEqual(TEXT("an image per tab page plus the hover image"), images, PlayerGuideTestPageSum() + 1);
    TestTrue(FString::Printf(TEXT("at least one image per Help tab (%d)"), images),
        images >= static_cast<int32>(ESOLHelpTab::Count) + 1);
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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideWriteFlagTest, "SOLTest.PlayerGuide.WriteFlag",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// -SOLWritePlayerGuide (or /SOLWritePlayerGuide) asks for the guide run as a whole switch, case ignored, read from the
// given command line only
bool FSOLPlayerGuideWriteFlagTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* const accepted[] = { TEXT("-SOLWritePlayerGuide"), TEXT("-game -SOLWritePlayerGuide -windowed"),
        TEXT("-game -windowed -solwriteplayerguide"), TEXT("\"D:/x/SOLTest.uproject\" -game -SOLWritePlayerGuide"),
        TEXT("/SOLWritePlayerGuide") };
    for (const TCHAR* commandLine : accepted)
    {
        TestTrue(FString::Printf(TEXT("'%s' asks for the guide"), commandLine),
            SOLPlayerGuide::IsWriteRequested(commandLine));
    }
    const TCHAR* const rejected[] = { TEXT(""), TEXT("-game -windowed"), TEXT("-SOLWritePlayerGuideX"),
        TEXT("-SOLWritePlayerGuide2 -game"), TEXT("-SOLMenu"), TEXT("-SOLSmokeSolidMenu"), TEXT("SOLWritePlayerGuide"),
        TEXT("-XSOLWritePlayerGuide") };
    for (const TCHAR* commandLine : rejected)
    {
        TestFalse(FString::Printf(TEXT("'%s' does not ask for the guide"), commandLine),
            SOLPlayerGuide::IsWriteRequested(commandLine));
    }
    const FString fromConstant = FString(TEXT("-")) + SOL::CommandLine::WRITE_PLAYER_GUIDE;
    TestTrue(FString::Printf(TEXT("the flag from the constant ('%s')"), *fromConstant),
        SOLPlayerGuide::IsWriteRequested(*fromConstant));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideShotsTest, "SOLTest.PlayerGuide.Shots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shots (D46) are the Keyboard tab's pages, one Keyboard hover shot, then each glossary tab's pages in ESOLHelpTab
// order, as many pages per tab as PLAYER_GUIDE_TAB_PAGES says, page 0 first; the names are SOL::Paths' with the page
// suffix, unique bare .png names; only the hover shot hovers; the list is stable
bool FSOLPlayerGuideShotsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Paths;
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    const int32 glossaryTabs = static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_TABS));
    TestEqual(TEXT("one glossary image name per glossary tab"),
        static_cast<int32>(UE_ARRAY_COUNT(PLAYER_GUIDE_IMAGE_GLOSSARY)), glossaryTabs);
    TestEqual(TEXT("a glossary tab per Help tab after Keyboard"), glossaryTabs,
        static_cast<int32>(ESOLHelpTab::Count) - 1);
    TestEqual(TEXT("a shot per tab page plus the hover shot"), shots.Num(), PlayerGuideTestPageSum() + 1);

    // The expected shots, in order
    const TArray<FSOLPlayerGuideShot> expected = PlayerGuideTestExpectedShots();
    TestEqual(TEXT("the expected list has every page and the hover shot"), expected.Num(),
        PlayerGuideTestPageSum() + 1);
    TestEqual(TEXT("as many shots as expected"), shots.Num(), expected.Num());
    const int32 common = FMath::Min(shots.Num(), expected.Num());
    for (int32 index = 0; index < common; ++index)
    {
        TestEqual(FString::Printf(TEXT("shot %d tab"), index), static_cast<int32>(shots[index].Tab),
            static_cast<int32>(expected[index].Tab));
        TestTrue(FString::Printf(TEXT("shot %d hover is %d"), index, expected[index].bHover ? 1 : 0),
            shots[index].bHover == expected[index].bHover);
        TestEqual(FString::Printf(TEXT("shot %d page"), index), shots[index].Page, expected[index].Page);
        TestEqual(FString::Printf(TEXT("shot %d file name"), index), shots[index].FileName, expected[index].FileName);
    }

    // Per tab: its pages without hover, numbered 0 .. pages - 1 in order; exactly one hover shot; unique bare names
    int32 perTab[static_cast<int32>(ESOLHelpTab::Count)] = {};
    int32 hoverShots = 0;
    TSet<FString> names;
    for (int32 index = 0; index < shots.Num(); ++index)
    {
        const FSOLPlayerGuideShot& shot = shots[index];
        const int32 tab = static_cast<int32>(shot.Tab);
        const bool bHelpTab = tab >= 0 && tab < static_cast<int32>(ESOLHelpTab::Count);
        TestTrue(FString::Printf(TEXT("shot %d tab %d is a Help tab"), index, tab), bHelpTab);
        TestTrue(FString::Printf(TEXT("shot %d page %d in 0 .. PLAYER_GUIDE_MAX_PAGES - 1"), index, shot.Page),
            shot.Page >= 0 && shot.Page < PLAYER_GUIDE_MAX_PAGES);
        if (shot.bHover)
        {
            ++hoverShots;
            TestTrue(FString::Printf(TEXT("hover shot %d is the Keyboard tab"), index),
                shot.Tab == ESOLHelpTab::Keyboard);
            TestEqual(FString::Printf(TEXT("hover shot %d is page 0"), index), shot.Page, 0);
            TestEqual(FString::Printf(TEXT("hover shot %d name"), index), shot.FileName,
                FString(PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER));
        }
        else if (bHelpTab)
        {
            TestEqual(FString::Printf(TEXT("shot %d is the tab's next page"), index), shot.Page, perTab[tab]);
            ++perTab[tab];
        }
        const FString& name = shot.FileName;
        TestFalse(FString::Printf(TEXT("shot %d has a file name"), index), name.IsEmpty());
        TestTrue(FString::Printf(TEXT("shot %d '%s' ends in .png"), index, *name),
            name.EndsWith(TEXT(".png"), ESearchCase::CaseSensitive));
        TestFalse(FString::Printf(TEXT("shot %d '%s' has no path separator"), index, *name),
            name.Contains(TEXT("/")) || name.Contains(TEXT("\\")));
        TestFalse(FString::Printf(TEXT("shot %d '%s' is unique"), index, *name), names.Contains(name));
        names.Add(name);
    }
    for (int32 tab = 0; tab < static_cast<int32>(ESOLHelpTab::Count); ++tab)
    {
        TestEqual(FString::Printf(TEXT("Help tab %d has its pages"), tab), perTab[tab],
            PlayerGuideTestTabPages(static_cast<ESOLHelpTab>(tab)));
    }
    TestEqual(TEXT("exactly one hover shot"), hoverShots, 1);

    // The glossary tabs follow Keyboard in the enum in GLOSSARY_TABS order
    for (int32 index = 0; index < glossaryTabs; ++index)
    {
        TestEqual(FString::Printf(TEXT("glossary tab %d '%s' is Help tab %d"), index, SOLMenuText::GLOSSARY_TABS[index],
            index + 1), static_cast<int32>(SOLGlossary::TabFromGlossaryName(SOLMenuText::GLOSSARY_TABS[index])),
            index + 1);
    }

    // Stable across calls
    const TConstArrayView<FSOLPlayerGuideShot> again = SOLPlayerGuide::Shots();
    TestEqual(TEXT("the same count twice"), again.Num(), shots.Num());
    for (int32 index = 0; index < FMath::Min(again.Num(), shots.Num()); ++index)
    {
        TestTrue(FString::Printf(TEXT("shot %d the same twice"), index), again[index].Tab == shots[index].Tab
            && again[index].bHover == shots[index].bHover && again[index].Page == shots[index].Page
            && again[index].FileName.Equals(shots[index].FileName, ESearchCase::CaseSensitive));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuidePathsTest, "SOLTest.PlayerGuide.Paths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An image sits in Docs/Images/ under the project directory, its capture is the prefixed name in the screenshot folder
// as a full path, and distinct shots (every page among them) give distinct paths
bool FSOLPlayerGuidePathsTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    TestTrue(TEXT("there are shots to check"), shots.Num() > 0);
    TestEqual(TEXT("the keyboard image path"), SOLPlayerGuide::ImagePath(SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD),
        FPaths::ProjectDir() + SOL::Paths::PLAYER_GUIDE_IMAGE_DIR + SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD);
    const FString secondPage = PlayerGuideTestPageName(SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD, 1);
    TestEqual(TEXT("the keyboard's second page image path"), SOLPlayerGuide::ImagePath(*secondPage),
        FPaths::ProjectDir() + SOL::Paths::PLAYER_GUIDE_IMAGE_DIR + secondPage);
    TestTrue(TEXT("the second page image ends in Docs/Images/33-help-keyboard-2.png"),
        SOLPlayerGuide::ImagePath(*secondPage).EndsWith(TEXT("Docs/Images/33-help-keyboard-2.png")));
    TSet<FString> images;
    TSet<FString> captures;
    for (int32 index = 0; index < shots.Num(); ++index)
    {
        const FString& name = shots[index].FileName;
        if (name.IsEmpty())
        {
            AddError(FString::Printf(TEXT("shot %d has no file name"), index));
            continue;
        }
        const FString image = SOLPlayerGuide::ImagePath(*name);
        TestEqual(FString::Printf(TEXT("'%s' image path"), *name), image,
            FPaths::ProjectDir() + SOL::Paths::PLAYER_GUIDE_IMAGE_DIR + name);

        // The capture: the prefixed name in the screenshot folder, as a full path apart from the image
        const FString capture = SOLPlayerGuide::CapturePath(*name);
        const FString captureName = FString(SOL::Paths::PLAYER_GUIDE_CAPTURE_PREFIX) + name;
        TestEqual(FString::Printf(TEXT("'%s' capture path"), *name), capture,
            FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / captureName));
        TestFalse(FString::Printf(TEXT("'%s' capture path is full ('%s')"), *name, *capture),
            FPaths::IsRelative(capture));
        TestTrue(FString::Printf(TEXT("'%s' capture path ends with '%s'"), *name, *captureName),
            capture.EndsWith(captureName));
        TestFalse(FString::Printf(TEXT("'%s' capture differs from the image"), *name), capture.Equals(image));
        TestFalse(FString::Printf(TEXT("'%s' capture differs from the full image path"), *name),
            capture.Equals(FPaths::ConvertRelativePathToFull(image)));
        images.Add(image);
        captures.Add(capture);
    }
    TestEqual(TEXT("distinct image paths"), images.Num(), shots.Num());
    TestEqual(TEXT("distinct capture paths"), captures.Num(), shots.Num());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideReferencedImagesTest, "SOLTest.PlayerGuide.ReferencedImages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every markdown image's target is listed in text order (plain links and unclosed images are not), and the real guide
// shows exactly the shots' images from Docs/ in Shots() order, each with its alt text (page 0 the tab's, later pages
// "..., part n of m", D46)
bool FSOLPlayerGuideReferencedImagesTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> targets = { TEXT("stale") };
    SOLPlayerGuide::ReferencedImages(TEXT("a ![x](one.png) b ![y z](dir/two.png)\n![](three.png)"), targets);
    PlayerGuideTestExpectList(*this, TEXT("three images"), targets,
        { TEXT("one.png"), TEXT("dir/two.png"), TEXT("three.png") });

    // Emptied first, also when nothing is found
    targets = { TEXT("stale") };
    SOLPlayerGuide::ReferencedImages(FString(), targets);
    PlayerGuideTestExpectList(*this, TEXT("empty text"), targets, {});
    targets = { TEXT("stale") };
    SOLPlayerGuide::ReferencedImages(TEXT("no image here, only text"), targets);
    PlayerGuideTestExpectList(*this, TEXT("no image"), targets, {});

    // Plain links and unclosed images are not images
    SOLPlayerGuide::ReferencedImages(TEXT("see [x](four.png)"), targets);
    PlayerGuideTestExpectList(*this, TEXT("a plain link"), targets, {});
    SOLPlayerGuide::ReferencedImages(TEXT("![a](one.png) and [x](four.png)"), targets);
    PlayerGuideTestExpectList(*this, TEXT("an image and a plain link"), targets, { TEXT("one.png") });
    SOLPlayerGuide::ReferencedImages(TEXT("text ![x](five.png"), targets);
    PlayerGuideTestExpectList(*this, TEXT("an unclosed image"), targets, {});
    SOLPlayerGuide::ReferencedImages(TEXT("![a](one.png) then ![x](five.png"), targets);
    PlayerGuideTestExpectList(*this, TEXT("an image then an unclosed one"), targets, { TEXT("one.png") });

    // Images at the very start and end, and repeats
    SOLPlayerGuide::ReferencedImages(TEXT("![s](start.png) middle ![e](end.png)"), targets);
    PlayerGuideTestExpectList(*this, TEXT("start and end"), targets, { TEXT("start.png"), TEXT("end.png") });
    SOLPlayerGuide::ReferencedImages(TEXT("![a](r.png) ![b](r.png)"), targets);
    PlayerGuideTestExpectList(*this, TEXT("a repeated image"), targets, { TEXT("r.png"), TEXT("r.png") });

    // The real guide: "Images/" + each shot's file name, in Shots() order
    const FString guide = SOLPlayerGuide::BuildMarkdown(SOLKeyBindings::DefaultKeys());
    SOLPlayerGuide::ReferencedImages(guide, targets);
    PlayerGuideTestExpectList(*this, TEXT("the guide's images"), targets, PlayerGuideTestExpectedTargets());
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    TestEqual(TEXT("one guide image per shot"), targets.Num(), shots.Num());
    for (int32 index = 0; index < FMath::Min(targets.Num(), shots.Num()); ++index)
    {
        TestEqual(FString::Printf(TEXT("guide image %d is shot %d"), index, index), targets[index],
            FString(TEXT("Images/")) + shots[index].FileName);
    }

    // The part format at a sample (cross-check), then each image with its alt text
    FStringFormatOrderedArguments sample;
    sample.Add(FStringFormatArg(FString(TEXT("Alt"))));
    sample.Add(FStringFormatArg(2));
    sample.Add(FStringFormatArg(3));
    TestEqual(TEXT("GUIDE_IMAGE_PART_FORMAT at a sample"),
        FString::Format(SOLMenuText::GUIDE_IMAGE_PART_FORMAT, sample),
        FString(TEXT("Alt, part 2 of 3")));
    for (const FSOLPlayerGuideShot& shot : PlayerGuideTestExpectedShots())
    {
        const FString image = FString::Printf(TEXT("![%s](Images/%s)"), *PlayerGuideTestAlt(shot), *shot.FileName);
        TestTrue(FString::Printf(TEXT("the guide shows %s"), *image),
            guide.Contains(image, ESearchCase::CaseSensitive));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideWriteFileTest, "SOLTest.PlayerGuide.WriteFile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The writer makes the folder, writes UTF-8 without a BOM and the characters as given, replaces the file, writes an
// empty text as an empty file, and fails cleanly when the folder cannot be made
bool FSOLPlayerGuideWriteFileTest::RunTest(const FString& /*parameters*/)
{
    IFileManager& files = IFileManager::Get();
    const FString root = FPaths::AutomationTransientDir() / TEXT("PlayerGuideTest");
    files.DeleteDirectory(*root, false, true);
    const FString folder = root / TEXT("Fresh") / TEXT("Sub");
    TestFalse(TEXT("the folder does not exist yet"), files.DirectoryExists(*folder));
    const FString path = folder / TEXT("guide.md");

    // ASCII text: the folder is made, the bytes are exact (no BOM, not UTF-16, no CR added)
    TestTrue(TEXT("writes 'a\\nb\\n'"), SOLPlayerGuide::WriteGuideFile(path, TEXT("a\nb\n")));
    TestTrue(TEXT("the file exists"), FPaths::FileExists(path));
    TArray<uint8> bytes = PlayerGuideTestBytes(path);
    TestFalse(TEXT("no UTF-8 byte order mark"), PlayerGuideTestHasBom(bytes));
    TestFalse(TEXT("not UTF-16"), PlayerGuideTestLooksUtf16(bytes));
    const TArray<uint8> expectedLf = { 0x61, 0x0A, 0x62, 0x0A };
    TestTrue(FString::Printf(TEXT("'a\\nb\\n' is 61 0A 62 0A (got %s)"), *PlayerGuideTestHex(bytes)),
        bytes == expectedLf);

    // A non-ASCII character as UTF-8, read back the same
    const FString copyright = FString::Chr(static_cast<TCHAR>(0x00A9));
    TestTrue(TEXT("writes the copyright sign"), SOLPlayerGuide::WriteGuideFile(path, copyright));
    bytes = PlayerGuideTestBytes(path);
    const TArray<uint8> expectedCopyright = { 0xC2, 0xA9 };
    TestTrue(FString::Printf(TEXT("the copyright sign is C2 A9 (got %s)"), *PlayerGuideTestHex(bytes)),
        bytes == expectedCopyright);
    FString read;
    TestTrue(TEXT("the copyright sign reads back"), FFileHelper::LoadFileToString(read, *path)
        && read.Equals(copyright, ESearchCase::CaseSensitive));

    // A CRLF in the text is kept as given
    TestTrue(TEXT("writes 'a\\r\\nb'"), SOLPlayerGuide::WriteGuideFile(path, TEXT("a\r\nb")));
    bytes = PlayerGuideTestBytes(path);
    const TArray<uint8> expectedCrLf = { 0x61, 0x0D, 0x0A, 0x62 };
    TestTrue(FString::Printf(TEXT("'a\\r\\nb' is 61 0D 0A 62 (got %s)"), *PlayerGuideTestHex(bytes)),
        bytes == expectedCrLf);

    // The real guide round-trips, LF only and without a BOM
    const FString guide = SOLPlayerGuide::BuildMarkdown(SOLKeyBindings::DefaultKeys());
    TestTrue(TEXT("writes the guide"), SOLPlayerGuide::WriteGuideFile(path, guide));
    read.Reset();
    TestTrue(TEXT("the guide reads back"), FFileHelper::LoadFileToString(read, *path));
    TestTrue(TEXT("the guide reads back exactly"), !guide.IsEmpty() && read.Equals(guide, ESearchCase::CaseSensitive));
    TestTrue(TEXT("the read guide is current"), SOLPlayerGuide::IsGuideCurrent(read, guide));
    bytes = PlayerGuideTestBytes(path);
    TestTrue(TEXT("the guide file has bytes"), bytes.Num() > 0);
    TestFalse(TEXT("the guide file has no BOM"), PlayerGuideTestHasBom(bytes));
    TestFalse(TEXT("the guide file has no CR byte"), bytes.Contains(0x0D));

    // Replacing a long text with a short one leaves only the short one
    TestTrue(TEXT("writes a long text"), SOLPlayerGuide::WriteGuideFile(path, FString::ChrN(1000, TEXT('x'))));
    const FString shortText = TEXT("short\n");
    TestTrue(TEXT("writes a short text"), SOLPlayerGuide::WriteGuideFile(path, shortText));
    bytes = PlayerGuideTestBytes(path);
    const TArray<uint8> expectedShort = { 's', 'h', 'o', 'r', 't', 0x0A };
    TestTrue(FString::Printf(TEXT("only the short text is left (got %s)"), *PlayerGuideTestHex(bytes)),
        bytes == expectedShort);

    // An empty text gives an empty file
    TestTrue(TEXT("writes an empty text"), SOLPlayerGuide::WriteGuideFile(path, FString()));
    TestTrue(TEXT("the empty file exists"), FPaths::FileExists(path));
    TestEqual(TEXT("the empty file has 0 bytes"), files.FileSize(*path), static_cast<int64>(0));

    // A folder that cannot be made (a regular file is in the way) fails cleanly
    const FString blocker = root / TEXT("blocker");
    TestTrue(TEXT("the blocking file is made"), FFileHelper::SaveStringToFile(TEXT("block"), *blocker));
    TestFalse(TEXT("writing under a regular file fails"),
        SOLPlayerGuide::WriteGuideFile(blocker / TEXT("guide.md"), TEXT("x")));

    files.DeleteDirectory(*root, false, true);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideStaleMessageTest, "SOLTest.PlayerGuide.StaleMessage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The stale message is the menu text constant and names the batch file and the guide file
bool FSOLPlayerGuideStaleMessageTest::RunTest(const FString& /*parameters*/)
{
    const FString message = SOLPlayerGuide::StaleMessage();
    TestFalse(TEXT("has text"), message.IsEmpty());
    TestEqual(TEXT("is GUIDE_STALE_MESSAGE"), message, FString(SOLMenuText::GUIDE_STALE_MESSAGE));
    TestTrue(TEXT("names Tools\\WritePlayerGuide.bat"),
        message.Contains(TEXT("Tools\\WritePlayerGuide.bat"), ESearchCase::CaseSensitive));
    TestTrue(TEXT("names Docs/PLAYER_GUIDE.md"), message.Contains(TEXT("Docs/PLAYER_GUIDE.md"),
        ESearchCase::CaseSensitive));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideUpToDateTest, "SOLTest.PlayerGuide.UpToDate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The committed Docs/PLAYER_GUIDE.md matches the generated text, without a BOM and with LF endings (D27): otherwise
// the test fails naming Tools\WritePlayerGuide.bat
bool FSOLPlayerGuideUpToDateTest::RunTest(const FString& /*parameters*/)
{
    const FString path = SOLPlayerGuide::GuideFilePath();
    FString fileText;
    if (!FFileHelper::LoadFileToString(fileText, *path))
    {
        AddError(SOLPlayerGuide::StaleMessage() + FString::Printf(TEXT(" (missing: %s)"), *path));
        return true;
    }
    const FString generated = SOLPlayerGuide::BuildMarkdown(SOLKeyBindings::DefaultKeys());
    if (!SOLPlayerGuide::IsGuideCurrent(fileText, generated))
    {
        AddError(SOLPlayerGuide::StaleMessage() + FString::Printf(TEXT(" (%s differs first at line %d)"), *path,
            PlayerGuideTestFirstDifferentLine(fileText, generated)));
    }

    // The committed bytes: no BOM, LF endings
    const TArray<uint8> bytes = PlayerGuideTestBytes(path);
    if (PlayerGuideTestHasBom(bytes))
    {
        AddError(SOLPlayerGuide::StaleMessage() + FString::Printf(TEXT(" (%s starts with a UTF-8 BOM)"), *path));
    }
    if (bytes.Contains(0x0D))
    {
        AddError(SOLPlayerGuide::StaleMessage() + FString::Printf(TEXT(" (%s has CR line endings)"), *path));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImagesExistTest, "SOLTest.PlayerGuide.ImagesExist",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every image the generated guide shows is committed next to it as a PNG, and so is every shot's image (every page of
// every tab, D46); a missing one fails naming Tools\WritePlayerGuide.bat
bool FSOLPlayerGuideImagesExistTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> targets;
    SOLPlayerGuide::ReferencedImages(SOLPlayerGuide::BuildMarkdown(SOLKeyBindings::DefaultKeys()), targets);
    if (targets.IsEmpty())
    {
        AddError(SOLPlayerGuide::StaleMessage() + TEXT(" (the generated guide references no image)"));
    }
    const FString guideFolder = FPaths::GetPath(SOLPlayerGuide::GuideFilePath());
    for (const FString& target : targets)
    {
        PlayerGuideTestExpectFile(*this, guideFolder / target);
    }

    // The same files seen from SOL::Paths
    const TConstArrayView<FSOLPlayerGuideShot> shots = SOLPlayerGuide::Shots();
    if (shots.Num() == 0)
    {
        AddError(SOLPlayerGuide::StaleMessage() + TEXT(" (no shots listed)"));
    }
    for (int32 index = 0; index < shots.Num(); ++index)
    {
        if (shots[index].FileName.IsEmpty())
        {
            AddError(FString::Printf(TEXT("shot %d has no file name"), index));
            continue;
        }
        PlayerGuideTestExpectFile(*this, SOLPlayerGuide::ImagePath(*shots[index].FileName));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideTabPagesTest, "SOLTest.PlayerGuide.TabPages",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// TabPages (D46) reads PLAYER_GUIDE_TAB_PAGES, one entry per Help tab, each 1 .. PLAYER_GUIDE_MAX_PAGES; Count and a
// tab past the enum give 0
bool FSOLPlayerGuideTabPagesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Paths;
    TestEqual(TEXT("one page count per Help tab"), static_cast<int32>(UE_ARRAY_COUNT(PLAYER_GUIDE_TAB_PAGES)),
        static_cast<int32>(ESOLHelpTab::Count));
    TestTrue(TEXT("PLAYER_GUIDE_MAX_PAGES is at least 1"), PLAYER_GUIDE_MAX_PAGES >= 1);
    const int32 known = FMath::Min(static_cast<int32>(ESOLHelpTab::Count),
        static_cast<int32>(UE_ARRAY_COUNT(PLAYER_GUIDE_TAB_PAGES)));
    for (int32 tab = 0; tab < known; ++tab)
    {
        const int32 pages = PLAYER_GUIDE_TAB_PAGES[tab];
        TestEqual(FString::Printf(TEXT("tab %d: TabPages is the table's"), tab),
            SOLPlayerGuide::TabPages(static_cast<ESOLHelpTab>(tab)), pages);
        TestTrue(FString::Printf(TEXT("tab %d: %d pages in 1 .. PLAYER_GUIDE_MAX_PAGES"), tab, pages),
            pages >= 1 && pages <= PLAYER_GUIDE_MAX_PAGES);
    }
    TestEqual(TEXT("Count: 0"), SOLPlayerGuide::TabPages(ESOLHelpTab::Count), 0);
    TestEqual(TEXT("past the enum: 0"), SOLPlayerGuide::TabPages(static_cast<ESOLHelpTab>(200)), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuidePageCountTest, "SOLTest.PlayerGuide.PageCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PageCount and PageOffset (D46): one page for a bad view or no scroll, else one plus the steps (the view less the
// overlap, at least 1) that reach the scroll end less the tolerance, capped; a page's offset its steps capped at the
// end; the pages cover the whole content (consecutive offsets a step apart at most, the last within the tolerance of
// the end) and no page is spare; the contract's cross-checks at the current values
bool FSOLPlayerGuidePageCountTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Paths;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float view = PLAYERGUIDETEST_VIEW;

    // Cross-checks at the current values (overlap 0, so view 556 is a step of 556)
    struct FCountCase
    {
        float View;
        float End;
        int32 Pages;
    };
    const FCountCase counts[] = { { view, 0.0f, 1 }, { view, 0.5f, 1 }, { view, 100.0f, 2 }, { view, 556.0f, 2 },
        { view, 556.5f, 2 }, { view, 557.0f, 3 }, { view, 1.0e6f, 8 }, { 10.0f, 3.0f, 2 }, { 10.0f, 25.0f, 4 },
        { nan, 100.0f, 1 } };
    for (const FCountCase& count : counts)
    {
        TestEqual(FString::Printf(TEXT("PageCount(%f, %f)"), count.View, count.End),
            SOLPlayerGuide::PageCount(count.View, count.End), count.Pages);
    }
    TestEqual(TEXT("PageOffset(1, 556, 100)"), SOLPlayerGuide::PageOffset(1, view, 100.0f), 100.0f,
        PLAYERGUIDETEST_PAGE_TOLERANCE);
    TestEqual(TEXT("PageOffset(1, 556, 1000)"), SOLPlayerGuide::PageOffset(1, view, 1000.0f), 556.0f,
        PLAYERGUIDETEST_PAGE_TOLERANCE);
    TestEqual(TEXT("PageOffset(2, 556, 1000)"), SOLPlayerGuide::PageOffset(2, view, 1000.0f), 1000.0f,
        PLAYERGUIDETEST_PAGE_TOLERANCE);

    // Bad inputs: one page, offset 0
    const float badViews[] = { 0.0f, -1.0f, nan, inf, -inf };
    for (const float badView : badViews)
    {
        TestEqual(FString::Printf(TEXT("view %f: one page"), badView), SOLPlayerGuide::PageCount(badView, 1000.0f), 1);
        TestEqual(FString::Printf(TEXT("view %f: offset 0"), badView), SOLPlayerGuide::PageOffset(1, badView, 1000.0f),
            0.0f, PLAYERGUIDETEST_PAGE_TOLERANCE);
    }
    const float badEnds[] = { -5.0f, nan, inf, -inf };
    for (const float badEnd : badEnds)
    {
        TestEqual(FString::Printf(TEXT("end %f: one page"), badEnd), SOLPlayerGuide::PageCount(view, badEnd), 1);
        TestEqual(FString::Printf(TEXT("end %f: offset 0"), badEnd), SOLPlayerGuide::PageOffset(1, view, badEnd),
            0.0f, PLAYERGUIDETEST_PAGE_TOLERANCE);
    }
    TestEqual(TEXT("end 0: offset 0"), SOLPlayerGuide::PageOffset(1, view, 0.0f), 0.0f, PLAYERGUIDETEST_PAGE_TOLERANCE);
    const int32 badPages[] = { 0, -1, MIN_int32 };
    for (const int32 badPage : badPages)
    {
        TestEqual(FString::Printf(TEXT("page %d: offset 0"), badPage), SOLPlayerGuide::PageOffset(badPage, view,
            1000.0f), 0.0f, PLAYERGUIDETEST_PAGE_TOLERANCE);
    }

    // The sweep: the rules restated, then the cover property
    const float views[] = { 10.0f, 33.0f, 100.0f, view, 1080.0f, 2000.0f };
    const float ends[] = { 0.0f, 0.25f, PLAYER_GUIDE_SCROLL_TOLERANCE, 0.75f, 1.0f, 37.3f, 100.0f, 523.9f, 600.0f,
        1234.5f, 3000.0f, 1.0e5f };
    for (const float sweepView : views)
    {
        const float step = PlayerGuideTestStep(sweepView);
        for (const float end : ends)
        {
            const FString what = FString::Printf(TEXT("view %.2f end %.2f"), sweepView, end);
            const int32 pages = SOLPlayerGuide::PageCount(sweepView, end);
            TestEqual(what + TEXT(": page count"), pages, PlayerGuideTestPageCount(sweepView, end));
            TestTrue(what + TEXT(": 1 .. PLAYER_GUIDE_MAX_PAGES pages"), pages >= 1 && pages <= PLAYER_GUIDE_MAX_PAGES);

            // Each page's offset as restated, the first at the top, a step apart at most, never past the end
            float previous = 0.0f;
            for (int32 page = 0; page < pages; ++page)
            {
                const float offset = SOLPlayerGuide::PageOffset(page, sweepView, end);
                TestEqual(FString::Printf(TEXT("%s: page %d offset"), *what, page), offset,
                    PlayerGuideTestPageOffset(page, sweepView, end), PLAYERGUIDETEST_PAGE_TOLERANCE);
                if (page == 0)
                {
                    TestEqual(what + TEXT(": the first page at the top"), offset, 0.0f, PLAYERGUIDETEST_PAGE_TOLERANCE);
                }
                else
                {
                    TestTrue(FString::Printf(TEXT("%s: page %d at most a step on"), *what, page),
                        offset - previous >= -PLAYERGUIDETEST_PAGE_TOLERANCE
                        && offset - previous <= step + PLAYERGUIDETEST_PAGE_TOLERANCE);
                }
                TestTrue(FString::Printf(TEXT("%s: page %d not past the end"), *what, page),
                    offset <= FMath::Max(end, 0.0f) + PLAYERGUIDETEST_PAGE_TOLERANCE);
                previous = offset;
            }

            // Uncapped, the last page reaches the end within the tolerance and one page fewer would not
            if (pages < PLAYER_GUIDE_MAX_PAGES)
            {
                TestTrue(FString::Printf(TEXT("%s: the last page (%.3f) reaches the end"), *what, previous),
                    end - previous <= PLAYER_GUIDE_SCROLL_TOLERANCE + PLAYERGUIDETEST_PAGE_TOLERANCE);
                if (pages > 1)
                {
                    TestTrue(what + TEXT(": no spare page"), SOLPlayerGuide::PageOffset(pages - 2, sweepView, end)
                        < end - PLAYER_GUIDE_SCROLL_TOLERANCE + PLAYERGUIDETEST_PAGE_TOLERANCE);
                }
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuidePageFileNameTest, "SOLTest.PlayerGuide.PageFileName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PageFileName (D46): page 0 or less keeps the base name; a later page inserts "-<page + 1>" before the last '.' (or
// appends it without one); a null name gives an empty one
bool FSOLPlayerGuidePageFileNameTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Paths;
    TestEqual(TEXT("page 0 keeps the name"), SOLPlayerGuide::PageFileName(TEXT("33-help-keyboard.png"), 0),
        FString(TEXT("33-help-keyboard.png")));
    TestEqual(TEXT("page -1 keeps the name"), SOLPlayerGuide::PageFileName(TEXT("33-help-keyboard.png"), -1),
        FString(TEXT("33-help-keyboard.png")));
    TestEqual(TEXT("page 1 is the second image"), SOLPlayerGuide::PageFileName(TEXT("33-help-keyboard.png"), 1),
        FString(TEXT("33-help-keyboard-2.png")));
    TestEqual(TEXT("page 2 is the third image"), SOLPlayerGuide::PageFileName(TEXT("33-help-keyboard.png"), 2),
        FString(TEXT("33-help-keyboard-3.png")));
    TestEqual(TEXT("page 9 is the tenth image"), SOLPlayerGuide::PageFileName(TEXT("33-help-keyboard.png"), 9),
        FString(TEXT("33-help-keyboard-10.png")));
    TestEqual(TEXT("before the last dot"), SOLPlayerGuide::PageFileName(TEXT("a.b.png"), 1),
        FString(TEXT("a.b-2.png")));
    TestEqual(TEXT("no dot: appended"), SOLPlayerGuide::PageFileName(TEXT("noextension"), 1),
        FString(TEXT("noextension-2")));
    TestEqual(TEXT("no dot, page 0: unchanged"), SOLPlayerGuide::PageFileName(TEXT("noextension"), 0),
        FString(TEXT("noextension")));
    TestEqual(TEXT("null: empty"), SOLPlayerGuide::PageFileName(nullptr, 1), FString());
    TestEqual(TEXT("null, page 0: empty"), SOLPlayerGuide::PageFileName(nullptr, 0), FString());

    // Every guide image name over every page, as the rule restates
    TArray<const TCHAR*> bases = { PLAYER_GUIDE_IMAGE_KEYBOARD };
    for (const TCHAR* name : PLAYER_GUIDE_IMAGE_GLOSSARY)
    {
        bases.Add(name);
    }
    for (const TCHAR* base : bases)
    {
        TSet<FString> names;
        for (int32 page = 0; page < PLAYER_GUIDE_MAX_PAGES; ++page)
        {
            const FString name = SOLPlayerGuide::PageFileName(base, page);
            TestEqual(FString::Printf(TEXT("'%s' page %d"), base, page), name, PlayerGuideTestPageName(base, page));
            TestTrue(FString::Printf(TEXT("'%s' page %d still a .png"), base, page),
                name.EndsWith(TEXT(".png"), ESearchCase::CaseSensitive));
            TestFalse(FString::Printf(TEXT("'%s' page %d unique"), base, page), names.Contains(name));
            TestFalse(FString::Printf(TEXT("'%s' page %d is not the hover image"), base, page),
                name.Equals(PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER));
            names.Add(name);
        }
    }
    return true;
}

#endif
