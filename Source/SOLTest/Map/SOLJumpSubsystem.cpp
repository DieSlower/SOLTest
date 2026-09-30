/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLJumpSubsystem.h"

#include "Game/SOLGameMode.h"
#include "Map/SOLWarpCurve.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "Universe/SOLAnchor.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Engine/World.h"

//////////////////////////////////////////////////////////////////////////
// Resolves the anchor, registry and ship subsystems and subscribes to the end of the ship step
void USOLJumpSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    AnchorSubsystem = collection.InitializeDependency<USOLAnchorSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Ships = collection.InitializeDependency<USOLShipSubsystem>();
    Targeting = collection.InitializeDependency<USOLTargetingSubsystem>();
    Super::Initialize(collection);

    // After the ship step, not on OnBodiesUpdated beside it (multicast order is not a contract): the arrival state is
    // then this frame's (bodies and ship at the same time) and only the anchor update follows it
    if (Ships != nullptr)
    {
        mShipsSteppedHandle = Ships->OnShipsStepped().AddUObject(this, &USOLJumpSubsystem::HandleShipsStepped);
    }

    // The end of every universe update, stepped or not: a frame without a ship step aborts a running sequence
    if (AnchorSubsystem != nullptr)
    {
        mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
            &USOLJumpSubsystem::HandleUniverseUpdated);
    }
}

