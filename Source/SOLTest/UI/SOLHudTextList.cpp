/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudTextList.h"

//////////////////////////////////////////////////////////////////////////
// Reserves room for maxItems lines (the only allocation) and empties the list
void FSOLHudTextList::Initialize(const int32 maxItems)
{
    mMaxItems = FMath::Max(maxItems, 0);
    mItems.Empty(mMaxItems);
    mDroppedCount = 0;
}

//////////////////////////////////////////////////////////////////////////
// Empties the list and its dropped count, keeping its capacity
void FSOLHudTextList::Reset()
{
    mItems.Reset();
    mDroppedCount = 0;
}

//////////////////////////////////////////////////////////////////////////
// Appends a line; false (and counted as dropped) when the list is full
bool FSOLHudTextList::Add(const FText& text, const FVector2f& position, const float scale, const FLinearColor& color,
    const ESOLHudFont font)
{
    if (mItems.Num() >= mMaxItems)
    {
        ++mDroppedCount;
        return false;
    }
    FSOLHudTextItem& item = mItems.AddDefaulted_GetRef();
    item.Text = text;
    item.Position = position;
    item.Scale = scale;
    item.Color = color;
    item.Font = font;
    return true;
}
