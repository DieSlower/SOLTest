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
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

static_assert(UE_ARRAY_COUNT(SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY) == UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_TABS),
    "one player guide image per glossary tab");
static_assert(UE_ARRAY_COUNT(SOL::Paths::PLAYER_GUIDE_TAB_PAGES) == static_cast<int32>(ESOLHelpTab::Count),
    "one player guide page count per Help tab");
// Returns whether every Help tab's page count is in 1 .. PLAYER_GUIDE_MAX_PAGES (compile time)
constexpr bool GuidePageCountsValid()
{
    for (const int32 pages : SOL::Paths::PLAYER_GUIDE_TAB_PAGES)
    {
        if (pages < 1 || pages > SOL::Paths::PLAYER_GUIDE_MAX_PAGES)
        {
            return false;
        }
    }
    return true;
}
static_assert(GuidePageCountsValid(), "each Help tab has 1 .. PLAYER_GUIDE_MAX_PAGES guide pages");

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
    constexpr TCHAR GUIDE_EXTENSION_DOT = TEXT('.');                // Before an image name's extension
    constexpr TCHAR GUIDE_PAGE_SUFFIX_FORMAT[] = TEXT("-%d");       // A scrolled shot's name suffix (Printf: an array)

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
    // Appends a tab's page images (D46), each on its own line with a blank line after: the first with its alt text,
    // the scrolled ones with GUIDE_IMAGE_PART_FORMAT
    void GuidePageImages(FString& out, const ESOLHelpTab tab, const TCHAR* alt, const TCHAR* baseName)
    {
        const int32 pages = SOLPlayerGuide::TabPages(tab);
        for (int32 page = 0; page < pages; ++page)
        {
            const FString pageAlt = page == 0 ? FString(alt)
                : FString::Format(SOLMenuText::GUIDE_IMAGE_PART_FORMAT, { FString(alt), page + 1, pages });
            GuideImage(out, *pageAlt, *SOLPlayerGuide::PageFileName(baseName, page));
            GuideBlankLine(out);
        }
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
        GuidePageImages(out, ESOLHelpTab::Keyboard, SOLMenuText::GUIDE_KEYBOARD_ALT,
            SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD);
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
            GuidePageImages(out, SOLGlossary::TabFromGlossaryName(heading), heading,
                SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY[tab]);
            GuideLine(out, SOLMenuText::GUIDE_GLOSSARY_COLUMNS);
            GuideLine(out, SOLMenuText::GUIDE_TABLE_RULE);
            SOLGlossary::EntriesOfTab(SOLGlossary::TabFromGlossaryName(heading), entries);
            for (const int32 index : entries)
            {
                const SOLMenuText::FGlossaryEntry& entry = SOLMenuText::GLOSSARY_ENTRIES[index];
                SOLGlossary::FormatEntryText(entry.Description, keys, description);
                FString nameCell = GuideCell(entry.Name);

                // A map row's screenshot crop sits under its name (33e-ii, D39)
                const int32 crop = SOLGlossary::EntryCrop(index);
                if (crop != INDEX_NONE)
                {
                    nameCell = FString::Format(SOLMenuText::GUIDE_CROP_CELL_FORMAT, { nameCell,
                        FString::Format(SOLMenuText::GUIDE_IMAGE_FORMAT, { FString(entry.Name),
                            GuideImageDir() + SOL::Paths::GLOSSARY_CROPS[crop].ImageFile }) });
                }
                GuideLine(out, FString::Printf(SOLMenuText::GUIDE_TABLE_ROW_FORMAT, *nameCell,
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

    //////////////////////////////////////////////////////////////////////////
    // Returns the scroll step between two guide pages for a view height (the overlap off, at least 1)
    float GuidePageStep(const float viewHeight)
    {
        return FMath::Max(viewHeight - SOL::Paths::PLAYER_GUIDE_PAGE_OVERLAP, 1.0f);
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
        crc = GuideHash(entry.Crop, crc);
    }

    for (const SOL::Paths::FGlossaryCrop& crop : SOL::Paths::GLOSSARY_CROPS)
    {
        crc = GuideHash(crop.Id, crc);
        crc = GuideHash(crop.ImageFile, crc);
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
// Returns whether the command line asks for the crop run
bool SOLPlayerGuide::IsCropCaptureRequested(const TCHAR* commandLine)
{
    return commandLine != nullptr && FParse::Param(commandLine, SOL::CommandLine::WRITE_GLOSSARY_CROPS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the guide's screenshots in shot order (D46): the Keyboard tab's pages, its hover shot, then each glossary
// tab's pages; built once from SOL::Paths' page table
TConstArrayView<FSOLPlayerGuideShot> SOLPlayerGuide::Shots()
{
    static const TArray<FSOLPlayerGuideShot> SHOTS = []()
    {
        TArray<FSOLPlayerGuideShot> shots;
        // Adds a tab's page shots (plain, no hover), named from its base image name
        const auto addPages = [&shots](const ESOLHelpTab tab, const TCHAR* baseName)
        {
            for (int32 page = 0; page < TabPages(tab); ++page)
            {
                shots.Add({ tab, false, page, PageFileName(baseName, page) });
            }
        };
        addPages(ESOLHelpTab::Keyboard, SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD);
        shots.Add({ ESOLHelpTab::Keyboard, true, 0, SOL::Paths::PLAYER_GUIDE_IMAGE_KEYBOARD_HOVER });
        for (int32 tab = 0; tab < static_cast<int32>(UE_ARRAY_COUNT(SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY)); ++tab)
        {
            addPages(SOLGlossary::TabFromGlossaryName(SOLMenuText::GLOSSARY_TABS[tab]),
                SOL::Paths::PLAYER_GUIDE_IMAGE_GLOSSARY[tab]);
        }
        return shots;
    }();
    return SHOTS;
}

//////////////////////////////////////////////////////////////////////////
// D46: returns a Help tab's shot count from the page table
int32 SOLPlayerGuide::TabPages(const ESOLHelpTab tab)
{
    const int32 index = static_cast<int32>(tab);
    return index >= 0 && index < static_cast<int32>(UE_ARRAY_COUNT(SOL::Paths::PLAYER_GUIDE_TAB_PAGES))
        ? SOL::Paths::PLAYER_GUIDE_TAB_PAGES[index] : 0;
}


//////////////////////////////////////////////////////////////////////////
// D46: returns the window shots a tab's scroll extent needs
int32 SOLPlayerGuide::PageCount(const float viewHeight, const float scrollEnd)
{
    using namespace SOL::Paths;
    if (!FMath::IsFinite(viewHeight) || viewHeight <= 0.0f || !FMath::IsFinite(scrollEnd)
        || scrollEnd <= PLAYER_GUIDE_SCROLL_TOLERANCE)
    {
        return 1;
    }
    const float steps = FMath::CeilToFloat((scrollEnd - PLAYER_GUIDE_SCROLL_TOLERANCE) / GuidePageStep(viewHeight));
    return steps >= static_cast<float>(PLAYER_GUIDE_MAX_PAGES - 1) ? PLAYER_GUIDE_MAX_PAGES
        : 1 + static_cast<int32>(steps);
}

//////////////////////////////////////////////////////////////////////////
// D46: returns a page's scroll offset
float SOLPlayerGuide::PageOffset(const int32 page, const float viewHeight, const float scrollEnd)
{
    if (page <= 0 || !FMath::IsFinite(viewHeight) || viewHeight <= 0.0f || !FMath::IsFinite(scrollEnd)
        || scrollEnd <= 0.0f)
    {
        return 0.0f;
    }
    return FMath::Min(static_cast<float>(page) * GuidePageStep(viewHeight), scrollEnd);
}

//////////////////////////////////////////////////////////////////////////
// D46: returns a page's image name
FString SOLPlayerGuide::PageFileName(const TCHAR* baseName, const int32 page)
{
    if (baseName == nullptr)
    {
        return FString();
    }
    FString name(baseName);
    if (page <= 0)
    {
        return name;
    }
    const FString suffix = FString::Printf(GUIDE_PAGE_SUFFIX_FORMAT, page + 1);
    int32 dot = INDEX_NONE;
    if (name.FindLastChar(GUIDE_EXTENSION_DOT, dot))
    {
        name.InsertAt(dot, suffix);
    }
    else
    {
        name += suffix;
    }
    return name;
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

//////////////////////////////////////////////////////////////////////////
// Compares a fresh image with an old one: per fresh pixel the closest old colour within the shift (an equal pixel in
// place needs no search), its largest channel difference counted
FSOLImageDiff SOLPlayerGuide::CompareImages(const FImage& fresh, const FImage& old, const FSOLImageNoise& noise)
{
    FSOLImageDiff diff;
    if (fresh.Format != ERawImageFormat::BGRA8 || old.Format != ERawImageFormat::BGRA8 || fresh.SizeX <= 0
        || fresh.SizeY <= 0 || fresh.SizeX != old.SizeX || fresh.SizeY != old.SizeY
        || fresh.NumSlices < 1 || fresh.NumSlices != old.NumSlices
        || fresh.RawData.Num() < static_cast<int64>(fresh.SizeX) * fresh.SizeY * static_cast<int64>(sizeof(FColor))
        || old.RawData.Num() < static_cast<int64>(old.SizeX) * old.SizeY * static_cast<int64>(sizeof(FColor)))
    {
        return diff;
    }
    diff.bComparable = true;
    const int32 width = fresh.SizeX;
    const int32 height = fresh.SizeY;
    const int32 shift = FMath::Clamp(noise.ShiftPx, 0, FMath::Max(width, height));
    const int32 noiseDelta = FMath::Max(noise.NoiseDelta, 0);
    const FColor* freshPixels = reinterpret_cast<const FColor*>(fresh.RawData.GetData());
    const FColor* oldPixels = reinterpret_cast<const FColor*>(old.RawData.GetData());
    // The largest of the R, G and B differences of two pixels
    const auto channelDelta = [](const FColor& a, const FColor& b)
    {
        return FMath::Max3(FMath::Abs(a.R - b.R), FMath::Abs(a.G - b.G), FMath::Abs(a.B - b.B));
    };
    for (int32 y = 0; y < height; ++y)
    {
        for (int32 x = 0; x < width; ++x)
        {
            const FColor& b = freshPixels[static_cast<int64>(y) * width + x];
            int32 delta = channelDelta(oldPixels[static_cast<int64>(y) * width + x], b);
            if (delta == 0)
            {
                continue;
            }

            // The closest old pixel in the neighbourhood (clamped to the image), stopping at an exact match
            const int32 yMax = FMath::Min(y + shift, height - 1);
            const int32 xMax = FMath::Min(x + shift, width - 1);
            for (int32 oy = FMath::Max(y - shift, 0); oy <= yMax && delta > 0; ++oy)
            {
                const FColor* row = oldPixels + static_cast<int64>(oy) * width;
                for (int32 ox = FMath::Max(x - shift, 0); ox <= xMax && delta > 0; ++ox)
                {
                    delta = FMath::Min(delta, channelDelta(row[ox], b));
                }
            }
            diff.MaxDelta = FMath::Max(diff.MaxDelta, delta);
            diff.NoisyPixels += delta > noiseDelta ? 1 : 0;
        }
    }
    return diff;
}

//////////////////////////////////////////////////////////////////////////
// Loads the image at a path as BGRA8 (sRGB) and compares a fresh image with it; not comparable without a loaded file
FSOLImageDiff SOLPlayerGuide::CompareWithFile(const FImage& fresh, const FString& path, const FSOLImageNoise& noise)
{
    FImage old;
    if (!IFileManager::Get().FileExists(*path) || !FImageUtils::LoadImage(*path, old))
    {
        return FSOLImageDiff();
    }
    old.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    return CompareImages(fresh, old, noise);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a difference is capture noise (the old file kept)
bool SOLPlayerGuide::IsCaptureNoise(const FSOLImageDiff& diff, const FSOLImageNoise& noise)
{
    return diff.bComparable && diff.MaxDelta <= noise.KeepMaxDelta && diff.NoisyPixels <= noise.KeepMaxNoisyPixels;
}

//////////////////////////////////////////////////////////////////////////
// Returns the map glossary crops' capture noise
FSOLImageNoise SOLPlayerGuide::CropNoise()
{
    return { SOL::Help::GLOSSARY_CROP_NOISE_SHIFT_PX, SOL::Help::GLOSSARY_CROP_NOISE_DELTA,
        SOL::Help::GLOSSARY_CROP_KEEP_MAX_DELTA, SOL::Help::GLOSSARY_CROP_KEEP_MAX_NOISY_PIXELS };
}

//////////////////////////////////////////////////////////////////////////
// Returns the Help screenshots' capture noise
FSOLImageNoise SOLPlayerGuide::HelpShotNoise()
{
    return { SOL::Help::GUIDE_SHOT_NOISE_SHIFT_PX, SOL::Help::GUIDE_SHOT_NOISE_DELTA,
        SOL::Help::GUIDE_SHOT_KEEP_MAX_DELTA, SOL::Help::GUIDE_SHOT_KEEP_MAX_NOISY_PIXELS };
}
