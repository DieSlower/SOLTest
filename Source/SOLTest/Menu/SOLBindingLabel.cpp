/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBindingLabel.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the display text for names that differ from the engine's, or an empty string when there is no special case
    const TCHAR* SpecialDisplayName(const FString& keyName)
    {
        static const TMap<FString, const TCHAR*> table = {
            {TEXT("LeftMouseButton"), TEXT("LMB")}, {TEXT("RightMouseButton"), TEXT("RMB")},
            {TEXT("MiddleMouseButton"), TEXT("MMB")}, {TEXT("SpaceBar"), TEXT("Space")},
            {TEXT("LeftShift"), TEXT("Shift")}, {TEXT("LeftControl"), TEXT("Ctrl")}, {TEXT("LeftAlt"), TEXT("Alt")},
            {TEXT("Escape"), TEXT("Esc")}, {TEXT("MouseScrollUp"), TEXT("Wheel Up")},
            {TEXT("MouseScrollDown"), TEXT("Wheel Down")}, {TEXT("Zero"), TEXT("0")}, {TEXT("One"), TEXT("1")},
            {TEXT("Two"), TEXT("2")}, {TEXT("Three"), TEXT("3")}, {TEXT("Four"), TEXT("4")}, {TEXT("Five"), TEXT("5")},
            {TEXT("Six"), TEXT("6")}, {TEXT("Seven"), TEXT("7")}, {TEXT("Eight"), TEXT("8")}, {TEXT("Nine"), TEXT("9")},
            {TEXT("Add"), TEXT("+")}, {TEXT("Subtract"), TEXT("-")}, {TEXT("Equals"), TEXT("=")},
            {TEXT("LeftBracket"), TEXT("[")}, {TEXT("RightBracket"), TEXT("]")}, {TEXT("Hyphen"), TEXT("-")},
            {TEXT("BackSpace"), TEXT("Backspace")}, {TEXT("MouseWheelAxis"), TEXT("Wheel")},
            {TEXT("Mouse2D"), TEXT("Mouse")}};
        const TCHAR* const* found = table.Find(keyName);
        return found != nullptr ? *found : nullptr;
    }
}

//////////////////////////////////////////////////////////////////////////
// Maps an engine key name to display text
FString SOLBindingLabel::KeyDisplayName(const FString& keyName)
{
    if (keyName.IsEmpty())
    {
        return TEXT("Unbound");
    }
    const TCHAR* special = SpecialDisplayName(keyName);
    return special != nullptr ? FString(special) : keyName;
}

//////////////////////////////////////////////////////////////////////////
// Joins key display names into a chord label
FString SOLBindingLabel::JoinChord(const TArray<FString>& keyNames)
{
    if (keyNames.IsEmpty())
    {
        return TEXT("Unbound");
    }
    TArray<FString> labels;
    labels.Reserve(keyNames.Num());
    for (const FString& keyName : keyNames)
    {
        labels.Add(KeyDisplayName(keyName));
    }
    return FString::Join(labels, TEXT("+"));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the key may be a rebind target
bool SOLBindingLabel::IsRebindable(const FString& keyName)
{
    return !keyName.IsEmpty() && keyName != TEXT("Escape");
}
