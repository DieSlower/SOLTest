/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcStatsWidget.h"

#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
    // The engine's monospace typeface, so the numbers line up
    const TCHAR* const STATS_WIDGET_TYPEFACE = TEXT("Mono");
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint)
void USOLNpcStatsWidget::NativeOnInitialized()
{
    using namespace SOL::MenuStyle;
    Super::NativeOnInitialized();
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);

    // Full-screen transparent root, the panel in the top-right corner
    UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    root->SetBrushColor(FLinearColor::Transparent);
    root->SetPadding(FMargin(STATS_OVERLAY_MARGIN));
    root->SetHorizontalAlignment(HAlign_Right);
    root->SetVerticalAlignment(VAlign_Top);
    WidgetTree->RootWidget = root;
    USizeBox* box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    box->SetMinDesiredWidth(STATS_OVERLAY_WIDTH);
    root->SetContent(box);
    UBorder* panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    panel->SetBrushColor(STATS_BACKGROUND);
    panel->SetPadding(FMargin(BUTTON_GAP));
    box->SetContent(panel);
    Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Lines"));
    Text->SetFont(FCoreStyle::GetDefaultFontStyle(STATS_WIDGET_TYPEFACE, STATS_FONT_SIZE));
    Text->SetColorAndOpacity(FSlateColor(TEXT_COLOR));
    panel->SetContent(Text);
}

//////////////////////////////////////////////////////////////////////////
// Replaces the shown text (one line per entry)
void USOLNpcStatsWidget::SetLines(const TArray<FString>& lines)
{
    if (Text != nullptr)
    {
        Text->SetText(FText::FromString(FString::Join(lines, TEXT("\n"))));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the shown text (verification)
FString USOLNpcStatsWidget::GetText() const
{
    return Text != nullptr ? Text->GetText().ToString() : FString();
}
