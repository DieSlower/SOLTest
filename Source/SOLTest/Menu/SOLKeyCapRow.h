/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLKeyCaps.h"

#include "Components/Widget.h"
#include "CoreMinimal.h"
#include "Fonts/ShapedTextFwd.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include "SOLKeyCapRow.generated.h"

/**
 * A row of drawn key caps (SDD 33 section 6, 33b): a hit-test-invisible leaf that paints a parsed bindings cell or
 * binding (SOLKeyCaps) as caps, one hull box each in the solid kit's Small button style (the shared hull MID, no new
 * material; an unbound cap the recess style), its legend in Rajdhani SemiBold, a mouse glyph with the used part lit in
 * cyan on mouse caps (SOLKeyCaps::MouseGlyph), "+" between a chord's caps and "/" between alternatives in the dim
 * colour, wrapped in its wrap width by SOLKeyCaps::LayoutRow. Or, while a message is set, that text instead (the rebind
 * waiting prompt). The legends are measured once per caps or style change, the row laid out again on a wrap width
 * change, and the texts shaped once per such change, per paint scale and after a Slate invalidate-all (the shaped
 * glyphs are kept), so a paint allocates nothing. A bound cap's content sits right of the Small hull's LED
 * (SOLKeyCaps::ContentLeft). No tick.
 */
class SOLTEST_API SSOLKeyCapRow : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SSOLKeyCapRow) {}
    SLATE_END_ARGS()

    // Makes the row hit-test invisible and tickless and listens for Slate's invalidate-all
    void Construct(const FArguments& args);

    // Stops listening for Slate's invalidate-all
    virtual ~SSOLKeyCapRow() override;

    // Sets the cap brushes (a bound cap, an unbound cap) and the fonts (legend, message);
    // re-measures
    void SetStyle(const FSlateBrush& capBrush, const FSlateBrush& unboundBrush, const FSlateFontInfo& legendFont,
        const FSlateFontInfo& messageFont);

    // Sets the caps (copied), re-measures and re-lays them out and ends a message
    void SetModel(const FSOLKeyCapModel& model);

    // Shows a text in place of the caps until the next SetModel
    void SetMessage(const FString& message);

    // Sets the width the caps wrap in (local units; not a positive number lays them out on one line)
    void SetWrapWidth(float width);

    // Sets the legend colour of bound caps (linear), repainting only on a change
    void SetLegendColor(const FLinearColor& color);

    // Returns the laid-out caps' width and height (local units; the message's while one shows) and line count
    float GetLayoutWidth() const;
    float GetLayoutHeight() const;
    int32 GetLineCount() const { return mIsMessage ? 1 : mLayout.LineCount; }

    // Returns whether every drawn legend and the "+" and "/" joints were measured wider than 0 (verification: a cap
    // whose legend measured 0 would be laid out too narrow)
    bool AreLegendsMeasured() const;

    // Returns whether the last paint drew every cap's hull with the hull material and every drawn legend shaped at the
    // paint scale, or the message (verification: the caps on screen, not only laid out)
    bool WasPaintedWhole() const;

    // Paints the caps (or the message)
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // The laid-out size: the caps' (at least one cap tall), or the message's
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    // Returns the font measure service, or null without Slate or before the style is set
    const FSlateFontMeasure* GetMeasure() const;

    // Measures the drawn legends and the "+" and "/" joints at scale 1 (on a caps or style change); the shaped legends
    // go stale
    void MeasureLegends();

    // Measures the message at scale 1 (on a message or style change); the shaped message goes stale
    void MeasureMessage();

    // Lays the caps out in the wrap width from the measured legends (no measure, no reshape)
    void Layout();

    // Slate invalidated every widget (a font cache flush unloads the faces the shaped texts point at): reshape
    void HandleInvalidateAll(bool bClearResourcesImmediately);

    // Shapes the stale drawn texts at a paint scale (on the first paint and after a content, font or scale change)
    void Reshape(float scale) const;

    // Paints one cap (its hull, glyph and legend) at a laid-out item with the shared cap painter (SOLKeyCapPaint)
    int32 PaintCap(const FSOLKeyCapLayoutItem& item, const FGeometry& geometry, FSlateWindowElementList& out,
        int32 layerId, const FLinearColor& styleTint, ESlateDrawEffect effects) const;

    FSOLKeyCapModel mModel;
    FSOLKeyCapLayout mLayout;
    TArray<float> mLegendWidths;                    // Each cap's drawn legend, measured (local units)
    TArray<float> mCapWidths;                       // Each cap's drawn width (SOLKeyCaps::DrawnCapWidth)
    FString mMessage;
    FSlateBrush mCapBrush;
    FSlateBrush mUnboundBrush;
    FSlateBrush mLineBrush;                         // A flat white box: the glyph's lines and lit part
    FSlateFontInfo mLegendFont;
    FSlateFontInfo mMessageFont;
    FLinearColor mLegendColor = FLinearColor::White;
    float mWrapWidth = 0.0f;
    float mMessageWidth = 0.0f;
    float mMessageHeight = 0.0f;
    float mPlusWidth = 0.0f;                        // The "+" and "/" joints, measured (local units)
    float mSlashWidth = 0.0f;
    bool mIsMessage = false;
    bool mHasStyle = false;
    FDelegateHandle mInvalidateAllHandle;

    // The shaped texts, kept between paints (rebuilt by Reshape)
    mutable TArray<FShapedGlyphSequencePtr> mShapedLegends;
    mutable FShapedGlyphSequencePtr mShapedPlus;
    mutable FShapedGlyphSequencePtr mShapedSlash;
    mutable FShapedGlyphSequencePtr mShapedMessage;
    mutable float mShapedScale = -1.0f;             // The scale the legends and joints were shaped at (-1: stale)
    mutable float mShapedMessageScale = -1.0f;      // ... and the message
    mutable float mPaintedScale = 0.0f;             // The last paint (verification: WasPaintedWhole)
    mutable int32 mPaintedCaps = 0;
    mutable int32 mPaintedLegends = 0;
};

