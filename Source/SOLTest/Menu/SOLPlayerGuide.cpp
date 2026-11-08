/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLPlayerGuide.h"

#include "Menu/SOLGlossary.h"
#include "Menu/SOLKeyCaps.h"
#include "Menu/SOLKeyDiagram.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

static_assert(UE_ARRAY_COUNT(SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY) == UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_TABS),
    "one player guide image per glossary tab");

namespace
{
    constexpr TCHAR GUIDE_NEWLINE = TEXT('\n');
    constexpr const TCHAR* GUIDE_LF = TEXT("\n");                    // The line ending IsGuideCurrent compares with
    constexpr const TCHAR* GUIDE_CRLF = TEXT("\r\n");               // ... and the one it ignores
    constexpr const TCHAR* GUIDE_CELL_BAR = TEXT("|");              // A table cell's '|' and its escaped form
    constexpr const TCHAR* GUIDE_CELL_BAR_ESCAPED = TEXT("\\|");
    constexpr const TCHAR* GUIDE_PATH_SEPARATOR = TEXT("/");
    constexpr const TCHAR* GUIDE_EMPTY = TEXT("");
    constexpr const TCHAR* GUIDE_FLAG_PREFIX = TEXT("-");           // Before the command-line flag's name
    constexpr TCHAR GUIDE_HEX_COLOR_FORMAT[] = TEXT("#%02X%02X%02X"); // A colour as "#RRGGBB" (Printf: an array)
    constexpr TCHAR GUIDE_HASH_FORMAT[] = TEXT("%08X");             // The input hash as 8 hex digits
    constexpr const TCHAR* GUIDE_IMAGE_OPEN = TEXT("![");           // A markdown image: "![alt](target)"
    constexpr int32 GUIDE_IMAGE_OPEN_LEN = 2;
    constexpr const TCHAR* GUIDE_IMAGE_MIDDLE = TEXT("](");
    constexpr int32 GUIDE_IMAGE_MIDDLE_LEN = 2;
    constexpr TCHAR GUIDE_IMAGE_CLOSE = TEXT(')');
    constexpr int32 GUIDE_FIXED_SHOTS = 2;                          // The Keyboard tab's shots: plain and hovered

