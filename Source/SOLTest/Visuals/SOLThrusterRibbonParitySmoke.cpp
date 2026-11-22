/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLThrusterRibbonParitySmoke.h"

#include "Game/SOLGameMode.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterRibbons.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // Script timing
    constexpr double RPSMOKE_SETTLE_S = 3.0;                 // The world and the camera settle
    constexpr int32 RPSMOKE_SETTLE_FRAMES = 60;
    constexpr double RPSMOKE_SHOT_S = 0.6;                   // Each state runs this long and...
    constexpr int32 RPSMOKE_SHOT_FRAMES = 45;                // ...this many frames (temporal history) before its shot
    constexpr double RPSMOKE_HOLD_S = 0.5;                   // The state holds this long after its screenshot

    // The synthetic flames: frozen clock, the legacy sweep's ships turning toward the camera's up
    constexpr double RPSMOKE_NOW_S = 1000.0;
    constexpr double RPSMOKE_TURN_DEG_PER_S = 90.0;

    // The legacy parity sweep (51e step 5): near (160 m), far (25 km, a 300 m plume whose 1.5 m width is far under a
    // pixel, so the 1 px floor draws it) at four quarter-pixel phases, farfat (the far geometry 10+ px wide), multi (40
    // flames, 14 long enough for Niagara); each with the project's anti-aliasing and with r.AntiAliasingMethod 0
    constexpr double RPSMOKE_QUARTER_PX_M = 7.5;

    //////////////////////////////////////////////////////////////////////////
    // Returns one legacy configuration (side view, turning at RPSMOKE_TURN_DEG_PER_S)
    FSOLFlameParityConfig RibbonParityLegacy(const TCHAR* name, const FVector3d& placeM, const double plumeLengthM,
        const float nozzleRadiusM, const int32 antiAliasing, const int32 trails, const TCHAR* niagaraPx)
    {
        FSOLFlameParityConfig config;
        config.Name = name;
        config.PlaceM = placeM;
        config.PlumeLengthM = plumeLengthM;
        config.NozzleRadiusM = nozzleRadiusM;
        config.AntiAliasing = antiAliasing;
        config.Trails = trails;
        config.NiagaraPx = niagaraPx;
        config.TurnDegPerS = RPSMOKE_TURN_DEG_PER_S;
        return config;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the legacy parity sweep
    void RibbonParityLegacyConfigs(TArray<FSOLFlameParityConfig>& out)
    {
        const FVector3d near(160.0, 30.0, 40.0);
        const FVector3d far(25000.0, 4700.0, 6250.0);
        const FVector3d quarter(0.0, RPSMOKE_QUARTER_PX_M, RPSMOKE_QUARTER_PX_M);
        out.Add(RibbonParityLegacy(TEXT("near"), near, 30.0, 1.5f, -1, 1, TEXT("0")));
        for (int32 aa = 0; aa < 2; ++aa)
        {
            for (int32 phase = 0; phase < 4; ++phase)
            {
                out.Add(RibbonParityLegacy(*FString::Printf(TEXT("far-p%d%s"), phase, aa == 0 ? TEXT("") : TEXT("-aa0")),
                    far + quarter * phase, 300.0, 1.5f, aa == 0 ? -1 : 0, 1, TEXT("0")));
            }
        }
        out.Add(RibbonParityLegacy(TEXT("farfat"), far, 300.0, 150.0f, -1, 1, TEXT("0")));
        out.Add(RibbonParityLegacy(TEXT("farfat-aa0"), far, 300.0, 150.0f, 0, 1, TEXT("0")));
        out.Add(RibbonParityLegacy(TEXT("near-aa0"), near, 30.0, 1.5f, 0, 1, TEXT("0")));
        out.Add(RibbonParityLegacy(TEXT("multi"), FVector3d(1000.0, 0.0, 0.0), 10.0, 1.5f, -1, 40, TEXT("50")));
        out.Add(RibbonParityLegacy(TEXT("multi-aa0"), FVector3d(1000.0, 0.0, 0.0), 10.0, 1.5f, 0, 40, TEXT("50")));
    }

    // The seam sweep (-SOLSmokeFlameSeam, R10 seam checks): the NPC flame (30 m hull, r_n 1.5, nozzle 15 m aft) and
    // the player's (20 m hull, r_n 0.8, nozzle 8 m aft) at the distances where the 1 px floor changes the flame, from
    // the side, oblique, chase and front, at War power; the burn states at one near distance; a 127 degree/s turn
    constexpr double RPSMOKE_SEAM_VIEWS_DEG[] = { 90.0, 45.0, 10.0, 170.0 };
    constexpr double RPSMOKE_SEAM_NPC_M[] = { 30.0, 60.0, 100.0, 290.0, 500.0, 1000.0, 2000.0, 2900.0, 10000.0 };
    constexpr double RPSMOKE_SEAM_PLAYER_M[] = { 30.0, 100.0, 150.0, 500.0, 1500.0, 2000.0 };
    constexpr double RPSMOKE_SEAM_TURN_DEG_PER_S = 127.0;
    constexpr double RPSMOKE_BOOST_G = 5.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns one seam configuration: a hull's flame at a main-engine G (War power: G_REF with c 1) at a distance and
    // view angle
    FSOLFlameParityConfig RibbonParitySeam(const TCHAR* hull, const TCHAR* state, const double gMain, const bool bWar,
        const double distanceM, const double viewDeg, const double turnDegPerS)
    {
        const bool bPlayer = FCString::Strcmp(hull, TEXT("player")) == 0;
        const double hullM = bPlayer ? SOL::Thruster::PLAYER_HULL_LENGTH_M : SOL::Thruster::NPC_HULL_LENGTH_M;
        FSOLFlameParityConfig config;
        config.Name = FString::Printf(TEXT("%s-%s-%.0fm-v%.0f%s"), hull, state, distanceM, viewDeg,
            turnDegPerS > 0.0 ? TEXT("-turn") : TEXT(""));
        config.PlaceM = FVector3d(distanceM, 0.0, 0.0);
        config.PlumeLengthM = SOLThruster::PlumeLengthM(hullM, gMain);
        config.Brightness = SOLThruster::PlumeBrightness(gMain);
        config.ColourC = SOLThruster::PlumeColourScalar(gMain, bWar);
        config.NozzleRadiusM = static_cast<float>(bPlayer ? SOL::Thruster::PLAYER_NOZZLE_RADIUS_M
            : SOL::Thruster::NPC_NOZZLE_RADIUS_M);
        config.NozzleAftM = bPlayer ? -SOL::Thruster::PLAYER_NOZZLE_X_M : -SOL::Thruster::NPC_NOZZLE_X_M;
        config.ViewDeg = viewDeg;
        config.TurnDegPerS = turnDegPerS;
        config.AntiAliasing = -1;   // The project's anti-aliasing, as the player sees it
        return config;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the seam sweep
    void RibbonParitySeamConfigs(TArray<FSOLFlameParityConfig>& out)
    {
        const double gRef = SOL::Thruster::G_REF;
        for (const double viewDeg : RPSMOKE_SEAM_VIEWS_DEG)
        {
            for (const double distanceM : RPSMOKE_SEAM_NPC_M)
            {
                out.Add(RibbonParitySeam(TEXT("npc"), TEXT("war"), gRef, true, distanceM, viewDeg, 0.0));
            }
            for (const double distanceM : RPSMOKE_SEAM_PLAYER_M)
            {
                out.Add(RibbonParitySeam(TEXT("player"), TEXT("war"), gRef, true, distanceM, viewDeg, 0.0));
            }
        }
        struct FState { const TCHAR* Name; double G; };
        const FState states[] = { { TEXT("idle"), 0.05 }, { TEXT("1g"), 1.0 }, { TEXT("3g"), 3.0 },
            { TEXT("boost"), RPSMOKE_BOOST_G } };
        for (const FState& state : states)
        {
            for (const double viewDeg : { 90.0, 45.0 })
            {
                out.Add(RibbonParitySeam(TEXT("npc"), state.Name, state.G, false, 100.0, viewDeg, 0.0));
                out.Add(RibbonParitySeam(TEXT("player"), state.Name, state.G, false, 30.0, viewDeg, 0.0));
            }

            // Each burn state near the axis from both ends: the nozzle nearest (v10) and the tail nearest (v170)
            out.Add(RibbonParitySeam(TEXT("player"), state.Name, state.G, false, 30.0, 10.0, 0.0));
            out.Add(RibbonParitySeam(TEXT("player"), state.Name, state.G, false, 30.0, 170.0, 0.0));
        }
        // 51e3 end caps: the nozzle end seen from the front quarter, where the open tube end looked hollow
        for (const double distanceM : { 60.0, 290.0, 1000.0 })
        {
            out.Add(RibbonParitySeam(TEXT("npc"), TEXT("war"), gRef, true, distanceM, 135.0, 0.0));
        }
        for (const double distanceM : { 30.0, 150.0 })
        {
            out.Add(RibbonParitySeam(TEXT("player"), TEXT("war"), gRef, true, distanceM, 135.0, 0.0));
        }
        out.Add(RibbonParitySeam(TEXT("npc"), TEXT("war"), gRef, true, 100.0, 90.0, RPSMOKE_SEAM_TURN_DEG_PER_S));
        out.Add(RibbonParitySeam(TEXT("player"), TEXT("war"), gRef, true, 30.0, 90.0, RPSMOKE_SEAM_TURN_DEG_PER_S));
        out.Add(RibbonParitySeam(TEXT("player"), TEXT("war"), gRef, true, 30.0, 10.0, RPSMOKE_SEAM_TURN_DEG_PER_S));
    }
    // The grid of the multi-trail configurations: 8 columns x 5 rows; trails from RPSMOKE_GRID_LONG_FIRST on are 3x
    // longer (above the Niagara threshold: 14 Niagara components, and pool indices 26-39 across the 32-trail block
    // boundary in the pool shot)
    constexpr int32 RPSMOKE_GRID_COLUMNS = 8;
    constexpr int32 RPSMOKE_GRID_LONG_FIRST = 26;
    constexpr double RPSMOKE_GRID_RIGHT_M = 120.0;
    constexpr double RPSMOKE_GRID_UP_M = 100.0;

    // Shots per configuration: the renderer forced by the Niagara threshold, or the ribbons off
    enum class ERibbonParityShot : uint8 { Niagara, Pool, Off };
    struct FRibbonParityShot
    {
        const TCHAR* Name;
        ERibbonParityShot Kind;
    };
    const FRibbonParityShot RPSMOKE_SHOTS[] = {
        { TEXT("niagara-a"), ERibbonParityShot::Niagara }, { TEXT("pool-a"), ERibbonParityShot::Pool },
        { TEXT("niagara-b"), ERibbonParityShot::Niagara }, { TEXT("pool-b"), ERibbonParityShot::Pool },
        { TEXT("off"), ERibbonParityShot::Off },
    };

    constexpr float RPSMOKE_POOL_ONLY_PX = 1.0e9f;

    //////////////////////////////////////////////////////////////////////////
    // Sets a console variable from a string, logging a name that does not exist
    void RibbonParitySetCvar(const TCHAR* name, const TCHAR* value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
            return;
        }
        UE_LOG(LogSOL, Warning, TEXT("SmokeRibbonParity: console variable %s not found"), name);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship heading (unit) at an emission age: the heading of now turned toward up by the turn rate x age
    FVector3d RibbonParityHeading(const FVector3d& headingNow, const FVector3d& up, const double ageS,
        const double turnDegPerS)
    {
        const double theta = FMath::DegreesToRadians(turnDegPerS) * ageS;
        return (headingNow * FMath::Cos(theta) + up * FMath::Sin(theta)).GetSafeNormal();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns trail i's place in the camera frame (forward, right, up metres): the configuration's, or its grid cell
    FVector3d RibbonParityPlace(const FSOLFlameParityConfig& config, const int32 trail)
    {
        if (config.Trails <= 1)
        {
            return config.PlaceM;
        }
        const int32 column = trail % RPSMOKE_GRID_COLUMNS;
        const int32 row = trail / RPSMOKE_GRID_COLUMNS;
        return config.PlaceM + FVector3d(0.0, (column - 3.5) * RPSMOKE_GRID_RIGHT_M, (row - 2.0) * RPSMOKE_GRID_UP_M);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns flame i's plume length L_p (the grid's long flames are 3x)
    double RibbonParityPlumeLength(const FSOLFlameParityConfig& config, const int32 trail)
    {
        return config.Trails > 1 && trail >= RPSMOKE_GRID_LONG_FIRST ? 3.0 * config.PlumeLengthM : config.PlumeLengthM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a console variable's value as a string ("" when it does not exist)
    FString RibbonParityGetCvar(const TCHAR* name)
    {
        const IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name);
        return cvar != nullptr ? cvar->GetString() : FString();
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes the ribbons' verification feeder (it points at this script)
FSOLThrusterRibbonParitySmoke::~FSOLThrusterRibbonParitySmoke()
{
    if (mRibbons.IsValid())
    {
        mRibbons->SetVerificationFeeder(nullptr);
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the actors, sets up the overrides and the frozen clock
void FSOLThrusterRibbonParitySmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    mRibbons = gameMode != nullptr ? gameMode->GetThrusterRibbons() : nullptr;
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    UE_LOG(LogSOL, Log, TEXT("SmokeRibbonParity: starting (ribbons %s)"), mRibbons.IsValid() ? TEXT("found")
        : TEXT("missing"));
    if (mOrbitLines.IsValid())
    {
        mOrbitLines->BeginVerificationOverride();
        mOrbitLines->SetLinesEnabled(false);
    }
    RibbonParitySetCvar(SOL::SpaceDust::CVAR_ENABLED, TEXT("0"));
    RibbonParitySetCvar(SOL::PLAYER_PLUME_CVAR, TEXT("0"));
    RibbonParitySetCvar(SOL::THRUSTER_RIBBONS_CVAR, TEXT("1"));
    mAntiAliasingDefault = RibbonParityGetCvar(TEXT("r.AntiAliasingMethod"));
    mConfigs.Reset();
    if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_FLAME_SEAM))
    {
        RibbonParitySeamConfigs(mConfigs);
        if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_FLAME_SEAM_AA_OFF))
        {
            for (FSOLFlameParityConfig& config : mConfigs)
            {
                config.AntiAliasing = 0;
            }
        }
        FString only;
        if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_FLAME_SEAM_ONLY, only) && !only.IsEmpty())
        {
            mConfigs.RemoveAll([&only](const FSOLFlameParityConfig& config) { return !config.Name.Contains(only); });
        }
    }
    else
    {
        RibbonParityLegacyConfigs(mConfigs);
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeRibbonParity: %d configurations"), mConfigs.Num());
    if (mRibbons.IsValid())
    {
        mRibbons->SetVerificationClock(RPSMOKE_NOW_S);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLThrusterRibbonParitySmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    UWorld* world = pawn.GetWorld();
    mTotalTimeS += realDeltaSeconds;
    if (!mRibbons.IsValid())
    {
        Check(TEXT("ribbons actor"), false, TEXT("ASOLThrusterRibbons not spawned"));
        Finish(pawn);
        return true;
    }

    // Settle, then capture the camera basis and start the first range
    if (!mIsSettled)
    {
        ++mShotFrames;
        if (mTotalTimeS < RPSMOKE_SETTLE_S || mShotFrames < RPSMOKE_SETTLE_FRAMES)
        {
            return false;
        }
        const APlayerController* controller = world->GetFirstPlayerController();
        if (controller != nullptr && controller->PlayerCameraManager != nullptr)
        {
            const FRotationMatrix basis(controller->PlayerCameraManager->GetCameraRotation());
            mForward = basis.GetUnitAxis(EAxis::X);
            mRight = basis.GetUnitAxis(EAxis::Y);
            mUp = basis.GetUnitAxis(EAxis::Z);
        }
        mIsSettled = true;
        SeedTrail();
        EnterShot();
        mRibbons->SetVerificationFeeder([this](const FVector3d& cameraM) { FeedTrail(cameraM); });
    }

    // A screenshot was requested: hold the state until it is written
    if (mHoldS > 0.0)
    {
        mHoldS -= realDeltaSeconds;
        if (mHoldS > 0.0)
        {
            return false;
        }
        ++mShot;
        if (mShot >= static_cast<int32>(UE_ARRAY_COUNT(RPSMOKE_SHOTS)))
        {
            mShot = 0;
            ++mRange;
            if (mRange >= mConfigs.Num())
            {
                Finish(pawn);
                return true;
            }
        }
        EnterShot();
        if (mShot == 0)
        {
            SeedTrail();
        }
        return false;
    }
    mShotTimeS += realDeltaSeconds;
    ++mShotFrames;
    if (mShotTimeS >= RPSMOKE_SHOT_S && mShotFrames >= RPSMOKE_SHOT_FRAMES)
    {
        ExitShot(world);
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Seeds the synthetic pose ring: FLAME_HISTORY_SAMPLES - 1 poses, pose k emitted k x FLAME_SAMPLE_INTERVAL_S ago while
// the ship pointed along the heading of that age (nozzle exit aft of the centre, exhaust straight back); the feed adds
// the pose of now
void FSOLThrusterRibbonParitySmoke::SeedTrail()
{
    const FSOLFlameParityConfig& config = mConfigs[mRange];
    for (int32 trail = 0; trail < config.Trails; ++trail)
    {
        FSOLPlumeHistory history;
        const FVector3d headingNow = -ExhaustNow();
        for (int32 k = SOL::Thruster::FLAME_HISTORY_SAMPLES - 1; k >= 1; --k)
        {
            const double ageS = k * SOL::Thruster::FLAME_SAMPLE_INTERVAL_S;
            const FVector3d heading = RibbonParityHeading(headingNow, mUp, ageS, config.TurnDegPerS);
            FSOLPlumeSample sample;
            sample.EmitTimeS = RPSMOKE_NOW_S - ageS;
            sample.OffsetM = FVector3f(-heading * config.NozzleAftM);
            sample.ExhaustDir = FVector3f(-heading);
            sample.Brightness = static_cast<float>(config.Brightness);
            history.Push(sample);
        }
        mRibbons->SeedVerificationHistory(trail, history);
    }
}

//////////////////////////////////////////////////////////////////////////
// Feeds the synthetic trails at the current configuration's places in the camera frame (render-space metres)
void FSOLThrusterRibbonParitySmoke::FeedTrail(const FVector3d& cameraM)
{
    const FSOLFlameParityConfig& config = mConfigs[mRange];
    TArrayView<FSOLRibbonFeed> feeds = mRibbons->BeginNpcFeeds(config.Trails);
    for (int32 trail = 0; trail < config.Trails; ++trail)
    {
        const FVector3d place = RibbonParityPlace(config, trail);
        // The place is the L_p mark for side views, the nozzle for the chase view and the tail for the front view
        const FVector3d exhaust = ExhaustNow();
        const double plumeLengthM = RibbonParityPlumeLength(config, trail);
        const double forwardness = FVector3d::DotProduct(exhaust, mForward);
        const double alongM = forwardness > 0.9 ? 0.0 : forwardness < -0.9 ? SOLThruster::FlameLengthM(plumeLengthM)
            : plumeLengthM;
        const FVector3d markM = cameraM + mForward * place.X + mRight * place.Y + mUp * place.Z;
        const FVector3d shipM = markM - exhaust * (config.NozzleAftM + alongM);
        FSOLRibbonFeed feed;
        feed.Key = trail;
        feed.ShipPositionM = shipM;
        feed.ColourScalar = static_cast<float>(config.ColourC);
        feed.PlumeLengthM = plumeLengthM;
        feed.DistanceM = (shipM - cameraM).Size();
        feed.NozzleRadiusM = config.NozzleRadiusM;
        feed.Pose.OffsetM = FVector3f(exhaust * config.NozzleAftM);
        feed.Pose.ExhaustDir = FVector3f(exhaust);
        feed.Pose.Brightness = static_cast<float>(config.Brightness);
        feed.bValid = true;
        mRibbons->FeedNpc(mRibbons->FindTrail(trail), feed, feeds[trail]);
    }
    mRibbons->CommitNpcFeeds();
}

//////////////////////////////////////////////////////////////////////////
// Returns the current configuration's exhaust direction now: forward cos(view) - right sin(view)
FVector3d FSOLThrusterRibbonParitySmoke::ExhaustNow() const
{
    const double viewRad = FMath::DegreesToRadians(mConfigs[mRange].ViewDeg);
    return (mForward * FMath::Cos(viewRad) - mRight * FMath::Sin(viewRad)).GetSafeNormal();
}

//////////////////////////////////////////////////////////////////////////
// Applies the cvars of the current shot (renderer threshold, ribbons on or off, the configuration's anti-aliasing)
void FSOLThrusterRibbonParitySmoke::EnterShot()
{
    const FSOLFlameParityConfig& config = mConfigs[mRange];
    const ERibbonParityShot kind = RPSMOKE_SHOTS[mShot].Kind;
    RibbonParitySetCvar(SOL::THRUSTER_RIBBONS_CVAR, kind == ERibbonParityShot::Off ? TEXT("0") : TEXT("1"));
    RibbonParitySetCvar(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR, kind == ERibbonParityShot::Pool
        ? *FString::SanitizeFloat(RPSMOKE_POOL_ONLY_PX) : config.NiagaraPx);
    RibbonParitySetCvar(TEXT("r.AntiAliasingMethod"), config.AntiAliasing >= 0
        ? *FString::FromInt(config.AntiAliasing) : *mAntiAliasingDefault);
    mShotTimeS = 0.0;
    mShotFrames = 0;
}

//////////////////////////////////////////////////////////////////////////
// Checks the current shot's trail counts, logs the view state and takes its screenshot (without the HUD)
void FSOLThrusterRibbonParitySmoke::ExitShot(UWorld* world)
{
    const FRibbonParityShot& shot = RPSMOKE_SHOTS[mShot];
    const FSOLFlameParityConfig& config = mConfigs[mRange];
    const int32 niagara = mRibbons->GetNiagaraTrailCount();
    const int32 pool = mRibbons->GetPoolTrailCount();
    const int32 longTrails = config.Trails > 1 ? config.Trails - RPSMOKE_GRID_LONG_FIRST : config.Trails;
    const int32 wantNiagara = shot.Kind == ERibbonParityShot::Niagara ? longTrails : 0;
    const int32 wantPool = shot.Kind == ERibbonParityShot::Pool ? config.Trails
        : shot.Kind == ERibbonParityShot::Niagara ? config.Trails - longTrails : 0;
    Check(*FString::Printf(TEXT("%s %s counts"), *config.Name, shot.Name), niagara == wantNiagara && pool == wantPool,
        FString::Printf(TEXT("niagara %d (want %d), pool %d (want %d), live %d, device focal %.3f px, anchor %s"),
            niagara, wantNiagara, pool, wantPool, mRibbons->GetLiveTrailCount(), mRibbons->GetLastDeviceFocalPx(),
            *mRibbons->GetLastAnchorCm().ToString()));
    FVector2D size(0.0, 0.0);
    if (world->GetGameViewport() != nullptr)
    {
        world->GetGameViewport()->GetViewportSize(size);
    }
    const APlayerController* controller = world->GetFirstPlayerController();
    const APlayerCameraManager* camera = controller != nullptr ? controller->PlayerCameraManager : nullptr;
    const FVector3d cameraCm = camera != nullptr ? camera->GetCameraLocation() : FVector3d::ZeroVector;
    UE_LOG(LogSOL, Log, TEXT("SmokeRibbonParity: viewport %.0fx%.0f, r.ScreenPercentage %s, r.AntiAliasingMethod %s, "
        "r.DynamicRes.OperationMode %s, camera %s rot %s fov %.4f, camera - anchor %s cm"), size.X, size.Y,
        *RibbonParityGetCvar(TEXT("r.ScreenPercentage")), *RibbonParityGetCvar(TEXT("r.AntiAliasingMethod")),
        *RibbonParityGetCvar(TEXT("r.DynamicRes.OperationMode")), *cameraCm.ToString(),
        camera != nullptr ? *camera->GetCameraRotation().ToString() : TEXT("?"),
        camera != nullptr ? camera->GetFOVAngle() : 0.0f, *(cameraCm - mRibbons->GetLastAnchorCm()).ToString());
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("RibbonParity_%dx%d_%s_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), *config.Name,
        shot.Name));
    FScreenshotRequest::RequestScreenshot(path, false, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeRibbonParity: screenshot %s"), *path);
    mHoldS = RPSMOKE_HOLD_S;
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, restores the overrides and quits through the game mode's closing screenshot
void FSOLThrusterRibbonParitySmoke::Finish(ASOLShipPawn& pawn)
{
    RibbonParitySetCvar(SOL::THRUSTER_RIBBONS_CVAR, TEXT("1"));
    RibbonParitySetCvar(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR, TEXT("-1"));
    if (!mAntiAliasingDefault.IsEmpty())
    {
        RibbonParitySetCvar(TEXT("r.AntiAliasingMethod"), *mAntiAliasingDefault);
    }
    RibbonParitySetCvar(SOL::SpaceDust::CVAR_ENABLED, TEXT("1"));
    RibbonParitySetCvar(SOL::PLAYER_PLUME_CVAR, TEXT("1"));
    if (mRibbons.IsValid())
    {
        mRibbons->SetVerificationClock(-1.0);
        mRibbons->SetVerificationFeeder(nullptr);
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeRibbonParity SUMMARY: complete after %.1f s, %d/%d checks run passed%s"),
        mTotalTimeS, mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && mChecksRun > 0 ? TEXT("")
        : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLThrusterRibbonParitySmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeRibbonParity CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
