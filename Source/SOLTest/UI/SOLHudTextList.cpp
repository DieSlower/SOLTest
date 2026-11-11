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
// Empties the list, its dropped count and the glow alpha override, keeping its capacity
void FSOLHudTextList::Reset()
{
    mItems.Reset();
    mDroppedCount = 0;
    ClearGlowAlphaOverride();
}

//////////////////////////////////////////////////////////////////////////
// Gives the lines added from now on their own glow-text outline alpha (clamped to [0, 1]; NaN = the layer's)
void FSOLHudTextList::SetGlowAlphaOverride(const float alpha)
{
    mGlowAlpha = FMath::IsNaN(alpha) ? -1.0f : FMath::Clamp(alpha, 0.0f, 1.0f);
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
    item.GlowAlpha = mGlowAlpha;
    return true;
}
