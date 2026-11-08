/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyboardDiagramSmokePage.h"

#include "Menu/SOLKeyboardDiagram.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSolidPanel.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

//////////////////////////////////////////////////////////////////////////
// Sets the content width the diagram is laid out in
void USOLKeyboardDiagramSmokePage::SetContentWidth(const float width)
{
    if (Diagram != nullptr)
    {
        Diagram->SetLayoutWidth(width);
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the page: a transparent root centring a hull panel (the page shell's chamfers and padding) with the diagram
void USOLKeyboardDiagramSmokePage::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    UBorder* root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    root->SetBrushColor(FLinearColor::Transparent);
    root->SetHorizontalAlignment(HAlign_Center);
    root->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = root;

    Panel = WidgetTree->ConstructWidget<USOLSolidPanel>(USOLSolidPanel::StaticClass());
    FSOLSolidHull hull;
    hull.ChamferTL = SOL::SolidMenu::PANEL_CHAMFER_TL;
    hull.ChamferTR = SOL::SolidMenu::PANEL_CHAMFER_TR;
    hull.ChamferBR = SOL::SolidMenu::PANEL_CHAMFER_BR;
    hull.ChamferBL = SOL::SolidMenu::PANEL_CHAMFER_BL;
    Panel->SetHull(ESOLSolidStyle::Panel, hull);
    Panel->SetContentPadding(FMargin(SOL::MenuStyle::PANEL_PADDING));
    root->SetContent(Panel);

    Diagram = WidgetTree->ConstructWidget<USOLKeyboardDiagram>(USOLKeyboardDiagram::StaticClass());
    Panel->AddChild(Diagram);
}

//////////////////////////////////////////////////////////////////////////
// Sets the live keys and binds the key bindings changed event (the diagram's owner, as the Help page will be)
void USOLKeyboardDiagramSmokePage::NativeConstruct()
{
    Super::NativeConstruct();
    if (Diagram != nullptr)
    {
        Diagram->SetKeys(SOLMenuContent::ResolveLiveKeys(GetWorld()));
    }
    USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(this);
    if (settings != nullptr && !mBindingsHandle.IsValid())
    {
        mBindingsHandle = settings->OnKeyBindingsChanged().AddUObject(this,
            &USOLKeyboardDiagramSmokePage::HandleKeyBindingsChanged);
    }
}

//////////////////////////////////////////////////////////////////////////
// Unbinds the key bindings changed event
void USOLKeyboardDiagramSmokePage::NativeDestruct()
{
    USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(this);
    if (settings != nullptr && mBindingsHandle.IsValid())
    {
        settings->OnKeyBindingsChanged().Remove(mBindingsHandle);
    }
    mBindingsHandle.Reset();
    Super::NativeDestruct();
}

//////////////////////////////////////////////////////////////////////////
// Sets the diagram's keys to the live ones
void USOLKeyboardDiagramSmokePage::HandleKeyBindingsChanged()
{
    ++mBindingsChanged;
    if (Diagram != nullptr)
    {
        Diagram->SetKeys(SOLMenuContent::ResolveLiveKeys(GetWorld()));
    }
}
