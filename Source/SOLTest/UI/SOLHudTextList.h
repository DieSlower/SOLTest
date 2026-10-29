/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// The engine fonts a HUD text line can use (GEngine's small, medium and large font)
enum class ESOLHudFont : uint8
{
    Small = 0,
    Medium = 1,
    Large = 2,
    Count = 3,
};

// One HUD text line in canvas pixels: its text, top-left position, scale on the font's size, colour and font
struct FSOLHudTextItem
{
    FText Text;
    FVector2f Position = FVector2f::ZeroVector;
    float Scale = 1.0f;
    FLinearColor Color = FLinearColor::White;
    ESOLHudFont Font = ESOLHudFont::Small;
};

/**
 * One frame's glow-text lines of the flight HUD (SDD 21 phase B), drawn by SSOLHudShapeLayer over its shapes. A fixed
 * capacity reserved once by Initialize: Add copies the FText (a reference-count increment, no allocation) and a line
 * that would exceed the capacity is dropped (counted), never reallocated.
 */
struct SOLTEST_API FSOLHudTextList
{
    // Reserves room for maxItems lines (the only allocation; a negative count is treated as 0) and empties the list
    void Initialize(int32 maxItems);

    // Empties the list and its dropped count, keeping its capacity
    void Reset();

    // Appends a line; false (and counted as dropped) when the list is full
    bool Add(const FText& text, const FVector2f& position, float scale, const FLinearColor& color, ESOLHudFont font);

    // Returns the lines added since the last Reset, in the order they were added
    TConstArrayView<FSOLHudTextItem> GetItems() const { return mItems; }

    // Returns the number of lines added since the last Reset
    int32 Num() const { return mItems.Num(); }

    // Returns the capacity set by Initialize
    int32 GetMaxItems() const { return mMaxItems; }

    // Returns the number of lines dropped since the last Reset because the list was full
    int32 GetDroppedCount() const { return mDroppedCount; }

private:

    TArray<FSOLHudTextItem> mItems;     // Lines of this frame
    int32 mMaxItems = 0;                // Capacity reserved by Initialize
    int32 mDroppedCount = 0;            // Lines dropped this frame (list full)
};
