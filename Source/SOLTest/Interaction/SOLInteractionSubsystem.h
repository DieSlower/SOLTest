/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLActionTable.h"
#include "Interaction/SOLInteractMenus.h"
#include "MinorBodies/SOLAsteroidBelt.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLInteractionSubsystem.generated.h"

class USOLBodyRegistrySubsystem;
class USOLCombatSubsystem;
class USOLDroppedTargetTargetable;
class USOLJumpSubsystem;
class USOLNpcSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;

// One object's facts for the info card and the menus' titles (ecliptic metres, m/s)
struct FSOLObjectInfo
{
    FName Name;
    ESOLObjectType Type = ESOLObjectType::Count;
    FName ParentName;                               // A moon's planet (NAME_None otherwise)
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d VelocityMps = FVector3d::ZeroVector;
    double RadiusM = 0.0;
    float ShieldFraction = -1.0f;                   // Dropped targets: shield and health in [0, 1] (-1 otherwise)
    float HealthFraction = -1.0f;
};

/**
 * The interaction menus' model and orders (SDD 22 section 2.4): the generic action table applied to the flight
 * view's overlay icons (the radial menu) and the map's bodies (the list menu), and the code that runs a chosen action
 * through the existing APIs: Target (USOLTargetingSubsystem::SelectCandidate; a dropped target through one
 * USOLDroppedTargetTargetable proxy), Info (the HUD's info card for INFO_CARD_S), Move To (USOLShipSubsystem::
 * EngageAutopilot, the stop-at-standoff body order), Follow / Orbit (a station-keeping point order at the autopilot's
 * holdable standoff that keeps AUTO on arrival), Jump To (USOLJumpSubsystem::StartJump next to the object), Match
 * speed (USOLTargetingSubsystem::LockToBodyIndex) and Attack (target it, plus the HUD's fire hint).
 *
 * The HUD hands over the icons it drew each frame (SetIconHits); the pawn hands over the free cursor and the left
 * button. Hover, the radial menu's press-drag-release and the hit-testing are the pure SOLRadialMenu functions. The
 * map's list menu (USOLActionListWidget, opened by the pawn) only reads BuildActions and calls Execute. No per-frame
 * work runs here except the proxy check on OnCombatUpdated (one slot) while a dropped target is targeted.
 */
