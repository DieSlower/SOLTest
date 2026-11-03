/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLInteractionSubsystem.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Game/SOLGameMode.h"
#include "Interaction/SOLDroppedTargetTargetable.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapPicking.h"
#include "Menu/SOLMenuText.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcTierProcessor.h"
#include "SOLTest.h"
#include "Ship/SOLShipSubsystem.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLOverlayIconLayer.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLKepler.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Logs the running (or last) chase's status in one line (sol.Chase Status)
    void LogChaseStatus(const USOLShipSubsystem& ships)
    {
        const FSOLChaseStatus status = ships.GetChaseStatus();
        const FSOLGMeterState meter = ships.GetGMeter();
        UE_LOG(LogSOL, Log, TEXT("sol.Chase: %s %s%s: activity %s, limit %.2f G, load %.2f G, meter %.3f (floor %.2f, "
            "resume %.2f%s), burst %.2f G, brake %.2f G, target %.0f m, tail %.0f m (trail %.0f m), closing %.1f m/s, "
            "T %.1f s"), status.bActive ? TEXT("chasing") : TEXT("not chasing"), *status.TargetName.ToString(),
            status.bTargetLost ? TEXT(" (target lost)") : TEXT(""), SOLHudFormat::ChaseActivityName(status.Activity),
            status.LimitG, ships.GetGLoadG(), meter.Meter, status.FloorMeter, status.ResumeMeter,
            status.bCooling ? TEXT(", cooling") : TEXT(""), status.BurstG, status.BrakePlanG, status.TargetDistanceM,
            status.AimDistanceM, status.TrailM, status.ClosingSpeedMps,
            status.TimeToInterceptS < DBL_MAX ? status.TimeToInterceptS : -1.0);
    }

    // sol.Chase <Body [Name]|Target|Drop|Off|Status>: Chase Down for verification and smokes (SDD 32 section 3.8)
    FAutoConsoleCommandWithWorldAndArgs GChaseCommand(TEXT("sol.Chase"),
        TEXT("sol.Chase <Body [Name]|<BodyName>|Target|Drop|Off|Status>: Chase Down a body (default the Moon), the "
            "selected target, the most recent dropped target (dropping one ahead when none is alive), or stop / log"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            USOLInteractionSubsystem* interaction = world != nullptr
                ? world->GetSubsystem<USOLInteractionSubsystem>() : nullptr;
            USOLShipSubsystem* ships = world != nullptr ? world->GetSubsystem<USOLShipSubsystem>() : nullptr;
            const USOLBodyRegistrySubsystem* registry = world != nullptr
                ? world->GetSubsystem<USOLBodyRegistrySubsystem>() : nullptr;
            USOLCombatSubsystem* combat = world != nullptr ? world->GetSubsystem<USOLCombatSubsystem>() : nullptr;
            if (interaction == nullptr || ships == nullptr || registry == nullptr || args.Num() == 0)
            {
                UE_LOG(LogSOL, Warning, TEXT("sol.Chase: usage sol.Chase <Body [Name]|Target|Drop|Off|Status>"));
                return;
            }
            const FString& mode = args[0];
            if (mode.Equals(TEXT("Off"), ESearchCase::IgnoreCase))
            {
                ships->DisengageChase();
                return;
            }
            if (mode.Equals(TEXT("Status"), ESearchCase::IgnoreCase))
            {
                LogChaseStatus(*ships);
                return;
            }

            // The selected body or dropped target; the latest drop (one dropped ahead if none is alive); or a body
            bool bEngaged = false;
            if (mode.Equals(TEXT("Target"), ESearchCase::IgnoreCase))
            {
                const FSOLObjectRef selected = interaction->GetSelectedObject();
                FSOLChaseOrder order;
                order.BodyIndex = selected.Kind == ESOLObjectKind::Body ? selected.Index : INDEX_NONE;
                bEngaged = selected.Kind == ESOLObjectKind::DroppedTarget
                    ? interaction->ChaseDroppedTarget(selected.Index)
                    : order.BodyIndex != INDEX_NONE && ships->EngageChase(order);
            }
            else if (mode.Equals(TEXT("Drop"), ESearchCase::IgnoreCase))
            {
                int32 slot = combat != nullptr ? combat->GetLastDroppedTargetSlot() : INDEX_NONE;
                const TConstArrayView<bool> active = combat != nullptr ? combat->GetTargetActive()
                    : TConstArrayView<bool>();
                if (combat != nullptr && (!active.IsValidIndex(slot) || !active[slot]) && ships->HasPlayerShip())
                {
                    slot = combat->DropTarget(ships->GetUniversePositionM(),
                        SOLRender::EclipticToUnreal(ships->GetState().Orientation.GetForwardVector()));
                }
                bEngaged = interaction->ChaseDroppedTarget(slot);
            }
            else
            {
                const bool bBodyKeyword = mode.Equals(TEXT("Body"), ESearchCase::IgnoreCase);
                const FString name = bBodyKeyword ? (args.Num() > 1 ? args[1] : FString(SOL::BodyNames::MOON)) : mode;
                FSOLChaseOrder order;
                order.BodyIndex = registry->GetRegistry().FindByName(FName(*name));
                bEngaged = order.BodyIndex != INDEX_NONE && ships->EngageChase(order);
            }
            if (!bEngaged)
            {
                UE_LOG(LogSOL, Warning, TEXT("sol.Chase: no chase started for '%s'"), *mode);
            }
        }));
}

