/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Targeting/SOLTargetable.h"

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "SOLDroppedTargetTargetable.generated.h"

class USOLCombatSubsystem;

/**
 * Makes one dropped target (a USOLCombatSubsystem slot) a targeting candidate while the interaction menus have it
 * targeted (SDD 22 section 2.4: Target / Attack on a dropped target). USOLInteractionSubsystem owns a single instance,
 * points it at a slot, registers it with USOLTargetingSubsystem and unregisters it once the slot is gone (destroyed,
 * evicted or reused) or the selection moved away, so R / F cycling never walks the whole target pool.
 */
UCLASS()
class SOLTEST_API USOLDroppedTargetTargetable : public UObject, public ISOLTargetable
{
    GENERATED_BODY()

public:

    // Points the proxy at a combat slot and the drop it holds now (its spawn serial), with the name it shows
    void Bind(USOLCombatSubsystem* combat, int32 slot, uint32 spawnSerial, FName name);

    // Returns true while the bound slot still holds the same drop
    bool IsBoundTargetAlive() const;

    // Returns the bound combat slot (INDEX_NONE when unbound)
    int32 GetSlot() const { return mSlot; }

    // Returns the target's snapshot (Unreal-handed universe frame): its position, velocity and hit radius; the last
    // live one after the drop is gone
    virtual FSOLTargetInfo GetTargetInfo() const override;

    // Returns true while the bound slot still holds the same drop (IsBoundTargetAlive)
    virtual bool IsTargetAlive() const override { return IsBoundTargetAlive(); }

    // Returns the combat frame body the drop is held relative to, or INDEX_NONE once it is gone
    virtual int32 GetTargetFrameBodyIndex() const override;

private:

    TWeakObjectPtr<USOLCombatSubsystem> mCombat;    // The combat subsystem that owns the slot
    mutable FSOLTargetInfo mLastInfo;               // Last live snapshot, returned once the drop is gone (until the
                                                    // interaction subsystem unregisters the proxy), so a frame lock
                                                    // never sees a jump to the origin
    FName mName;                                    // Shown by the HUD's target readout
    int32 mSlot = INDEX_NONE;                       // Combat target slot
    uint32 mSpawnSerial = 0;                        // The drop the slot held when bound
};
