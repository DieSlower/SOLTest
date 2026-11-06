/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

/**
 * Pure maths of the scrolling rails and stacks (SDD 46 D17, part 46g), widget-free. A stack is count items of
 * itemHeight, each followed by gap, inside padding at both ends (the same sum as SOLSolidTabRail::RailHeight). Offsets
 * are the content's scroll offset in UMG units (0 = top, positive = scrolled down); a view is viewHeight tall.
 */
namespace SOLSolidScroll
{
    // The farthest a view scrolls: max(0, contentHeight - viewHeight); a negative or non-finite height reads 0
    SOLTEST_API float MaxOffset(float contentHeight, float viewHeight);

    // Whether the content needs scrolling: it is taller than the view by more than SCROLL_FIT_TOLERANCE_PX (heights
    // read as MaxOffset reads them)
    SOLTEST_API bool NeedsScroll(float contentHeight, float viewHeight);

    // The offset clamped to [0, MaxOffset]; NaN reads 0
    SOLTEST_API float ClampOffset(float offset, float contentHeight, float viewHeight);

    // The offset that shows the item [itemTop, itemTop + itemHeight] whole while moving as little as possible from
    // offset: unchanged when it already shows whole, itemTop when it is above the view (or taller than the view),
    // itemTop + itemHeight - viewHeight when it is below; always clamped as ClampOffset. A non-finite itemTop or
    // itemHeight, or a negative itemHeight, returns ClampOffset(offset)
    SOLTEST_API float OffsetToShow(float offset, float itemTop, float itemHeight, float contentHeight,
        float viewHeight);

    // Item index's top in the content: padding + index x (itemHeight + gap); a negative index reads 0
    SOLTEST_API float StackItemTop(int32 index, float itemHeight, float gap, float padding);

    // A stack's content height: 2 x padding + count x (itemHeight + gap); a negative count reads 0
    SOLTEST_API float StackContentHeight(int32 count, float itemHeight, float gap, float padding);

    // The view height a stack gets under a cap: min(contentHeight, maxHeight); a cap <= 0 or non-finite leaves the
    // content height (no cap)
    SOLTEST_API float StackViewHeight(float contentHeight, float maxHeight);

    // Whether a line at y (panel local, unscrolled) shows once scrolled by offset inside a view band starting at
    // viewTop (panel local) viewHeight tall: viewTop <= y - offset <= viewTop + viewHeight; false for any NaN
    SOLTEST_API bool IsLineInView(float y, float offset, float viewTop, float viewHeight);
}