//////////////////////////////////////////////////////////////////////////
// Unsubscribes and drops any running sequence
void USOLJumpSubsystem::Deinitialize()
{
    if (Ships != nullptr)
    {
        Ships->OnShipsStepped().Remove(mShipsSteppedHandle);
    }
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mShipsSteppedHandle.Reset();
    mUniverseUpdatedHandle.Reset();
    mIsWarping = false;
    mWasSteppedThisFrame = false;
    mElapsedS = 0.0;
    AnchorSubsystem = nullptr;
    BodyRegistry = nullptr;
    Ships = nullptr;
    Targeting = nullptr;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLJumpSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLJumpSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Starts the warp sequence to a pick with at least the planar offset locked; false otherwise or while one runs
bool USOLJumpSubsystem::StartJump(const FSOLMapPickState& pick)
{
    if (mIsWarping || !pick.bHasReference || !pick.bPlanarLocked || Ships == nullptr || !Ships->HasPlayerShip())
    {
        return false;
    }

    // The reference (what the arrival matches) is fixed now; only its position is re-read at the end
    mPick = pick;
    mElapsedS = 0.0;
    mIsWarping = true;
    UE_LOG(LogSOL, Log, TEXT("Jump %s: warp started (%.1f s) to reference %s (body index %d) + (%.6g, %.6g, %.6g) m"),
        *GetName(), SOL::WARP_DURATION_S, mPick.bReferenceIsBody ? TEXT("body") : TEXT("ship"),
        mPick.ReferenceBodyIndex, mPick.PlanarOffsetXM, mPick.PlanarOffsetYM,
        mPick.bHeightLocked ? mPick.HeightOffsetZM : 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the chase camera FOV for this point of the sequence, pulsing from baseFovDeg to WARP_PEAK_FOV_DEG
double USOLJumpSubsystem::ComputeFovDeg(const double baseFovDeg) const
{
    return mIsWarping ? SOLWarpCurve::ComputeFovDeg(mElapsedS, SOL::WARP_DURATION_S, baseFovDeg,
        SOL::WARP_PEAK_FOV_DEG) : baseFovDeg;
}

//////////////////////////////////////////////////////////////////////////
// Returns the radial-streak intensity for this point of the sequence (0 when none runs)
double USOLJumpSubsystem::ComputeStreakIntensity() const
{
    return mIsWarping ? SOLWarpCurve::ComputeStreakIntensity(mElapsedS, SOL::WARP_DURATION_S,
        SOL::WARP_PEAK_STREAK_INTENSITY) : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Advances the sequence on real time and completes it once WARP_DURATION_S has passed
void USOLJumpSubsystem::HandleShipsStepped(const float realDeltaSeconds)
{
    mWasSteppedThisFrame = true;
    if (!mIsWarping)
    {
        return;
    }
    mElapsedS += realDeltaSeconds;
    if (mElapsedS >= SOL::WARP_DURATION_S)
    {
        CompleteJump();
    }
}

//////////////////////////////////////////////////////////////////////////
// Aborts a running sequence if this frame's universe update ran without a ship step (the ship became invalid)
void USOLJumpSubsystem::HandleUniverseUpdated()
{
    const bool bWasStepped = mWasSteppedThisFrame;
    mWasSteppedThisFrame = false;
    if (!mIsWarping || bWasStepped)
    {
        return;
    }

    // Defensive (not reachable today): the ship step skipped this frame, so the clock would never run out. The pawn
    // and the HUD read IsWarping, so clearing it restores the FOV, the post-process and the flight HUD
    mIsWarping = false;
    mElapsedS = 0.0;
    UE_LOG(LogSOL, Warning, TEXT("Jump %s: warp aborted, the ship step did not run (no valid ship)"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Teleports the ship to the destination recomposed from the reference's current position, matched to its velocity
void USOLJumpSubsystem::CompleteJump()
{
    mIsWarping = false;
    mElapsedS = 0.0;
    if (AnchorSubsystem == nullptr || BodyRegistry == nullptr || Ships == nullptr || !Ships->HasPlayerShip())
    {
        UE_LOG(LogSOL, Warning, TEXT("Jump %s: warp ended without a ship or universe; no arrival"), *GetName());
        return;
    }

    // The reference's position now (this frame's bodies, or the coasting ship): the destination rides along with it.
    // Registry indices are stable for the session; a stale one falls back to the ship, as the map's pick does
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const bool bBodyReference = mPick.bReferenceIsBody && mPick.ReferenceBodyIndex >= 0
        && mPick.ReferenceBodyIndex < registry.Num();
    const FVector3d referenceM = bBodyReference ? registry.GetPositionM(mPick.ReferenceBodyIndex)
        : AnchorSubsystem->GetObserverPositionM();
    const FVector3d destinationM = SOLMapPicking::ComposeDestination(referenceM, mPick.PlanarOffsetXM,
        mPick.PlanarOffsetYM, mPick.bHeightLocked ? mPick.HeightOffsetZM : 0.0, SOL::MAP_PICK_ECLIPTIC_X,
        SOL::MAP_PICK_ECLIPTIC_Y, SOL::MAP_PICK_UP);

    // Velocity: the reference body's; for the ship / empty space, the body a fresh anchor selection (no hysteresis,
    // as a teleport re-picks it) chooses at the destination
    int32 velocityBody = INDEX_NONE;
    if (bBodyReference)
    {
        velocityBody = mPick.ReferenceBodyIndex;
    }
    else
    {
        FSOLAnchorSelector selector;
        velocityBody = selector.Update(registry.GetPositionsM(), destinationM);
    }

    // Orientation and spin are kept; position and velocity go through the ship's teleport path (Unreal-handed frame),
    // which moves the observer and force-snaps the render origin onto it now
    FSOLShipState state = Ships->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(destinationM);
    if (velocityBody != INDEX_NONE)
    {
        state.VelocityMps = SOLRender::EclipticToUnreal(registry.GetVelocityMps(velocityBody));
    }

    // The selection and the M lock refer to the old location: a lock kept on a far body would have the assist thrust
    // toward that body's velocity (scaled by the time-warp carry). A surface-lock (SDD 4) and its auto-engage
    // suppression latch refer to the old location's body too and are released with them; if the arrival is within
    // auto range of a body, the next ship step auto-engages on that one. Cleared before the teleport
    if (Targeting != nullptr)
    {
        Targeting->ClearTarget();
        if (Targeting->IsFrameLocked())
        {
            Targeting->ToggleFrameLock();
        }
    }
    Ships->ClearSurfaceLock();
    Ships->SetState(state);

    mLastArrivalReferenceM = referenceM;
    mLastArrivalPositionM = destinationM;
    mLastArrivalVelocityBodyIndex = velocityBody;
    ++mCompletedJumpCount;
    UE_LOG(LogSOL, Log, TEXT("Jump %s: arrived at (%.9g, %.9g, %.9g) m, velocity matched to %s, anchor %s"),
        *GetName(), destinationM.X, destinationM.Y, destinationM.Z, velocityBody != INDEX_NONE
        ? *registry.GetName(velocityBody).ToString() : TEXT("none"), AnchorSubsystem->GetAnchorIndex() != INDEX_NONE
        ? *registry.GetName(AnchorSubsystem->GetAnchorIndex()).ToString() : TEXT("none"));
}
