/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLPlayerGuide.h"
#include "SOLConstants.h"

#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33e-iv keep-if-unchanged image compare (Docs/SDDs/33-player-documentation.md section
// 9), written test-first against the header contract (Menu/SOLPlayerGuide.h: FSOLImageDiff, FSOLImageNoise,
// CompareImages, CompareWithFile, IsCaptureNoise, CropNoise, HelpShotNoise) before the production code existed. The
// glossary crop writer and the Help screenshot writer share it: a fresh capture that differs from the file on disk
// only by capture noise keeps the old file byte for byte, anything beyond it replaces the file.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr int32 PLAYERGUIDEIMAGEDIFFTEST_SIZE = 16;     // Side of most test images (pixels)
    constexpr int32 PLAYERGUIDEIMAGEDIFFTEST_BIG = 64;      // Side of the deliberate-change image (pixels)
    constexpr int32 PLAYERGUIDEIMAGEDIFFTEST_BLOCK = 16;    // Side of the deliberate change's block (pixels)
    constexpr int32 PLAYERGUIDEIMAGEDIFFTEST_HUGE = TNumericLimits<int32>::Max();   // Keep limits nothing exceeds

    // Background colours (FColor's constructor takes R, G, B, A; memory order is B, G, R, A)
    const FColor PLAYERGUIDEIMAGEDIFFTEST_GREY(100, 100, 100, 255);
    const FColor PLAYERGUIDEIMAGEDIFFTEST_BLACK(0, 0, 0, 255);
    const FColor PLAYERGUIDEIMAGEDIFFTEST_WHITE(255, 255, 255, 255);

    //////////////////////////////////////////////////////////////////////////
    // Returns a BGRA8 sRGB image of a size filled with one colour
    FImage PlayerGuideImageDiffTestMake(const int32 width, const int32 height, const FColor& fill)
    {
        FImage image;
        image.Init(width, height, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
        for (FColor& pixel : image.AsBGRA8())
        {
            pixel = fill;
        }
        return image;
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets one pixel of a BGRA8 image
    void PlayerGuideImageDiffTestSet(FImage& image, const int32 x, const int32 y, const FColor& color)
    {
        image.AsBGRA8()[static_cast<int64>(y) * image.SizeX + x] = color;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one pixel of a BGRA8 image
    FColor PlayerGuideImageDiffTestGet(const FImage& image, const int32 x, const int32 y)
    {
        return image.AsBGRA8()[static_cast<int64>(y) * image.SizeX + x];
    }

    //////////////////////////////////////////////////////////////////////////
    // Draws a one-pixel-wide vertical line over the whole height of a BGRA8 image
    void PlayerGuideImageDiffTestLine(FImage& image, const int32 x, const FColor& color)
    {
        for (int32 y = 0; y < image.SizeY; ++y)
        {
            PlayerGuideImageDiffTestSet(image, x, y, color);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an opaque BGRA8 image whose pixels all differ from their neighbours (no channel near 0 or 255)
    FImage PlayerGuideImageDiffTestGradient(const int32 width, const int32 height)
    {
        FImage image = PlayerGuideImageDiffTestMake(width, height, PLAYERGUIDEIMAGEDIFFTEST_GREY);
        for (int32 y = 0; y < height; ++y)
        {
            for (int32 x = 0; x < width; ++x)
            {
                PlayerGuideImageDiffTestSet(image, x, y, FColor(static_cast<uint8>(20 + x * 8),
                    static_cast<uint8>(50 + y * 4), static_cast<uint8>(30 + (x + y) * 3), 255));
            }
        }
        return image;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns noise settings from their four fields
    FSOLImageNoise PlayerGuideImageDiffTestNoise(const int32 shiftPx, const int32 noiseDelta, const int32 keepMaxDelta,
        const int32 keepMaxNoisyPixels)
    {
        FSOLImageNoise noise;
        noise.ShiftPx = shiftPx;
        noise.NoiseDelta = noiseDelta;
        noise.KeepMaxDelta = keepMaxDelta;
        noise.KeepMaxNoisyPixels = keepMaxNoisyPixels;
        return noise;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a difference from its three fields
    FSOLImageDiff PlayerGuideImageDiffTestDiff(const bool bComparable, const int32 maxDelta, const int32 noisyPixels)
    {
        FSOLImageDiff diff;
        diff.bComparable = bComparable;
        diff.MaxDelta = maxDelta;
        diff.NoisyPixels = noisyPixels;
        return diff;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks every field of a difference against the expected values, naming the case in each message
    void PlayerGuideImageDiffTestExpect(FAutomationTestBase& test, const TCHAR* what, const FSOLImageDiff& diff,
        const bool bComparable, const int32 maxDelta, const int32 noisyPixels)
    {
        test.TestTrue(FString::Printf(TEXT("%s: bComparable is %s"), what, bComparable ? TEXT("true") : TEXT("false")),
            diff.bComparable == bComparable);
        test.TestEqual(FString::Printf(TEXT("%s: MaxDelta"), what), diff.MaxDelta, maxDelta);
        test.TestEqual(FString::Printf(TEXT("%s: NoisyPixels"), what), diff.NoisyPixels, noisyPixels);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a unique temporary PNG path under Saved/Automation for this test file
    FString PlayerGuideImageDiffTestTempPath(const TCHAR* tag)
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), FString::Printf(
            TEXT("PlayerGuideImageDiff_%s_%s.png"), tag, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Deletes a temporary file and checks it is gone
    void PlayerGuideImageDiffTestDelete(FAutomationTestBase& test, const FString& path)
    {
        IFileManager::Get().Delete(*path, /*requireExists*/ false, /*evenReadOnly*/ true, /*quiet*/ true);
        test.TestFalse(FString::Printf(TEXT("The temporary file %s is deleted"), *path),
            IFileManager::Get().FileExists(*path));
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffIdenticalTest, "SOLTest.PlayerGuide.ImageDiff.Identical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Identical images are comparable with no difference, so they are capture noise at any non-negative settings
bool FSOLPlayerGuideImageDiffIdenticalTest::RunTest(const FString& /*parameters*/)
{
    const FImage old = PlayerGuideImageDiffTestGradient(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE);
    const FImage fresh = old;
    const FSOLImageNoise loose = PlayerGuideImageDiffTestNoise(1, 2, 128, 16);
    const FSOLImageNoise strict = PlayerGuideImageDiffTestNoise(0, 0, 0, 0);

    const FSOLImageDiff looseDiff = SOLPlayerGuide::CompareImages(fresh, old, loose);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Identical, shift 1"), looseDiff, true, 0, 0);
    TestTrue(TEXT("Identical images are capture noise at {1, 2, 128, 16}"),
        SOLPlayerGuide::IsCaptureNoise(looseDiff, loose));

    const FSOLImageDiff strictDiff = SOLPlayerGuide::CompareImages(fresh, old, strict);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Identical, shift 0"), strictDiff, true, 0, 0);
    TestTrue(TEXT("Identical images are capture noise at {0, 0, 0, 0} (a difference of 0 is not above 0)"),
        SOLPlayerGuide::IsCaptureNoise(strictDiff, strict));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffNotComparableTest,
    "SOLTest.PlayerGuide.ImageDiff.NotComparable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Images of different sizes, a non-BGRA8 format or an empty image on either side are not comparable (all zero), and
// never capture noise, however loose the keep limits
bool FSOLPlayerGuideImageDiffNotComparableTest::RunTest(const FString& /*parameters*/)
{
    const FSOLImageNoise loose = PlayerGuideImageDiffTestNoise(1, 0, PLAYERGUIDEIMAGEDIFFTEST_HUGE,
        PLAYERGUIDEIMAGEDIFFTEST_HUGE);
    const FImage base = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE,
        PLAYERGUIDEIMAGEDIFFTEST_GREY);
    const FImage wider = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_SIZE + 1, PLAYERGUIDEIMAGEDIFFTEST_SIZE,
        PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    const FImage taller = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE + 1,
        PLAYERGUIDEIMAGEDIFFTEST_WHITE);

    // The same size in other formats (float formats must be linear for a valid FImage)
    FImage floatImage;
    floatImage.Init(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE, ERawImageFormat::RGBA32F,
        EGammaSpace::Linear);
    FMemory::Memzero(floatImage.RawData.GetData(), floatImage.RawData.Num());
    FImage greyImage;
    greyImage.Init(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE, ERawImageFormat::G8,
        EGammaSpace::Linear);
    FMemory::Memzero(greyImage.RawData.GetData(), greyImage.RawData.Num());

    // Empty images: default-constructed and initialised at 0 x 0
    const FImage defaultImage;
    FImage zeroImage;
    zeroImage.Init(0, 0, ERawImageFormat::BGRA8, EGammaSpace::sRGB);

    // Checks one pair both ways round: not comparable, all zero, not capture noise
    auto expectNotComparable = [this, &loose](const TCHAR* what, const FImage& first, const FImage& second)
    {
        const FSOLImageDiff forward = SOLPlayerGuide::CompareImages(first, second, loose);
        PlayerGuideImageDiffTestExpect(*this, *FString::Printf(TEXT("%s (fresh first)"), what), forward, false, 0, 0);
        TestFalse(FString::Printf(TEXT("%s (fresh first) is not capture noise"), what),
            SOLPlayerGuide::IsCaptureNoise(forward, loose));
        const FSOLImageDiff backward = SOLPlayerGuide::CompareImages(second, first, loose);
        PlayerGuideImageDiffTestExpect(*this, *FString::Printf(TEXT("%s (old first)"), what), backward, false, 0, 0);
        TestFalse(FString::Printf(TEXT("%s (old first) is not capture noise"), what),
            SOLPlayerGuide::IsCaptureNoise(backward, loose));
    };

    expectNotComparable(TEXT("A wider image"), base, wider);
    expectNotComparable(TEXT("A taller image"), base, taller);
    expectNotComparable(TEXT("An RGBA32F image"), base, floatImage);
    expectNotComparable(TEXT("A G8 image"), base, greyImage);
    expectNotComparable(TEXT("A default-constructed image"), base, defaultImage);
    expectNotComparable(TEXT("A 0 x 0 BGRA8 image"), base, zeroImage);
    expectNotComparable(TEXT("Two 0 x 0 BGRA8 images"), zeroImage, zeroImage);
    expectNotComparable(TEXT("Two RGBA32F images of the same size"), floatImage, floatImage);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffAlphaTest, "SOLTest.PlayerGuide.ImageDiff.AlphaIgnored",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Images that differ only in alpha have no difference: only the colour channels count
bool FSOLPlayerGuideImageDiffAlphaTest::RunTest(const FString& /*parameters*/)
{
    const FImage old = PlayerGuideImageDiffTestGradient(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE);
    FImage fresh = old;
    for (FColor& pixel : fresh.AsBGRA8())
    {
        pixel.A = 0;
    }
    const FSOLImageNoise strict = PlayerGuideImageDiffTestNoise(0, 0, 0, 0);
    const FSOLImageDiff diff = SOLPlayerGuide::CompareImages(fresh, old, strict);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Alpha 0 against alpha 255"), diff, true, 0, 0);
    TestTrue(TEXT("An alpha-only change is capture noise even at {0, 0, 0, 0}"),
        SOLPlayerGuide::IsCaptureNoise(diff, strict));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffChannelTest, "SOLTest.PlayerGuide.ImageDiff.LargestChannel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A pixel's difference is its largest absolute R, G or B difference, whichever image is brighter
bool FSOLPlayerGuideImageDiffChannelTest::RunTest(const FString& /*parameters*/)
{
    const FImage plain = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE,
        PLAYERGUIDEIMAGEDIFFTEST_GREY);
    FImage changed = plain;
    // R + 10, G + 30, B + 5 on one pixel
    PlayerGuideImageDiffTestSet(changed, 4, 7, FColor(110, 130, 105, 255));
    const FSOLImageNoise noise = PlayerGuideImageDiffTestNoise(0, 10, 255, 1000);

    PlayerGuideImageDiffTestExpect(*this, TEXT("Fresh brighter (R+10, G+30, B+5)"),
        SOLPlayerGuide::CompareImages(changed, plain, noise), true, 30, 1);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Fresh darker (R-10, G-30, B-5)"),
        SOLPlayerGuide::CompareImages(plain, changed, noise), true, 30, 1);

    // The largest channel wins in each position: blue alone, red alone
    FImage blueChanged = plain;
    PlayerGuideImageDiffTestSet(blueChanged, 0, 0, FColor(101, 98, 140, 255));
    PlayerGuideImageDiffTestExpect(*this, TEXT("Blue the largest (R+1, G-2, B+40)"),
        SOLPlayerGuide::CompareImages(blueChanged, plain, noise), true, 40, 1);
    FImage redChanged = plain;
    PlayerGuideImageDiffTestSet(redChanged, 15, 15, FColor(45, 99, 112, 255));
    PlayerGuideImageDiffTestExpect(*this, TEXT("Red the largest (R-55, G-1, B+12)"),
        SOLPlayerGuide::CompareImages(redChanged, plain, noise), true, 55, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffThresholdTest,
    "SOLTest.PlayerGuide.ImageDiff.NoiseDeltaBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A pixel counts as noisy only above NoiseDelta: exactly NoiseDelta is not noisy, NoiseDelta + 1 is; MaxDelta is
// reported either way
bool FSOLPlayerGuideImageDiffThresholdTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 noiseDelta = 7;
    const FImage old = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE,
        PLAYERGUIDEIMAGEDIFFTEST_GREY);
    const FSOLImageNoise noise = PlayerGuideImageDiffTestNoise(0, noiseDelta, 255, 1000);

    FImage atDelta = old;
    PlayerGuideImageDiffTestSet(atDelta, 3, 3, FColor(100, 100 + noiseDelta, 100, 255));
    PlayerGuideImageDiffTestExpect(*this, TEXT("A pixel off by exactly NoiseDelta"),
        SOLPlayerGuide::CompareImages(atDelta, old, noise), true, noiseDelta, 0);

    FImage aboveDelta = old;
    PlayerGuideImageDiffTestSet(aboveDelta, 3, 3, FColor(100, 100 + noiseDelta + 1, 100, 255));
    PlayerGuideImageDiffTestExpect(*this, TEXT("A pixel off by NoiseDelta + 1"),
        SOLPlayerGuide::CompareImages(aboveDelta, old, noise), true, noiseDelta + 1, 1);

    // Two pixels: one at NoiseDelta, one above it; only the second is noisy, MaxDelta is the larger
    FImage both = atDelta;
    PlayerGuideImageDiffTestSet(both, 10, 12, FColor(100, 100, 100 - noiseDelta - 1, 255));
    PlayerGuideImageDiffTestExpect(*this, TEXT("One pixel at NoiseDelta and one above it"),
        SOLPlayerGuide::CompareImages(both, old, noise), true, noiseDelta + 1, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffShiftTest, "SOLTest.PlayerGuide.ImageDiff.Shift",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A crisp line moved by one pixel is noisy at full contrast without a shift and no difference with ShiftPx 1; moved by
// two pixels it stays noisy with ShiftPx 1; a negative shift reads as 0. Lines in both directions
bool FSOLPlayerGuideImageDiffShiftTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 side = PLAYERGUIDEIMAGEDIFFTEST_SIZE;
    constexpr int32 oldX = 5;
    const FImage black = PlayerGuideImageDiffTestMake(side, side, PLAYERGUIDEIMAGEDIFFTEST_BLACK);
    FImage old = black;
    PlayerGuideImageDiffTestLine(old, oldX, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    FImage movedOne = black;
    PlayerGuideImageDiffTestLine(movedOne, oldX + 1, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    FImage movedTwo = black;
    PlayerGuideImageDiffTestLine(movedTwo, oldX + 2, PLAYERGUIDEIMAGEDIFFTEST_WHITE);

    // Without a shift: the fresh line's pixels (white on old black) and the old line's place (black on old white)
    const FSOLImageNoise noShift = PlayerGuideImageDiffTestNoise(0, 2, 255, 10000);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Line moved 1 px, ShiftPx 0"),
        SOLPlayerGuide::CompareImages(movedOne, old, noShift), true, 255, 2 * side);

    // ShiftPx 1: every fresh pixel finds its colour in old within 1 px
    const FSOLImageNoise shiftOne = PlayerGuideImageDiffTestNoise(1, 2, 255, 10000);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Line moved 1 px, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(movedOne, old, shiftOne), true, 0, 0);

    // Moved by 2 px: the fresh line (x = 7) finds no white in old within x 6..8; the old line's place (x = 5) finds
    // black at x = 4 or 6
    PlayerGuideImageDiffTestExpect(*this, TEXT("Line moved 2 px, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(movedTwo, old, shiftOne), true, 255, side);

    // A negative shift reads 0
    const FSOLImageNoise negativeShift = PlayerGuideImageDiffTestNoise(-1, 2, 255, 10000);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Line moved 1 px, ShiftPx -1"),
        SOLPlayerGuide::CompareImages(movedOne, old, negativeShift), true, 255, 2 * side);

    // The same with a horizontal line moved by one row (the shift is a square: x and y)
    FImage oldRow = black;
    FImage movedRow = black;
    for (int32 x = 0; x < side; ++x)
    {
        PlayerGuideImageDiffTestSet(oldRow, x, oldX, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
        PlayerGuideImageDiffTestSet(movedRow, x, oldX - 1, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    }
    PlayerGuideImageDiffTestExpect(*this, TEXT("Row moved 1 px, ShiftPx 0"),
        SOLPlayerGuide::CompareImages(movedRow, oldRow, noShift), true, 255, 2 * side);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Row moved 1 px, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(movedRow, oldRow, shiftOne), true, 0, 0);

    // A single bright pixel moved diagonally by one pixel is within the square of ShiftPx 1
    FImage oldDot = black;
    PlayerGuideImageDiffTestSet(oldDot, 8, 8, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    FImage movedDot = black;
    PlayerGuideImageDiffTestSet(movedDot, 9, 9, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Dot moved diagonally 1 px, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(movedDot, oldDot, shiftOne), true, 0, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffBorderTest, "SOLTest.PlayerGuide.ImageDiff.BorderClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shift's neighbourhood is clamped to the image: corner pixels are measured against the in-bounds old pixels only,
// and a shift wider than the image reaches every old pixel
bool FSOLPlayerGuideImageDiffBorderTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 side = 8;
    const FImage old = PlayerGuideImageDiffTestMake(side, side, PLAYERGUIDEIMAGEDIFFTEST_GREY);
    FImage fresh = old;
    // Top-left: (200, 10, 50) against grey 100 -> 100; bottom-right: (160, 100, 100) -> 60
    PlayerGuideImageDiffTestSet(fresh, 0, 0, FColor(200, 10, 50, 255));
    PlayerGuideImageDiffTestSet(fresh, side - 1, side - 1, FColor(160, 100, 100, 255));
    // Top-right and bottom-left: off by 1, below NoiseDelta
    PlayerGuideImageDiffTestSet(fresh, side - 1, 0, FColor(101, 100, 100, 255));
    PlayerGuideImageDiffTestSet(fresh, 0, side - 1, FColor(100, 99, 100, 255));

    const FSOLImageNoise shiftOne = PlayerGuideImageDiffTestNoise(1, 2, 255, 1000);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Unique corner colours, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(fresh, old, shiftOne), true, 100, 2);
    const FSOLImageNoise shiftTwo = PlayerGuideImageDiffTestNoise(2, 2, 255, 1000);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Unique corner colours, ShiftPx 2"),
        SOLPlayerGuide::CompareImages(fresh, old, shiftTwo), true, 100, 2);

    // The old image holds the corner colour two pixels in from the corner: out of reach for ShiftPx 1, found with 2
    FImage oldWithColour = old;
    PlayerGuideImageDiffTestSet(oldWithColour, 2, 2, FColor(200, 10, 50, 255));
    FImage freshCorner = old;
    PlayerGuideImageDiffTestSet(freshCorner, 0, 0, FColor(200, 10, 50, 255));
    PlayerGuideImageDiffTestExpect(*this, TEXT("Corner colour 2 px away in old, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(freshCorner, oldWithColour, shiftOne), true, 100, 1);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Corner colour 2 px away in old, ShiftPx 2"),
        SOLPlayerGuide::CompareImages(freshCorner, oldWithColour, shiftTwo), true, 0, 0);

    // A shift far wider than the image: the opposite corner's colour is in reach
    FImage oldOpposite = old;
    PlayerGuideImageDiffTestSet(oldOpposite, side - 1, side - 1, FColor(200, 10, 50, 255));
    const FSOLImageNoise shiftHuge = PlayerGuideImageDiffTestNoise(100, 2, 255, 1000);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Corner colour at the opposite corner, ShiftPx 100"),
        SOLPlayerGuide::CompareImages(freshCorner, oldOpposite, shiftHuge), true, 0, 0);

    // A 1 x 1 image at a shift: only its one pixel is in reach
    const FImage oneOld = PlayerGuideImageDiffTestMake(1, 1, PLAYERGUIDEIMAGEDIFFTEST_GREY);
    const FImage oneFresh = PlayerGuideImageDiffTestMake(1, 1, FColor(100, 125, 100, 255));
    PlayerGuideImageDiffTestExpect(*this, TEXT("1 x 1 images, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(oneFresh, oneOld, shiftOne), true, 25, 1);
    TestEqual(TEXT("The fresh image is not changed by the compare"), PlayerGuideImageDiffTestGet(oneFresh, 0, 0),
        FColor(100, 125, 100, 255));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffDirectionTest, "SOLTest.PlayerGuide.ImageDiff.FreshToOld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The difference is measured from each fresh pixel to the old neighbourhood, so the compare is not symmetric: a line
// that disappears is no difference with ShiftPx 1 (each fresh background pixel finds background in old within 1 px),
// a line that appears is
bool FSOLPlayerGuideImageDiffDirectionTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 side = PLAYERGUIDEIMAGEDIFFTEST_SIZE;
    const FImage plain = PlayerGuideImageDiffTestMake(side, side, PLAYERGUIDEIMAGEDIFFTEST_BLACK);
    FImage lined = plain;
    PlayerGuideImageDiffTestLine(lined, 5, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    const FSOLImageNoise shiftOne = PlayerGuideImageDiffTestNoise(1, 2, 255, 10000);

    PlayerGuideImageDiffTestExpect(*this, TEXT("Old has a 1 px line, fresh is plain, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(plain, lined, shiftOne), true, 0, 0);
    PlayerGuideImageDiffTestExpect(*this, TEXT("Old is plain, fresh has a 1 px line, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(lined, plain, shiftOne), true, 255, side);

    // A line 3 px wide that disappears: the middle column's fresh pixels find only white in old within 1 px
    FImage thick = plain;
    for (int32 x = 4; x <= 6; ++x)
    {
        PlayerGuideImageDiffTestLine(thick, x, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    }
    PlayerGuideImageDiffTestExpect(*this, TEXT("Old has a 3 px line, fresh is plain, ShiftPx 1"),
        SOLPlayerGuide::CompareImages(plain, thick, shiftOne), true, 255, side);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffKeepTest, "SOLTest.PlayerGuide.ImageDiff.KeepLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// IsCaptureNoise keeps a comparable difference at MaxDelta up to KeepMaxDelta and NoisyPixels up to
// KeepMaxNoisyPixels, both inclusive; one more of either, or a difference that is not comparable, replaces the file
bool FSOLPlayerGuideImageDiffKeepTest::RunTest(const FString& /*parameters*/)
{
    using SOLPlayerGuide::IsCaptureNoise;
    const FSOLImageNoise noise = PlayerGuideImageDiffTestNoise(1, 2, 10, 5);

    TestTrue(TEXT("No difference is kept"), IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 0, 0), noise));
    TestTrue(TEXT("MaxDelta == KeepMaxDelta and NoisyPixels == KeepMaxNoisyPixels is kept"),
        IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 10, 5), noise));
    TestFalse(TEXT("MaxDelta == KeepMaxDelta + 1 is replaced"),
        IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 11, 5), noise));
    TestFalse(TEXT("NoisyPixels == KeepMaxNoisyPixels + 1 is replaced"),
        IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 10, 6), noise));
    TestFalse(TEXT("Both over the limits is replaced"),
        IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 255, 1000), noise));
    TestFalse(TEXT("A difference that is not comparable is replaced"),
        IsCaptureNoise(PlayerGuideImageDiffTestDiff(false, 0, 0), noise));

    // Zero limits keep only an exact match
    const FSOLImageNoise strict = PlayerGuideImageDiffTestNoise(0, 0, 0, 0);
    TestTrue(TEXT("Zero limits keep no difference"), IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 0, 0), strict));
    TestFalse(TEXT("Zero limits replace MaxDelta 1"), IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 1, 0), strict));
    TestFalse(TEXT("Zero limits replace one noisy pixel"),
        IsCaptureNoise(PlayerGuideImageDiffTestDiff(true, 0, 1), strict));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffDecisionTest, "SOLTest.PlayerGuide.ImageDiff.KeepOrReplace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the writers' real settings, a deliberate change (a 16 x 16 block of a new colour) replaces the file and a few
// faint single-pixel specks within CropNoise's limits keep it
bool FSOLPlayerGuideImageDiffDecisionTest::RunTest(const FString& /*parameters*/)
{
    const FSOLImageNoise crop = SOLPlayerGuide::CropNoise();
    const FSOLImageNoise help = SOLPlayerGuide::HelpShotNoise();
    const FImage old = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_BIG, PLAYERGUIDEIMAGEDIFFTEST_BIG,
        PLAYERGUIDEIMAGEDIFFTEST_BLACK);

    // A white block in the middle of a black image
    FImage block = old;
    constexpr int32 blockStart = (PLAYERGUIDEIMAGEDIFFTEST_BIG - PLAYERGUIDEIMAGEDIFFTEST_BLOCK) / 2;
    for (int32 y = blockStart; y < blockStart + PLAYERGUIDEIMAGEDIFFTEST_BLOCK; ++y)
    {
        for (int32 x = blockStart; x < blockStart + PLAYERGUIDEIMAGEDIFFTEST_BLOCK; ++x)
        {
            PlayerGuideImageDiffTestSet(block, x, y, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
        }
    }
    const FSOLImageDiff cropBlock = SOLPlayerGuide::CompareImages(block, old, crop);
    TestTrue(TEXT("The block compares at CropNoise"), cropBlock.bComparable);
    TestFalse(TEXT("A 16 x 16 block of a new colour is not capture noise at CropNoise"),
        SOLPlayerGuide::IsCaptureNoise(cropBlock, crop));
    const FSOLImageDiff helpBlock = SOLPlayerGuide::CompareImages(block, old, help);
    TestTrue(TEXT("The block compares at HelpShotNoise"), helpBlock.bComparable);
    TestFalse(TEXT("A 16 x 16 block of a new colour is not capture noise at HelpShotNoise"),
        SOLPlayerGuide::IsCaptureNoise(helpBlock, help));

    // A few specks one step above CropNoise's NoiseDelta (so noisy, but within its keep limits), far apart, on grey
    const FImage grey = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_BIG, PLAYERGUIDEIMAGEDIFFTEST_BIG,
        PLAYERGUIDEIMAGEDIFFTEST_GREY);
    const int32 speckDelta = FMath::Clamp(crop.NoiseDelta + 1, 1, FMath::Min(crop.KeepMaxDelta, 100));
    const int32 speckCount = FMath::Clamp(crop.KeepMaxNoisyPixels, 0, 3);
    FImage specks = grey;
    for (int32 index = 0; index < speckCount; ++index)
    {
        const int32 at = 10 + index * 20;
        PlayerGuideImageDiffTestSet(specks, at, at, FColor(static_cast<uint8>(100 + speckDelta), 100, 100, 255));
    }
    const FSOLImageDiff speckDiff = SOLPlayerGuide::CompareImages(specks, grey, crop);
    TestTrue(TEXT("The specks compare at CropNoise"), speckDiff.bComparable);
    TestTrue(FString::Printf(TEXT("%d specks off by %d are capture noise at CropNoise"), speckCount, speckDelta),
        SOLPlayerGuide::IsCaptureNoise(speckDiff, crop));
    TestEqual(TEXT("The specks' MaxDelta"), speckDiff.MaxDelta, speckDelta);
    TestEqual(TEXT("The specks' NoisyPixels (each above NoiseDelta)"), speckDiff.NoisyPixels,
        speckDelta > crop.NoiseDelta ? speckCount : 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffSettingsTest, "SOLTest.PlayerGuide.ImageDiff.Settings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CropNoise and HelpShotNoise are the SOL::Help constants field by field, none negative
bool FSOLPlayerGuideImageDiffSettingsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLImageNoise crop = SOLPlayerGuide::CropNoise();
    TestEqual(TEXT("CropNoise ShiftPx"), crop.ShiftPx, SOL::Help::GLOSSARY_CROP_NOISE_SHIFT_PX);
    TestEqual(TEXT("CropNoise NoiseDelta"), crop.NoiseDelta, SOL::Help::GLOSSARY_CROP_NOISE_DELTA);
    TestEqual(TEXT("CropNoise KeepMaxDelta"), crop.KeepMaxDelta, SOL::Help::GLOSSARY_CROP_KEEP_MAX_DELTA);
    TestEqual(TEXT("CropNoise KeepMaxNoisyPixels"), crop.KeepMaxNoisyPixels,
        SOL::Help::GLOSSARY_CROP_KEEP_MAX_NOISY_PIXELS);

    const FSOLImageNoise help = SOLPlayerGuide::HelpShotNoise();
    TestEqual(TEXT("HelpShotNoise ShiftPx"), help.ShiftPx, SOL::Help::GUIDE_SHOT_NOISE_SHIFT_PX);
    TestEqual(TEXT("HelpShotNoise NoiseDelta"), help.NoiseDelta, SOL::Help::GUIDE_SHOT_NOISE_DELTA);
    TestEqual(TEXT("HelpShotNoise KeepMaxDelta"), help.KeepMaxDelta, SOL::Help::GUIDE_SHOT_KEEP_MAX_DELTA);
    TestEqual(TEXT("HelpShotNoise KeepMaxNoisyPixels"), help.KeepMaxNoisyPixels,
        SOL::Help::GUIDE_SHOT_KEEP_MAX_NOISY_PIXELS);

    // No setting is negative
    const FSOLImageNoise* const settings[] = { &crop, &help };
    const TCHAR* const names[] = { TEXT("CropNoise"), TEXT("HelpShotNoise") };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(settings)); ++index)
    {
        TestTrue(FString::Printf(TEXT("%s ShiftPx >= 0"), names[index]), settings[index]->ShiftPx >= 0);
        TestTrue(FString::Printf(TEXT("%s NoiseDelta >= 0"), names[index]), settings[index]->NoiseDelta >= 0);
        TestTrue(FString::Printf(TEXT("%s KeepMaxDelta >= 0"), names[index]), settings[index]->KeepMaxDelta >= 0);
        TestTrue(FString::Printf(TEXT("%s KeepMaxNoisyPixels >= 0"), names[index]),
            settings[index]->KeepMaxNoisyPixels >= 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffFileTest, "SOLTest.PlayerGuide.ImageDiff.CompareWithFile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CompareWithFile: a missing or unreadable file is not comparable; a saved PNG round-trips to no difference and
// measures a changed image like CompareImages. The temporary files are deleted
bool FSOLPlayerGuideImageDiffFileTest::RunTest(const FString& /*parameters*/)
{
    const FSOLImageNoise noise = PlayerGuideImageDiffTestNoise(0, 2, PLAYERGUIDEIMAGEDIFFTEST_HUGE,
        PLAYERGUIDEIMAGEDIFFTEST_HUGE);
    const FImage image = PlayerGuideImageDiffTestGradient(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE);

    // A path that does not exist
    const FString missing = PlayerGuideImageDiffTestTempPath(TEXT("Missing"));
    TestFalse(TEXT("The missing path does not exist"), IFileManager::Get().FileExists(*missing));
    const FSOLImageDiff missingDiff = SOLPlayerGuide::CompareWithFile(image, missing, noise);
    PlayerGuideImageDiffTestExpect(*this, TEXT("A missing file"), missingDiff, false, 0, 0);
    TestFalse(TEXT("A missing file is not capture noise"), SOLPlayerGuide::IsCaptureNoise(missingDiff, noise));

    // A file that is not an image
    const FString garbage = PlayerGuideImageDiffTestTempPath(TEXT("Garbage"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(garbage), /*tree*/ true);
    TestTrue(TEXT("The non-image file is written"), FFileHelper::SaveStringToFile(TEXT("not a png"), *garbage));
    PlayerGuideImageDiffTestExpect(*this, TEXT("A file that does not load"),
        SOLPlayerGuide::CompareWithFile(image, garbage, noise), false, 0, 0);
    PlayerGuideImageDiffTestDelete(*this, garbage);

    // A round trip through an opaque PNG (alpha 255, so PNG keeping or dropping alpha does not matter)
    const FString saved = PlayerGuideImageDiffTestTempPath(TEXT("RoundTrip"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(saved), /*tree*/ true);
    const bool bSaved = FImageUtils::SaveImageByExtension(*saved, image);
    TestTrue(TEXT("The round-trip PNG is saved"), bSaved);
    if (bSaved)
    {
        const FSOLImageDiff same = SOLPlayerGuide::CompareWithFile(image, saved, noise);
        PlayerGuideImageDiffTestExpect(*this, TEXT("The saved image against its own file"), same, true, 0, 0);
        TestTrue(TEXT("The saved image against its own file is capture noise"),
            SOLPlayerGuide::IsCaptureNoise(same, noise));

        // One pixel's green up by 40: measured as by CompareImages
        FImage changed = image;
        const FColor original = PlayerGuideImageDiffTestGet(changed, 6, 9);
        PlayerGuideImageDiffTestSet(changed, 6, 9, FColor(original.R, static_cast<uint8>(original.G + 40), original.B,
            255));
        PlayerGuideImageDiffTestExpect(*this, TEXT("A changed image against the file"),
            SOLPlayerGuide::CompareWithFile(changed, saved, noise), true, 40, 1);
        PlayerGuideImageDiffTestExpect(*this, TEXT("CompareImages on the same pair"),
            SOLPlayerGuide::CompareImages(changed, image, noise), true, 40, 1);

        // A fresh image of another size against the file
        const FImage smaller = PlayerGuideImageDiffTestGradient(PLAYERGUIDEIMAGEDIFFTEST_SIZE - 1,
            PLAYERGUIDEIMAGEDIFFTEST_SIZE);
        PlayerGuideImageDiffTestExpect(*this, TEXT("A smaller image against the file"),
            SOLPlayerGuide::CompareWithFile(smaller, saved, noise), false, 0, 0);
    }
    PlayerGuideImageDiffTestDelete(*this, saved);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerGuideImageDiffReviewTest, "SOLTest.PlayerGuide.ImageDiff.ReviewCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Part 33e-iv review cases (test-after): a size without its pixels is not comparable (no read past the data), a
// negative NoiseDelta reads 0 whether a pixel matches in place or in the neighbourhood, a huge shift is clamped, and
// a faint tint over more than HelpShotNoise's pixel limit replaces the file
bool FSOLPlayerGuideImageDiffReviewTest::RunTest(const FString& /*parameters*/)
{
    const FSOLImageNoise loose = PlayerGuideImageDiffTestNoise(1, 0, PLAYERGUIDEIMAGEDIFFTEST_HUGE,
        PLAYERGUIDEIMAGEDIFFTEST_HUGE);
    const FImage base = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_SIZE, PLAYERGUIDEIMAGEDIFFTEST_SIZE,
        PLAYERGUIDEIMAGEDIFFTEST_GREY);

    // The size and format set, the pixels never allocated (and no slice)
    FImage hollow;
    hollow.SizeX = PLAYERGUIDEIMAGEDIFFTEST_SIZE;
    hollow.SizeY = PLAYERGUIDEIMAGEDIFFTEST_SIZE;
    hollow.Format = ERawImageFormat::BGRA8;
    TestFalse(TEXT("A sized image without pixels (fresh) is not comparable"),
        SOLPlayerGuide::CompareImages(hollow, base, loose).bComparable);
    TestFalse(TEXT("A sized image without pixels (old) is not comparable"),
        SOLPlayerGuide::CompareImages(base, hollow, loose).bComparable);

    // A negative NoiseDelta: the equal pixels (in place or one pixel away) are never noisy
    FImage moved = base;
    PlayerGuideImageDiffTestLine(moved, 5, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    FImage line = base;
    PlayerGuideImageDiffTestLine(line, 4, PLAYERGUIDEIMAGEDIFFTEST_WHITE);
    const FSOLImageDiff negative = SOLPlayerGuide::CompareImages(moved, line,
        PlayerGuideImageDiffTestNoise(1, -5, PLAYERGUIDEIMAGEDIFFTEST_HUGE, PLAYERGUIDEIMAGEDIFFTEST_HUGE));
    TestEqual(TEXT("A negative NoiseDelta reads 0: MaxDelta"), negative.MaxDelta, 0);
    TestEqual(TEXT("A negative NoiseDelta reads 0: NoisyPixels"), negative.NoisyPixels, 0);

    // A huge shift is clamped to the image (no overflow): the line is found anywhere
    const FSOLImageDiff huge = SOLPlayerGuide::CompareImages(moved, line,
        PlayerGuideImageDiffTestNoise(PLAYERGUIDEIMAGEDIFFTEST_HUGE, 0, 0, 0));
    TestTrue(TEXT("A huge shift compares"), huge.bComparable);
    TestEqual(TEXT("A huge shift: MaxDelta"), huge.MaxDelta, 0);

    // A faint tint (40 per channel) over a 64 x 64 area (4096 pixels, above HelpShotNoise's pixel limit)
    const FSOLImageNoise help = SOLPlayerGuide::HelpShotNoise();
    const FImage dark = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_BIG, PLAYERGUIDEIMAGEDIFFTEST_BIG,
        PLAYERGUIDEIMAGEDIFFTEST_GREY);
    const FImage tinted = PlayerGuideImageDiffTestMake(PLAYERGUIDEIMAGEDIFFTEST_BIG, PLAYERGUIDEIMAGEDIFFTEST_BIG,
        FColor(140, 140, 140, 255));
    const FSOLImageDiff tint = SOLPlayerGuide::CompareImages(tinted, dark, help);
    TestEqual(TEXT("The faint tint's MaxDelta"), tint.MaxDelta, 40);
    TestEqual(TEXT("The faint tint's NoisyPixels"), tint.NoisyPixels,
        PLAYERGUIDEIMAGEDIFFTEST_BIG * PLAYERGUIDEIMAGEDIFFTEST_BIG);
    TestFalse(TEXT("A faint tint over 4096 pixels is not capture noise at HelpShotNoise"),
        SOLPlayerGuide::IsCaptureNoise(tint, help));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