//////////////////////////////////////////////////////////////////////////
// Resolves the subsystems the orders go through, caches the named asteroids and reserves the hit buffer
void USOLInteractionSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    Ships = collection.InitializeDependency<USOLShipSubsystem>();
    Targeting = collection.InitializeDependency<USOLTargetingSubsystem>();
    Jump = collection.InitializeDependency<USOLJumpSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Combat = collection.InitializeDependency<USOLCombatSubsystem>();
    Npcs = collection.InitializeDependency<USOLNpcSubsystem>();
    SimClock = collection.InitializeDependency<USOLSimClockSubsystem>();
    Super::Initialize(collection);

    FSOLOverlayIconLayer::BuildNamedAsteroids(mNamedAsteroids);
    mHits.Reset(SOL::Overlay::MAX_ICONS);
    TargetProxy = NewObject<USOLDroppedTargetTargetable>(this);
    ChaseProxy = NewObject<USOLDroppedTargetTargetable>(this);
    if (Combat != nullptr)
    {
        mCombatUpdatedHandle = Combat->OnCombatUpdated().AddUObject(this,
            &USOLInteractionSubsystem::HandleCombatUpdated);
    }
}

//////////////////////////////////////////////////////////////////////////
// Unregisters the dropped-target proxy and unhooks from the combat update
void USOLInteractionSubsystem::Deinitialize()
{
    ReleaseProxy();
    if (Combat != nullptr && mCombatUpdatedHandle.IsValid())
    {
        Combat->OnCombatUpdated().Remove(mCombatUpdatedHandle);
    }
    mCombatUpdatedHandle.Reset();
    mRadial = FSOLRadialMenuState();
    mHits.Reset();
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLInteractionSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Copies the icons the HUD drew this frame (the buffer is reserved for MAX_ICONS and never grown)
void USOLInteractionSubsystem::SetIconHits(TConstArrayView<FSOLIconHit> hits, const FVector2D& viewSizePx,
    const float uiScale)
{
    const int32 count = FMath::Min(hits.Num(), SOL::Overlay::MAX_ICONS);
    mHits.SetNumUninitialized(count, EAllowShrinking::No);
    for (int32 index = 0; index < count; ++index)
    {
        mHits[index] = hits[index];
    }
    mViewSizePx = viewSizePx;
    mUiScale = uiScale > 0.0f ? uiScale : 1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Forgets the icons
void USOLInteractionSubsystem::ClearIconHits()
{
    mHits.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Updates the hovered icon from a free cursor; the open menu's object keeps the highlight
void USOLInteractionSubsystem::UpdateHover(const FVector2D& cursorPx, const bool bCursorFree)
{
    if (mRadial.bOpen)
    {
        // The menu's object left the icons (off screen, overlays off, HUD hidden): nothing would show what a release
        // chooses, so it is cancelled
        bool bShown = false;
        for (const FSOLIconHit& hit : mHits)
        {
            bShown = bShown || hit.Object == mRadial.Object;
        }
        if (bShown)
        {
            mHovered = mRadial.Object;
            return;
        }
        CancelRadial();
    }
    const int32 hit = bCursorFree ? SOLRadialMenu::HitTestIcons(mHits, cursorPx,
        SOL::Interact::ICON_HIT_SLACK_PX * mUiScale) : INDEX_NONE;
    mHovered = hit != INDEX_NONE ? mHits[hit].Object : FSOLObjectRef();
}

//////////////////////////////////////////////////////////////////////////
// Left press with a free cursor: opens the radial menu on the icon under the cursor
bool USOLInteractionSubsystem::PressPrimary(const FVector2D& cursorPx)
{
    if (mRadial.bOpen)
    {
        return true;
    }
    // Only an object hovered at the last update opens a menu: its icon was drawn (a faded one is drawn while
    // hovered), so a click on an invisible spot still fires
    const int32 hit = SOLRadialMenu::HitTestIcons(mHits, cursorPx, SOL::Interact::ICON_HIT_SLACK_PX * mUiScale);
    if (hit == INDEX_NONE || mHits[hit].Object != mHovered || !BuildActions(mHits[hit].Object, mRadialActions))
    {
        return false;
    }
    const FVector2D centerPx = SOLRadialMenu::PlaceCenter(mHits[hit].CenterPx, mViewSizePx,
        SOL::Interact::RADIAL_MARGIN_PX * mUiScale);
    if (SOLRadialMenu::Open(mRadial, mHits[hit].Object, centerPx, mRadialActions.Num) != ESOLRadialOutcome::Opened)
    {
        return false;
    }
    mHovered = mRadial.Object;
    ++mRadialOpenCount;
    UE_LOG(LogSOL, Log, TEXT("Interaction: radial menu open on %d:%d (%d actions)"),
        static_cast<int32>(mRadial.Object.Kind), mRadial.Object.Index, mRadial.Count);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Pointer moved while the radial menu is open: updates its highlighted sector
void USOLInteractionSubsystem::MovePointer(const FVector2D& cursorPx)
{
    SOLRadialMenu::Move(mRadial, cursorPx, SOL::Interact::RADIAL_INNER_PX * mUiScale);
}

//////////////////////////////////////////////////////////////////////////
// Left release: confirms the sector under the pointer (runs its action) or cancels
bool USOLInteractionSubsystem::ReleasePrimary(const FVector2D& cursorPx)
{
    if (!mRadial.bOpen)
    {
        return false;
    }
    bool enabled[SOL::Interact::MAX_ACTIONS] = {};
    for (int32 index = 0; index < mRadialActions.Num; ++index)
    {
        enabled[index] = mRadialActions.Entries[index].IsEnabled();
    }
    const FSOLObjectRef object = mRadial.Object;
    int32 sector = INDEX_NONE;
    const ESOLRadialOutcome outcome = SOLRadialMenu::Release(mRadial, cursorPx,
        SOL::Interact::RADIAL_INNER_PX * mUiScale, MakeArrayView(enabled, mRadialActions.Num), sector);
    if (outcome == ESOLRadialOutcome::Confirmed && sector >= 0 && sector < mRadialActions.Num)
    {
        Execute(object, mRadialActions.Entries[sector].Action);
    }
    else
    {
        ++mRadialCancelCount;
        UE_LOG(LogSOL, Log, TEXT("Interaction: radial menu cancelled"));
    }
    mHovered = FSOLObjectRef();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Closes the radial menu without running anything
bool USOLInteractionSubsystem::CancelRadial()
{
    if (SOLRadialMenu::Cancel(mRadial) != ESOLRadialOutcome::Cancelled)
    {
        return false;
    }
    ++mRadialCancelCount;
    mHovered = FSOLObjectRef();
    UE_LOG(LogSOL, Log, TEXT("Interaction: radial menu cancelled"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the action-table type of an object
ESOLObjectType USOLInteractionSubsystem::GetObjectType(const FSOLObjectRef& object) const
{
    switch (object.Kind)
    {
    case ESOLObjectKind::Body:
    {
        if (BodyRegistry == nullptr || object.Index >= BodyRegistry->GetRegistry().Num())
        {
            return ESOLObjectType::Count;
        }
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        return SOLActionTable::TypeForCategory(SOLOverlayIcons::ClassifyBody(object.Index,
            registry.GetParent(object.Index), SOL::Overlay::LAST_PLANET_INDEX));
    }
    case ESOLObjectKind::Asteroid:
        return mNamedAsteroids.IsValidIndex(object.Index) ? ESOLObjectType::Asteroid : ESOLObjectType::Count;
    case ESOLObjectKind::DroppedTarget:
        return object.Index >= 0 ? ESOLObjectType::DroppedTarget : ESOLObjectType::Count;
    case ESOLObjectKind::Ship:
        return object.Index >= 0 ? ESOLObjectType::Ship : ESOLObjectType::Count;
    default:
        return ESOLObjectType::Count;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the targeting candidate index of an object, or INDEX_NONE
int32 USOLInteractionSubsystem::FindCandidate(const FSOLObjectRef& object) const
{
    if (Targeting == nullptr)
    {
        return INDEX_NONE;
    }
    if (object.Kind == ESOLObjectKind::Body)
    {
        return object.Index;
    }
    if (object.Kind == ESOLObjectKind::DroppedTarget && mIsProxyRegistered && TargetProxy != nullptr
        && TargetProxy->GetSlot() == object.Index && TargetProxy->IsBoundTargetAlive())
    {
        return Targeting->FindTargetableCandidate(TargetProxy);
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Gathers the availability facts of one object
FSOLActionContext USOLInteractionSubsystem::BuildContext(const FSOLObjectRef& object) const
{
    FSOLActionContext context;
    context.bHasShip = Ships != nullptr && Ships->HasPlayerShip();
    context.bPilotHasControl = context.bHasShip && Ships->PilotHasControl();
    context.bJumpWarping = Jump != nullptr && Jump->IsWarping();

    // Targetable: every body; a dropped target while its slot is active (through the proxy)
    if (object.Kind == ESOLObjectKind::Body)
    {
        context.bIsTargetable = Targeting != nullptr;
    }
    else if (object.Kind == ESOLObjectKind::DroppedTarget && Combat != nullptr)
    {
        const TConstArrayView<bool> active = Combat->GetTargetActive();
        context.bIsTargetable = Targeting != nullptr && active.IsValidIndex(object.Index) && active[object.Index];
    }
    const int32 candidate = FindCandidate(object);
    if (candidate != INDEX_NONE)
    {
        context.bIsSelected = Targeting->GetSelectedIndex() == candidate;
        context.bIsFrameLocked = Targeting->GetLockedIndex() == candidate;
    }

    // The autopilot's order: a body order is Move To, the station-keeping point order this subsystem issued is Follow
    if (object.Kind == ESOLObjectKind::Body && context.bHasShip && Ships->GetFlightMode() == ESOLFlightMode::Auto
        && Ships->GetAutopilotBodyIndex() == object.Index)
    {
        const bool bPointOrder = Ships->IsAutopilotPointOrder();
        context.bIsMoveTarget = !bPointOrder;
        context.bIsFollowed = bPointOrder && Ships->GetAutopilotOrderSerial() == mFollowOrderSerial;
    }

    // The chase: the ship's running chase has this object (a body index, or the dropped target's slot)
    if (context.bHasShip && Ships->IsChasing())
    {
        if (object.Kind == ESOLObjectKind::Body)
        {
            context.bIsChased = Ships->GetChaseOrder().BodyIndex == object.Index;
        }
        else if (object.Kind == ESOLObjectKind::DroppedTarget)
        {
            context.bIsChased = GetChasedDroppedTargetSlot() == object.Index;
        }
    }
    return context;
}

//////////////////////////////////////////////////////////////////////////
// Fills an object's actions with their availability now
bool USOLInteractionSubsystem::BuildActions(const FSOLObjectRef& object, FSOLActionList& outList) const
{
    const ESOLObjectType type = GetObjectType(object);
    SOLActionTable::Build(type, BuildContext(object), outList);
    return type != ESOLObjectType::Count && outList.Num > 0;
}

//////////////////////////////////////////////////////////////////////////
// Targets an object: selects its candidate (a dropped target through the proxy)
bool USOLInteractionSubsystem::TargetObject(const FSOLObjectRef& object)
{
    if (Targeting == nullptr)
    {
        return false;
    }
    if (object.Kind == ESOLObjectKind::Body)
    {
        return Targeting->SelectCandidate(object.Index);
    }
    if (object.Kind != ESOLObjectKind::DroppedTarget || Combat == nullptr || TargetProxy == nullptr)
    {
        return false;
    }

    // One proxy: re-bound to this slot's current drop (the old one's registration goes first)
    const TConstArrayView<uint32> serials = Combat->GetTargetSpawnSerials();
    if (!serials.IsValidIndex(object.Index))
    {
        return false;
    }
    const TConstArrayView<bool> active = Combat->GetTargetActive();
    if (!active.IsValidIndex(object.Index) || !active[object.Index])
    {
        return false;
    }
    if (FindCandidate(object) == INDEX_NONE)
    {
        // The one proxy moves to this drop (a lock on the previous one ends with it)
        ReleaseProxy();
        FString name = SOLMenuText::INTERACT_TARGET_NAME_PREFIX;
        name.AppendInt(object.Index + 1);
        TargetProxy->Bind(Combat, object.Index, serials[object.Index], FName(*name));
        Targeting->RegisterTargetable(TScriptInterface<ISOLTargetable>(TargetProxy.Get()));
        mIsProxyRegistered = true;
    }
    return Targeting->SelectCandidate(Targeting->FindTargetableCandidate(TargetProxy));
}

//////////////////////////////////////////////////////////////////////////
// Starts a Chase Down on a dropped target through the chase proxy; false when the slot is empty or the order failed
bool USOLInteractionSubsystem::ChaseDroppedTarget(const int32 slot)
{
    if (Combat == nullptr || Ships == nullptr || ChaseProxy == nullptr)
    {
        return false;
    }
    const TConstArrayView<bool> active = Combat->GetTargetActive();
    const TConstArrayView<uint32> serials = Combat->GetTargetSpawnSerials();
    if (!active.IsValidIndex(slot) || !active[slot] || !serials.IsValidIndex(slot))
    {
        return false;
    }

    // The proxy follows this drop only (its spawn serial), named as the menus name it
    FString name = SOLMenuText::INTERACT_TARGET_NAME_PREFIX;
    name.AppendInt(slot + 1);
    ChaseProxy->Bind(Combat, slot, serials[slot], FName(*name));
    FSOLChaseOrder order;
    order.Targetable = TWeakInterfacePtr<ISOLTargetable>(ChaseProxy.Get());
    return Ships->EngageChase(order);
}

//////////////////////////////////////////////////////////////////////////
// Returns the chase proxy's slot while the running chase is on a dropped target, else INDEX_NONE
int32 USOLInteractionSubsystem::GetChasedDroppedTargetSlot() const
{
    if (Ships == nullptr || ChaseProxy == nullptr || !Ships->IsChasing()
        || Ships->GetChaseOrder().Targetable.GetObject() != ChaseProxy.Get())
    {
        return INDEX_NONE;
    }
    return ChaseProxy->GetSlot();
}

//////////////////////////////////////////////////////////////////////////
// Orders the autopilot to keep station on a body at its holdable standoff, on the ship's side
bool USOLInteractionSubsystem::FollowBody(const int32 bodyIndex)
{
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    if (!cache.PositionsM.IsValidIndex(bodyIndex))
    {
        return false;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const double standoffM = SOLFlight::AutopilotStandoffM(registry.GetRadiusM(bodyIndex), registry.GetGM(bodyIndex),
        SOLNewtonian::AutopilotAccelLimitMps2(Ships->GetFlightParams().PilotLimits, Ships->GetGMeter()));
    FSOLAutopilotPointOrder order;
    order.BodyIndex = bodyIndex;
    order.OffsetM = SOLActionOrders::ArrivalOffset(cache.PositionsM[bodyIndex], Ships->GetState().PositionM, standoffM);
    order.PassSpeedMps = 0.0;
    order.CaptureRadiusM = 0.0;
    order.bStayAutoOnArrival = true;
    if (!Ships->EngageAutopilotPoint(order))
    {
        return false;
    }
    mFollowOrderSerial = Ships->GetAutopilotOrderSerial();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Starts a jump next to a body: a body-referenced pick at JumpArrivalDistanceM on the ship's side (ecliptic axes)
bool USOLInteractionSubsystem::JumpToBody(const int32 bodyIndex)
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    if (Jump == nullptr || bodyIndex < 0 || bodyIndex >= registry.Num())
    {
        return false;
    }
    const FVector3d offsetM = SOLActionOrders::ArrivalOffset(registry.GetPositionM(bodyIndex),
        Ships->GetUniversePositionM(), SOLActionOrders::JumpArrivalDistanceM(registry.GetRadiusM(bodyIndex)));
    FSOLMapPickState pick;
    pick.bHasReference = true;
    pick.bReferenceIsBody = true;
    pick.ReferenceBodyIndex = bodyIndex;
    pick.bPlanarLocked = true;
    pick.PlanarOffsetXM = offsetM.X;
    pick.PlanarOffsetYM = offsetM.Y;
    pick.bHeightLocked = true;
    pick.HeightOffsetZM = offsetM.Z;
    return Jump->StartJump(pick);
}

//////////////////////////////////////////////////////////////////////////
// Runs an action on an object when it is available now
bool USOLInteractionSubsystem::Execute(const FSOLObjectRef& object, const ESOLAction action)
{
    const ESOLObjectType type = GetObjectType(object);
    const ESOLActionBlock block = SOLActionTable::Evaluate(type, action, BuildContext(object));
    if (block != ESOLActionBlock::None || Ships == nullptr || BodyRegistry == nullptr)
    {
        UE_LOG(LogSOL, Log, TEXT("Interaction: %s on %d:%d refused (%s)"), SOLActionTable::ActionLabel(action),
            static_cast<int32>(object.Kind), object.Index, SOLActionTable::BlockText(block));
        return false;
    }

    // Each action through the API that already owns it; only bodies take the autopilot and jump orders in v1
    bool bDone = false;
    const bool bBody = object.Kind == ESOLObjectKind::Body;
    switch (action)
    {
    case ESOLAction::Target:
        bDone = TargetObject(object);
        break;
    case ESOLAction::Info:
        mInfoObject = object;
        mInfoUntilS = GetNowS() + SOL::Interact::INFO_CARD_S;
        bDone = true;
        break;
    case ESOLAction::MoveTo:
        bDone = bBody && Ships->EngageAutopilot(object.Index);
        break;
    case ESOLAction::Follow:
        bDone = bBody && FollowBody(object.Index);
        break;
    case ESOLAction::ChaseDown:
        // Does not touch the selection (SDD 32 Q4): a dropped target goes through the chase proxy, a body by index
        if (object.Kind == ESOLObjectKind::DroppedTarget)
        {
            bDone = ChaseDroppedTarget(object.Index);
        }
        else if (bBody)
        {
            FSOLChaseOrder order;
            order.BodyIndex = object.Index;
            bDone = Ships->EngageChase(order);
        }
        break;
    case ESOLAction::JumpTo:
        bDone = bBody && JumpToBody(object.Index);
        break;
    case ESOLAction::MatchSpeed:
        if (bBody)
        {
            Targeting->LockToBodyIndex(object.Index);
            bDone = true;
        }
        break;
    case ESOLAction::Attack:
        bDone = TargetObject(object);
        if (bDone)
        {
            mHintAction = ESOLAction::Attack;
            mHintObject = object;
            mHintUntilS = GetNowS() + SOL::Interact::HINT_S;
        }
        break;
    default:
        break;
    }
    UE_LOG(LogSOL, Log, TEXT("Interaction: %s on %d:%d %s"), SOLActionTable::ActionLabel(action),
        static_cast<int32>(object.Kind), object.Index, bDone ? TEXT("done") : TEXT("failed"));
    if (bDone)
    {
        ++mExecutedCount;
        mLastExecutedAction = action;
        mLastExecutedObject = object;
    }
    return bDone;
}

//////////////////////////////////////////////////////////////////////////
// Fills an object's facts; false for an unknown or vanished object
bool USOLInteractionSubsystem::GetObjectInfo(const FSOLObjectRef& object, FSOLObjectInfo& outInfo) const
{
    outInfo = FSOLObjectInfo();
    outInfo.Type = GetObjectType(object);
    if (outInfo.Type == ESOLObjectType::Count || BodyRegistry == nullptr)
    {
        return false;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    switch (object.Kind)
    {
    case ESOLObjectKind::Body:
    {
        const int32 parent = registry.GetParent(object.Index);
        outInfo.Name = registry.GetName(object.Index);
        outInfo.ParentName = outInfo.Type == ESOLObjectType::Moon && parent >= 0 ? registry.GetName(parent) : NAME_None;
        outInfo.PositionM = registry.GetPositionM(object.Index);
        outInfo.VelocityMps = registry.GetVelocityMps(object.Index);
        outInfo.RadiusM = registry.GetRadiusM(object.Index);
        return true;
    }
    case ESOLObjectKind::Asteroid:
    {
        // Sun-relative elements at the sim time, as the icon layer places it
        if (SimClock == nullptr || registry.Num() == 0)
        {
            return false;
        }
        const FSOLAsteroidDef& asteroid = mNamedAsteroids[object.Index];
        const FSOLOrbitState state = SOLKepler::ElementsToState(asteroid.Elements.AtCenturies(
            SimClock->GetClock().GetCenturiesSinceJ2000()), registry.GetGM(0), 0.0);
        outInfo.Name = asteroid.Name;
        outInfo.PositionM = registry.GetPositionM(0) + state.PositionM;
        outInfo.VelocityMps = registry.GetVelocityMps(0) + state.VelocityMps;
        outInfo.RadiusM = asteroid.RadiusM;
        return true;
    }
    case ESOLObjectKind::DroppedTarget:
    {
        if (Combat == nullptr || !Combat->GetTargetActive().IsValidIndex(object.Index)
            || !Combat->GetTargetActive()[object.Index])
        {
            return false;
        }
        outInfo.Name = TargetProxy != nullptr && TargetProxy->GetSlot() == object.Index
            ? TargetProxy->GetTargetInfo().Name : NAME_None;
        outInfo.PositionM = Combat->GetTargetPositionsM()[object.Index];
        Combat->GetTargetVelocityMps(object.Index, outInfo.VelocityMps);
        outInfo.RadiusM = Combat->GetTargetRadiusM();
        outInfo.ShieldFraction = Combat->GetTargetShieldFractions()[object.Index];
        outInfo.HealthFraction = Combat->GetTargetHealthFractions()[object.Index];
        return true;
    }
    case ESOLObjectKind::Ship:
    {
        // The population index is stable; the CPU ship view is searched (only while a card or menu shows)
        if (Npcs == nullptr)
        {
            return false;
        }

        // The slot found last time is checked first (O(1) while the population is unchanged), the view scanned only
        // when the ship moved in it
        const TConstArrayView<FSOLNpcCpuShip> ships = Npcs->GetCpuShips();
        if (!ships.IsValidIndex(mShipSlotHint) || ships[mShipSlotHint].NpcIndex != object.Index)
        {
            mShipSlotHint = INDEX_NONE;
            for (int32 slot = 0; slot < ships.Num(); ++slot)
            {
                if (ships[slot].NpcIndex == object.Index)
                {
                    mShipSlotHint = slot;
                    break;
                }
            }
        }
        if (mShipSlotHint == INDEX_NONE || ships[mShipSlotHint].bHidden != 0)
        {
            return false;
        }
        outInfo.PositionM = ships[mShipSlotHint].PositionM;
        return true;
    }
    default:
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the selected target as an object: a body, or the dropped target the proxy stands for
FSOLObjectRef USOLInteractionSubsystem::GetSelectedObject() const
{
    const int32 selected = Targeting != nullptr ? Targeting->GetSelectedIndex() : INDEX_NONE;
    if (selected == INDEX_NONE)
    {
        return FSOLObjectRef();
    }
    if (BodyRegistry != nullptr && selected < BodyRegistry->GetRegistry().Num())
    {
        return FSOLObjectRef{ ESOLObjectKind::Body, selected };
    }
    return mIsProxyRegistered && TargetProxy != nullptr && Targeting->FindTargetableCandidate(TargetProxy) == selected
        ? FSOLObjectRef{ ESOLObjectKind::DroppedTarget, TargetProxy->GetSlot() } : FSOLObjectRef();
}

//////////////////////////////////////////////////////////////////////////
// Returns the object whose info card shows now, or an invalid reference once it timed out
FSOLObjectRef USOLInteractionSubsystem::GetInfoObject() const
{
    return mInfoObject.IsValid() && GetNowS() < mInfoUntilS ? mInfoObject : FSOLObjectRef();
}

//////////////////////////////////////////////////////////////////////////
// Returns the action whose HUD hint shows now, or Count when none
ESOLAction USOLInteractionSubsystem::GetHintAction() const
{
    return GetNowS() < mHintUntilS ? mHintAction : ESOLAction::Count;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the "target lost" note shows now: within its window of the loss and while no order or chase runs
bool USOLInteractionSubsystem::IsChaseTargetLostNoteShown() const
{
    if (Ships == nullptr || Ships->IsChasing() || Ships->GetAutopilotBodyIndex() != INDEX_NONE)
    {
        return false;
    }
    const double lostAtS = Ships->GetChaseTargetLostRealS();
    return lostAtS >= 0.0 && GetNowS() - lostAtS < SOL::Chase::TARGET_LOST_NOTE_S;
}

//////////////////////////////////////////////////////////////////////////
// Drops the proxy's registration when its target is gone or no longer selected or locked (one slot per update)
void USOLInteractionSubsystem::HandleCombatUpdated()
{
    if (!mIsProxyRegistered || TargetProxy == nullptr || Targeting == nullptr)
    {
        return;
    }
    const int32 candidate = Targeting->FindTargetableCandidate(TargetProxy);
    const bool bInUse = candidate != INDEX_NONE
        && (Targeting->GetSelectedIndex() == candidate || Targeting->GetLockedIndex() == candidate);
    if (!TargetProxy->IsBoundTargetAlive() || !bInUse)
    {
        ReleaseProxy();
    }
}

//////////////////////////////////////////////////////////////////////////
// Unregisters the proxy from targeting (a selection or lock on it is cleared by the targeting subsystem)
void USOLInteractionSubsystem::ReleaseProxy()
{
    if (mIsProxyRegistered && Targeting != nullptr && TargetProxy != nullptr)
    {
        Targeting->UnregisterTargetable(TScriptInterface<ISOLTargetable>(TargetProxy.Get()));
    }
    mIsProxyRegistered = false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the real time used for the info card and hints
double USOLInteractionSubsystem::GetNowS() const
{
    const UWorld* world = GetWorld();
    return world != nullptr ? world->GetRealTimeSeconds() : 0.0;
}