    //////////////////////////////////////////////////////////////////////////
    // Appends a line and its '\n'
    void GuideLine(FString& out, const FString& line)
    {
        out += line;
        out.AppendChar(GUIDE_NEWLINE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends an empty line
    void GuideBlankLine(FString& out)
    {
        out.AppendChar(GUIDE_NEWLINE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a text made safe for a markdown table cell (a '|' escaped)
    FString GuideCell(const FString& text)
    {
        return text.Replace(GUIDE_CELL_BAR, GUIDE_CELL_BAR_ESCAPED, ESearchCase::CaseSensitive);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a bindings cell as <kbd> tags: each cap in its own tag, a chord's caps joined by '+', the alternatives
    // by " / " (the caps of SOLKeyCaps::ParseCell, so the text is the Controls page's)
    FString GuideKeys(const TCHAR* cell, const TArray<FString>& keys, FSOLKeyCapModel& model)
    {
        SOLKeyCaps::ParseCell(cell, keys, model);
        FString out;
        for (int32 chord = 0; chord < model.Chords.Num(); ++chord)
        {
            if (chord > 0)
            {
                out += SOLMenuText::BINDING_ALTERNATIVE_SEPARATOR;
            }
            const FSOLKeyCapChord& caps = model.Chords[chord];
            for (int32 cap = 0; cap < caps.CapCount; ++cap)
            {
                if (cap > 0)
                {
                    out += SOLMenuText::BINDING_CHORD_DELIMITER;
                }
                const int32 capIndex = caps.FirstCap + cap;
                const FString& label = model.Caps.IsValidIndex(capIndex) ? model.Caps[capIndex].Label : FString();
                out += SOLMenuText::GUIDE_KEY_OPEN;
                out += GuideCell(label);
                out += SOLMenuText::GUIDE_KEY_CLOSE;
            }
        }
        return out;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the image folder relative to the guide file's folder ("Images/" for Docs/PLAYER_GUIDE.md and
    // Docs/Images/), so the guide's links work where it sits
    FString GuideImageDir()
    {
        const FString guideDir = FPaths::GetPath(SOL::Paths::PLAYER_GUIDE_FILE) + GUIDE_PATH_SEPARATOR;
        FString imageDir = SOL::Paths::PLAYER_GUIDE_IMAGE_DIR;
        if (imageDir.StartsWith(guideDir, ESearchCase::CaseSensitive))
        {
            return imageDir.RightChop(guideDir.Len());
        }
        FPaths::MakePathRelativeTo(imageDir, SOL::Paths::PLAYER_GUIDE_FILE);
        return imageDir;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a markdown image line of a file in the image folder
    void GuideImage(FString& out, const TCHAR* alt, const TCHAR* fileName)
    {
        GuideLine(out, FString::Format(SOLMenuText::GUIDE_IMAGE_FORMAT, { FString(alt), GuideImageDir() + fileName }));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a colour as "#RRGGBB"
    FString GuideHexColor(const FColor& color)
    {
        return FString::Printf(GUIDE_HEX_COLOR_FORMAT, color.R, color.G, color.B);
    }

    //////////////////////////////////////////////////////////////////////////
    // Folds a string into a running CRC
    uint32 GuideHash(const TCHAR* text, const uint32 crc)
    {
        return FCrc::StrCrc32(text != nullptr ? text : GUIDE_EMPTY, crc);
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends the Controls: one table per area in area order, then the keyboard images and the area legend
    void GuideControls(FString& out, const TArray<FString>& keys)
    {
        GuideLine(out, SOLMenuText::GUIDE_CONTROLS_HEADING);
        FSOLKeyCapModel model;
        for (int32 area = 0; area < static_cast<int32>(ESOLControlArea::Count); ++area)
        {
            const TCHAR* areaName = SOLKeyDiagram::AreaName(static_cast<ESOLControlArea>(area));
            GuideBlankLine(out);
            GuideLine(out, FString(SOLMenuText::GUIDE_SUBHEADING_PREFIX) + areaName);
            GuideBlankLine(out);
            GuideLine(out, SOLMenuText::GUIDE_CONTROLS_COLUMNS);
            GuideLine(out, SOLMenuText::GUIDE_TABLE_RULE);
            for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
            {
                if (FCString::Strcmp(row.Area, areaName) != 0)
                {
                    continue;
                }
                const FString keysCell = GuideKeys(row.Bindings, keys, model);
                GuideLine(out, FString::Printf(SOLMenuText::GUIDE_TABLE_ROW_FORMAT, *keysCell,
                    *GuideCell(SOLMenuContent::FormatControlAction(row.Action))));
            }
        }

        // The keyboard as the Help page draws it, and what its colours mean
        GuideBlankLine(out);
        GuideLine(out, SOLMenuText::GUIDE_KEYBOARD_HEADING);
        GuideBlankLine(out);
        GuideImage(out, SOLMenuText::GUIDE_KEYBOARD_ALT, SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD);
        GuideBlankLine(out);
        GuideImage(out, SOLMenuText::GUIDE_KEYBOARD_HOVER_ALT, SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER);
        GuideBlankLine(out);
        GuideLine(out, SOLMenuText::GUIDE_LEGEND_INTRO);
        GuideBlankLine(out);
        GuideLine(out, SOLMenuText::GUIDE_LEGEND_COLUMNS);
        GuideLine(out, SOLMenuText::GUIDE_TABLE_RULE);
        for (int32 area = 0; area < static_cast<int32>(ESOLControlArea::Count); ++area)
        {
            const ESOLControlArea value = static_cast<ESOLControlArea>(area);
            GuideLine(out, FString::Format(SOLMenuText::GUIDE_LEGEND_ROW_FORMAT,
                { FString(SOLKeyDiagram::AreaName(value)), GuideHexColor(SOLKeyDiagram::AreaColor(value)) }));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends the HUD glossary: per glossary tab its heading, its image and a table of its entries
    void GuideGlossary(FString& out, const TArray<FString>& keys)
    {
        GuideBlankLine(out);
        GuideLine(out, SOLMenuText::GUIDE_GLOSSARY_HEADING);
        TArray<int32> entries;
        FString description;
        for (int32 tab = 0; tab < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_TABS)); ++tab)
        {
            const TCHAR* heading = SOLMenuText::GLOSSARY_TABS[tab];
            GuideBlankLine(out);
            GuideLine(out, FString(SOLMenuText::GUIDE_SUBHEADING_PREFIX) + heading);
            GuideBlankLine(out);
            GuideImage(out, heading, SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY[tab]);
            GuideBlankLine(out);
            GuideLine(out, SOLMenuText::GUIDE_GLOSSARY_COLUMNS);
            GuideLine(out, SOLMenuText::GUIDE_TABLE_RULE);
            SOLGlossary::EntriesOfTab(SOLGlossary::TabFromGlossaryName(heading), entries);
            for (const int32 index : entries)
            {
                const SOLMenuText::FGlossaryEntry& entry = SOLMenuText::GLOSSARY_ENTRIES[index];
                SOLGlossary::FormatEntryText(entry.Description, keys, description);
                GuideLine(out, FString::Printf(SOLMenuText::GUIDE_TABLE_ROW_FORMAT, *GuideCell(entry.Name),
                    *GuideCell(description)));
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a guide text with LF line endings and no trailing newlines (the form IsGuideCurrent compares)
    FString GuideNormalized(const FString& text)
    {
        FString normalized = text.Replace(GUIDE_CRLF, GUIDE_LF, ESearchCase::CaseSensitive);
        int32 end = normalized.Len();
        while (end > 0 && normalized[end - 1] == GUIDE_NEWLINE)
        {
            --end;
        }
        normalized.LeftInline(end);
        return normalized;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the guide's markdown for a key array: banner, generated-file line, intro, Controls, HUD glossary
FString SOLPlayerGuide::BuildMarkdown(const TArray<FString>& keys)
{
    FString out;
    GuideLine(out, SOLMenuText::GUIDE_BANNER);
    GuideLine(out, FString::Format(SOLMenuText::GUIDE_GENERATED_FORMAT,
        { FString(GUIDE_FLAG_PREFIX) + SOL::CommandLine::WRITE_PLAYER_GUIDE, InputHash(keys) }));
    GuideBlankLine(out);
    GuideLine(out, SOLMenuText::GUIDE_TITLE);
    GuideBlankLine(out);
    GuideLine(out, SOLMenuText::GUIDE_INTRO);
    GuideBlankLine(out);
    GuideControls(out, keys);
    GuideGlossary(out, keys);
    return out;
}

//////////////////////////////////////////////////////////////////////////
// Returns a CRC-32 of the guide's inputs (every Controls row, glossary tab and entry, and the keys) as 8 hex digits
FString SOLPlayerGuide::InputHash(const TArray<FString>& keys)
{
    uint32 crc = 0;
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        crc = GuideHash(row.Area, crc);
        crc = GuideHash(row.Bindings, crc);
        crc = GuideHash(row.Action, crc);
    }
    for (const TCHAR* tab : SOLMenuText::GLOSSARY_TABS)
    {
        crc = GuideHash(tab, crc);
    }
    for (const SOLMenuText::FGlossaryEntry& entry : SOLMenuText::GLOSSARY_ENTRIES)
    {
        crc = GuideHash(entry.Tab, crc);
        crc = GuideHash(entry.Symbols, crc);
        crc = GuideHash(entry.Sample, crc);
        crc = GuideHash(entry.Name, crc);
        crc = GuideHash(entry.Description, crc);
    }

    // Each key with a separator, so moving a key between bindings changes the hash too
    for (const FString& key : keys)
    {
        crc = GuideHash(*key, crc);
        crc = GuideHash(SOLMenuText::BINDING_ALTERNATIVE_DELIMITER, crc);
    }
    return FString::Printf(GUIDE_HASH_FORMAT, crc);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a guide file matches the generated text, ignoring CRLF / LF and trailing newlines
bool SOLPlayerGuide::IsGuideCurrent(const FString& fileText, const FString& generated)
{
    return GuideNormalized(fileText).Equals(GuideNormalized(generated), ESearchCase::CaseSensitive);
}

//////////////////////////////////////////////////////////////////////////
// Returns the guide file's full path
FString SOLPlayerGuide::GuideFilePath()
{
    return FPaths::ProjectDir() + SOL::Paths::PLAYER_GUIDE_FILE;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a command line holds the -SOLWritePlayerGuide switch as a whole token
bool SOLPlayerGuide::IsWriteRequested(const TCHAR* commandLine)
{
    return commandLine != nullptr && FParse::Param(commandLine, SOL::CommandLine::WRITE_PLAYER_GUIDE);
}

//////////////////////////////////////////////////////////////////////////
// Returns the guide's screenshots in shot order: the Keyboard tab, its hover shot, then each glossary tab
TConstArrayView<FSOLPlayerGuideShot> SOLPlayerGuide::Shots()
{
    static const FSOLPlayerGuideShot SHOTS[] = {
        { ESOLHelpTab::Keyboard, false, SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD },
        { ESOLHelpTab::Keyboard, true, SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER },
        { ESOLHelpTab::FlightHud, false, SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY[0] },
        { ESOLHelpTab::TargetsRadar, false, SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY[1] },
        { ESOLHelpTab::OverlaysRoute, false, SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY[2] },
        { ESOLHelpTab::MenusMap, false, SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY[3] },
    };
    static_assert(UE_ARRAY_COUNT(SHOTS) == GUIDE_FIXED_SHOTS + UE_ARRAY_COUNT(SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY),
        "one shot per glossary tab after the two Keyboard shots");
    return SHOTS;
}

//////////////////////////////////////////////////////////////////////////
// Returns an image's full path in the repository's image folder
FString SOLPlayerGuide::ImagePath(const TCHAR* fileName)
{
    return FPaths::ProjectDir() + SOL::Paths::PLAYER_GUIDE_IMAGE_DIR + fileName;
}

//////////////////////////////////////////////////////////////////////////
// Returns an image's capture path in the screenshot folder (full)
FString SOLPlayerGuide::CapturePath(const TCHAR* fileName)
{
    return FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / (FString(SOL::Paths::PLAYER_GUIDE_CAPTURE_PREFIX) + fileName));
}

//////////////////////////////////////////////////////////////////////////
// Fills the target of every "![alt](target)" of a text, in order
void SOLPlayerGuide::ReferencedImages(const FString& markdown, TArray<FString>& outTargets)
{
    outTargets.Reset();
    const int32 length = markdown.Len();
    int32 from = 0;
    while (from < length)
    {
        // The next "![", its "](" and the closing ')' (each search starts inside the text: no start clamp)
        const int32 open = markdown.Find(GUIDE_IMAGE_OPEN, ESearchCase::CaseSensitive, ESearchDir::FromStart, from);
        if (open == INDEX_NONE)
        {
            return;
        }
        const int32 altEnd = open + GUIDE_IMAGE_OPEN_LEN < length ? markdown.Find(GUIDE_IMAGE_MIDDLE,
            ESearchCase::CaseSensitive, ESearchDir::FromStart, open + GUIDE_IMAGE_OPEN_LEN) : INDEX_NONE;
        if (altEnd == INDEX_NONE)
        {
            return;
        }
        const int32 targetStart = altEnd + GUIDE_IMAGE_MIDDLE_LEN;
        int32 close = INDEX_NONE;
        for (int32 index = targetStart; index < length; ++index)
        {
            if (markdown[index] == GUIDE_IMAGE_CLOSE)
            {
                close = index;
                break;
            }
        }
        if (close == INDEX_NONE)
        {
            return;
        }
        outTargets.Add(markdown.Mid(targetStart, close - targetStart));
        from = close + 1;
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes a text as UTF-8 without a BOM (the folder made, the file replaced); returns whether it was written
bool SOLPlayerGuide::WriteGuideFile(const FString& path, const FString& text)
{
    IFileManager& files = IFileManager::Get();
    const FString folder = FPaths::GetPath(path);
    if (!folder.IsEmpty() && !files.DirectoryExists(*folder) && !files.MakeDirectory(*folder, true))
    {
        return false;
    }
    return FFileHelper::SaveStringToFile(text, *path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &files,
        FILEWRITE_None);
}

//////////////////////////////////////////////////////////////////////////
// Returns the stale-guide message (the fix command in it)
FString SOLPlayerGuide::StaleMessage()
{
    return SOLMenuText::GUIDE_STALE_MESSAGE;
}