UCLASS()
class SOLTEST_API USOLInteractionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Resolves the subsystems the orders go through, caches the named asteroids and reserves the hit buffer
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Unregisters the dropped-target proxy and unhooks from the combat update
    virtual void Deinitialize() override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Copies the icons the HUD drew this frame (canvas pixels, nearest first); at most MAX_ICONS, no allocation
    void SetIconHits(TConstArrayView<FSOLIconHit> hits, const FVector2D& viewSizePx, float uiScale);

    // Forgets the icons (a frame without them: overlays off, HUD hidden, map open, warp)
    void ClearIconHits();

    // Updates the hovered icon from a free cursor (none when bCursorFree is false; the menu's object while it is open)
    void UpdateHover(const FVector2D& cursorPx, bool bCursorFree);

    // Returns the hovered (or open menu's) object, or an invalid reference
    const FSOLObjectRef& GetHovered() const { return mHovered; }

    // Left press with a free cursor: opens the radial menu on the icon under the cursor; true when it did (the press
    // is the menu's and must not fire)
    bool PressPrimary(const FVector2D& cursorPx);

    // Pointer moved while the radial menu is open: updates its highlighted sector
    void MovePointer(const FVector2D& cursorPx);

    // Left release: confirms the sector under the pointer (runs its action) or cancels; true when a menu was open
    bool ReleasePrimary(const FVector2D& cursorPx);

    // Closes the radial menu without running anything (Esc, steering, a panel or the map taking the input); true when
    // one was open
    bool CancelRadial();

    // Returns true while the radial menu is open
    bool IsRadialOpen() const { return mRadial.bOpen; }

    // Returns the radial menu's state
    const FSOLRadialMenuState& GetRadial() const { return mRadial; }

    // Returns the radial menu's actions as they were when it opened (valid while open)
    const FSOLActionList& GetRadialActions() const { return mRadialActions; }

    // Returns the action-table type of an object (Count for an unknown one)
    ESOLObjectType GetObjectType(const FSOLObjectRef& object) const;

    // Fills an object's actions with their availability now; false (empty list) for an unknown object
    bool BuildActions(const FSOLObjectRef& object, FSOLActionList& outList) const;

    // Runs an action on an object when it is available now; returns whether it ran (logged either way)
    bool Execute(const FSOLObjectRef& object, ESOLAction action);

    // Fills an object's facts (name, type, position, ...); false for an unknown or vanished object
    bool GetObjectInfo(const FSOLObjectRef& object, FSOLObjectInfo& outInfo) const;

    // Returns the object whose info card shows now, or an invalid reference once it timed out
    FSOLObjectRef GetInfoObject() const;

    // Closes the info card
    void ClearInfo() { mInfoObject = FSOLObjectRef(); }

    // Returns the action whose HUD hint shows now (Attack), or Count when none
    ESOLAction GetHintAction() const;

    // Returns the object the last hint is about
    const FSOLObjectRef& GetHintObject() const { return mHintObject; }

    // Returns how many actions ran so far (verification)
    int32 GetExecutedCount() const { return mExecutedCount; }

    // Returns the last action that ran (verification)
    ESOLAction GetLastExecutedAction() const { return mLastExecutedAction; }

    // Returns the object of the last action that ran (verification)
    const FSOLObjectRef& GetLastExecutedObject() const { return mLastExecutedObject; }

    // Returns how many radial menus were opened so far (verification)
    int32 GetRadialOpenCount() const { return mRadialOpenCount; }

    // Returns how many radial menus were cancelled so far (verification)
    int32 GetRadialCancelCount() const { return mRadialCancelCount; }

    // Returns the selected target as an object (a body, or the targeted dropped target), else an invalid reference
    FSOLObjectRef GetSelectedObject() const;

    // Returns the icon hits of the last SetIconHits (verification)
    TConstArrayView<FSOLIconHit> GetIconHits() const { return mHits; }

    // Starts a Chase Down on a dropped target through the chase proxy (never a targeting candidate, so the selection
    // and R / F cycling are unaffected; SDD 32 section 3.6); false when the slot holds no target or the order failed
    bool ChaseDroppedTarget(int32 slot);

    // Returns the chase proxy's slot while the running chase is on a dropped target, else INDEX_NONE
    int32 GetChasedDroppedTargetSlot() const;

    // Returns whether the HUD shows "CHASE ENDED: target lost" now: for SOL::Chase::TARGET_LOST_NOTE_S real seconds
    // after the loss, and only while no autopilot order or chase runs (a new order's marker is never hidden)
    bool IsChaseTargetLostNoteShown() const;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Gathers the availability facts of one object
    FSOLActionContext BuildContext(const FSOLObjectRef& object) const;

    // Returns the targeting candidate index of an object, or INDEX_NONE (bodies; the proxy's dropped target)
    int32 FindCandidate(const FSOLObjectRef& object) const;

    // Targets an object: selects its candidate (a dropped target through the proxy); false if it cannot be
    bool TargetObject(const FSOLObjectRef& object);

    // Orders the autopilot to keep station on a body at its holdable standoff, on the ship's side
    bool FollowBody(int32 bodyIndex);

    // Starts a jump next to a body (Jump To)
    bool JumpToBody(int32 bodyIndex);

    // Drops the dropped-target proxy's registration when its target is gone or no longer selected or locked
    void HandleCombatUpdated();

    // Unregisters the proxy from targeting (if it is registered)
    void ReleaseProxy();

    // Returns the real time used for the info card and hints
    double GetNowS() const;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLJumpSubsystem> Jump;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLCombatSubsystem> Combat;

    UPROPERTY(Transient)
    TObjectPtr<USOLNpcSubsystem> Npcs;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLDroppedTargetTargetable> TargetProxy;    // Created once, registered while a drop is targeted

    UPROPERTY(Transient)
    TObjectPtr<USOLDroppedTargetTargetable> ChaseProxy;     // Created once, bound to the chased drop (never registered)

    TArray<FSOLIconHit> mHits;                              // Icons the HUD drew last (reserved once)
    TArray<FSOLAsteroidDef> mNamedAsteroids;                // Same list and order as the icon layer's
    FSOLRadialMenuState mRadial;                            // Flight radial menu
    FSOLActionList mRadialActions;                          // Its actions as they were when it opened
    FSOLObjectRef mHovered;                                 // Icon under the free cursor
    FSOLObjectRef mInfoObject;                              // Info card object
    FSOLObjectRef mHintObject;                              // Hint object
    FSOLObjectRef mLastExecutedObject;                      // Verification
    FVector2D mViewSizePx = FVector2D::ZeroVector;          // Canvas size of the last hits
    FDelegateHandle mCombatUpdatedHandle;                   // Binding to the combat update (proxy check)
    double mInfoUntilS = 0.0;                               // Real time the info card hides
    double mHintUntilS = 0.0;                               // Real time the hint hides
    float mUiScale = 1.0f;                                  // UI scale of the last hits
    mutable int32 mShipSlotHint = INDEX_NONE;               // CPU ship view slot of the last ship looked up
    uint32 mFollowOrderSerial = 0;                          // Ship order serial of the last Follow order
    int32 mExecutedCount = 0;
    int32 mRadialOpenCount = 0;
    int32 mRadialCancelCount = 0;
    ESOLAction mHintAction = ESOLAction::Count;
    ESOLAction mLastExecutedAction = ESOLAction::Count;
    bool mIsProxyRegistered = false;                        // TargetProxy is a targeting candidate now
};