/**
 * The UMG wrapper of SSOLKeyCapRow (SDD 33 section 6, 33b): the Controls page's key cells and the Settings > Key
 * bindings rows' key cells (D20). It keeps the parsed caps, the message, the wrap width and the legend colour itself,
 * so the state is set and read without Slate (headless tests) and handed to the leaf when it is built. Setting the
 * same caps again changes nothing (the cell is parsed into a scratch model and compared first), so refreshing a page on
 * every show re-measures only the cells whose keys changed.
 */
UCLASS()
class SOLTEST_API USOLKeyCapRow : public UWidget
{
    GENERATED_BODY()

public:

    // Starts with the solid text colour as the legend colour
    USOLKeyCapRow();

    // Shows a SOLMenuText::CONTROL_ROWS bindings cell's caps at the live keys (SOLKeyCaps::ParseCell); ends a message
    void SetCell(const TCHAR* cell, const TArray<FString>& liveKeys);

    // Shows one binding's caps at the keys (SOLKeyCaps::ParseBinding); ends a message
    void SetBinding(int32 binding, const TArray<FString>& liveKeys);

    // Shows a text in place of the caps (the rebind waiting prompt) until the next SetCell / SetBinding; the caps are
    // kept
    void SetMessage(const FString& message);

    // Returns whether a message shows in place of the caps
    bool IsShowingMessage() const { return mIsShowingMessage; }

    // Returns the cell's text: the message while one shows, else SOLKeyCaps::ModelText of the caps
    FString GetText() const;

    // Returns the caps shown (or kept under a message)
    const FSOLKeyCapModel& GetModel() const { return mModel; }

    // Sets the width the caps wrap in (menu units; not a positive number: one line)
    void SetWrapWidth(float width);

    // Returns the wrap width set
    float GetWrapWidth() const { return mWrapWidth; }

    // Sets the legend colour of bound caps (linear; the Settings page's warning colour on a conflicting row)
    void SetLegendColor(const FLinearColor& color);

    // Returns the legend colour of bound caps (SOLSolidStyle::TextColor until one is set)
    FLinearColor GetLegendColor() const { return mLegendColor; }

    // Returns the laid-out width and height of the caps (or the message) and the line count, -1 / 0 before the leaf is
    // built (verification: a cell's caps inside their column)
    float GetLaidOutWidth() const;
    float GetLaidOutHeight() const;
    int32 GetLaidOutLines() const;

    // Returns whether the leaf measured every drawn legend and joint wider than 0 (false before it is built;
    // verification)
    bool AreLegendsMeasured() const;

    // Returns whether the leaf's last paint drew every cap with the hull material and every drawn legend (false before
    // it is built or painted; verification)
    bool WasPaintedWhole() const;

    // Drops the leaf
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:

    // Builds the leaf with the solid kit's cap brushes and fonts and hands it the state
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:

    // Takes the scratch model as the caps when it differs from them (or a message showed) and hands it to the leaf
    void TakeScratch();

    TSharedPtr<SSOLKeyCapRow> mRow;
    FSOLKeyCapModel mModel;
    FSOLKeyCapModel mScratch;                       // ParseCell / ParseBinding target, compared with mModel
    FString mMessage;
    FLinearColor mLegendColor;
    float mWrapWidth = 0.0f;
    bool mIsShowingMessage = false;
};
