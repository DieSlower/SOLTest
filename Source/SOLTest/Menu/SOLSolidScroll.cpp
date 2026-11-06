/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidScroll.h"

#include "SOLConstants.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a height as the scroll maths reads it: negative or non-finite reads 0
    float SolidScrollHeight(const float height)
    {
        return FMath::IsFinite(height) && height > 0.0f ? height : 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
// The farthest a view scrolls
float SOLSolidScroll::MaxOffset(const float contentHeight, const float viewHeight)
{
    return FMath::Max(0.0f, SolidScrollHeight(contentHeight) - SolidScrollHeight(viewHeight));
}

//////////////////////////////////////////////////////////////////////////
// Whether the content needs scrolling
bool SOLSolidScroll::NeedsScroll(const float contentHeight, const float viewHeight)
{
    return SolidScrollHeight(contentHeight) - SolidScrollHeight(viewHeight) > SOL::SolidMenu::SCROLL_FIT_TOLERANCE_PX;
}

//////////////////////////////////////////////////////////////////////////
// The offset clamped to [0, MaxOffset]
float SOLSolidScroll::ClampOffset(const float offset, const float contentHeight, const float viewHeight)
{
    if (FMath::IsNaN(offset))
    {
        return 0.0f;
    }
    return FMath::Clamp(offset, 0.0f, MaxOffset(contentHeight, viewHeight));
}

//////////////////////////////////////////////////////////////////////////
// The offset that shows an item whole, moving as little as possible
float SOLSolidScroll::OffsetToShow(const float offset, const float itemTop, const float itemHeight,
    const float contentHeight, const float viewHeight)
{
    if (!FMath::IsFinite(itemTop) || !FMath::IsFinite(itemHeight) || itemHeight < 0.0f)
    {
        return ClampOffset(offset, contentHeight, viewHeight);
    }
    const float current = ClampOffset(offset, contentHeight, viewHeight);
    const float view = SolidScrollHeight(viewHeight);
    const float itemBottom = itemTop + itemHeight;
    float target = current;
    if (itemTop < current || itemHeight > view)
    {
        target = itemTop;
    }
    else if (itemBottom > current + view)
    {
        target = itemBottom - view;
    }
    return ClampOffset(target, contentHeight, viewHeight);
}

//////////////////////////////////////////////////////////////////////////
// Item index's top in the content
float SOLSolidScroll::StackItemTop(const int32 index, const float itemHeight, const float gap, const float padding)
{
    return padding + FMath::Max(index, 0) * (itemHeight + gap);
}

//////////////////////////////////////////////////////////////////////////
// A stack's content height
float SOLSolidScroll::StackContentHeight(const int32 count, const float itemHeight, const float gap,
    const float padding)
{
    return 2.0f * padding + FMath::Max(count, 0) * (itemHeight + gap);
}

//////////////////////////////////////////////////////////////////////////
// The view height a stack gets under a cap
float SOLSolidScroll::StackViewHeight(const float contentHeight, const float maxHeight)
{
    if (!FMath::IsFinite(maxHeight) || maxHeight <= 0.0f)
    {
        return contentHeight;
    }
    return FMath::Min(contentHeight, maxHeight);
}

//////////////////////////////////////////////////////////////////////////
// Whether a line shows once scrolled
bool SOLSolidScroll::IsLineInView(const float y, const float offset, const float viewTop, const float viewHeight)
{
    if (FMath::IsNaN(y) || FMath::IsNaN(offset) || FMath::IsNaN(viewTop) || FMath::IsNaN(viewHeight))
    {
        return false;
    }
    const float shown = y - offset;
    return shown >= viewTop && shown <= viewTop + viewHeight;
}
