/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLGlossaryCropWriter.h"

#if !UE_BUILD_SHIPPING

#include "Game/SOLGameMode.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLRoute.h"
#include "Map/SOLRouteSubsystem.h"
#include "Menu/SOLGlossary.h"
#include "Menu/SOLPlayerGuide.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
    // The scenes, one window shot each, in shot order
    enum class ECropScene : uint8
    {
        Orbits,         // The inner orbits, Earth targeted (its orbit gold)
        Route,          // An active Patrol Loop, waypoints above and below the plane
        Grid,           // An active Move whose stems reach the grid in the middle, its markers and legs around them
        Pick,           // A destination pick in progress: disc, height guide, marker
        Count,
    };
    constexpr int32 CROP_SCENE_COUNT = static_cast<int32>(ECropScene::Count);

    static_assert(UE_ARRAY_COUNT(SOL::Paths::GLOSSARY_CROP_SHOT_FILES) == CROP_SCENE_COUNT, "One shot file per scene");

    // The scene each SOL::Paths::GLOSSARY_CROPS id is cut from
    struct FCropSource
    {
        const TCHAR* Id;
        ECropScene Scene;
    };
    const FCropSource CROP_SOURCES[] =
    {
        { TEXT("OrbitLines"), ECropScene::Orbits },
        { TEXT("RouteLegs"), ECropScene::Route },
        { TEXT("Waypoints"), ECropScene::Route },
        { TEXT("MapPick"), ECropScene::Pick },
        { TEXT("MapGrid"), ECropScene::Grid },
    };

    // Run timing
    const FIntPoint CROP_VIEW(SOL::Help::GLOSSARY_CROP_CAPTURE_WIDTH, SOL::Help::GLOSSARY_CROP_CAPTURE_HEIGHT);
    constexpr int32 CROP_VIEW_STABLE_FRAMES = 10;           // Frames in a row at the view size before the first scene
    constexpr double CROP_START_SETTLE_S = 3.0;             // Spawn, streaming and the first frames settled
    constexpr double CROP_VIEW_TIMEOUT_S = 60.0;            // The view size wait's limit
    constexpr double CROP_SHOT_SETTLE_S = 2.0;              // A scene's overlays and proxies settled before its shot
    constexpr int32 CROP_SHOT_SETTLE_FRAMES = 20;           // ... and at least this many frames
    constexpr double CROP_SCENE_TIMEOUT_S = 30.0;           // A scene's settle wait's limit (shaders included)
    constexpr int32 CROP_SHOT_FRAMES = 10;                  // A capture's frames before the next scene is set up
    constexpr double CROP_FILE_CHECK_S = 0.25;              // The shot size check's period
    constexpr double CROP_FILES_TIMEOUT_S = 20.0;           // The shots-on-disk wait's limit
    constexpr double CROP_WATCHDOG_S = 180.0;               // A stuck run fails and quits
    constexpr int32 CROP_CURSOR_INSET_PX = 8;               // The parked cursor, this far in from the top right
    constexpr int32 CROP_MAX_WINDOW_PX = 4096;              // The window may grow past a 1080p desktop's work area
    constexpr const TCHAR* CROP_STAGED_SUFFIX = TEXT(".new"); // A crop copied next to its target before the swap
    constexpr const TCHAR* CROP_BACKUP_SUFFIX = TEXT(".old"); // A target moved aside during the swap (until it is done)
    constexpr uint8 CROP_EXIT_FAILED = 1;                   // The process exit code of a failed run
    constexpr uint8 CROP_OPAQUE_ALPHA = 255;                // A crop pixel's alpha (the shots' alpha is not a matte)

    // The pinned scene. The ship sits this far from Earth along Earth's orbital direction (prograde, in the ecliptic),
    // and every scene looks further along it, so Earth is behind the camera and the Sun off to the side
    constexpr double CROP_SHIP_OFFSET_M = 6.0e8;

    // Orbits: the focus on the far side of the Sun from Earth, between Earth's and Venus's orbits; the only scene with
    // the orbit lines (the player's default overlay choices; hidden with SOL::ORBIT_LINE_CVAR in the others), and the
    // only one without the map hologram (its grid would cover the thin lines; the lines look the same in flight)
    constexpr double CROP_ORBIT_FOCUS_RADIUS_M = 1.29e11;
    constexpr double CROP_ORBIT_DISTANCE_M = 9.0e11;
    constexpr double CROP_ORBIT_PITCH_RAD = 1.0;

    // Route and Grid: routes ahead of the ship (the ship's own leg comes from behind the camera, left of the crops),
    // seen from CROP_ROUTE_DISTANCE_M at CROP_ROUTE_PITCH_RAD; waypoints as forward, right and up offsets from the
    // routes' centre (meters; up from the plane through it, which is the grid's), the first is the route's target.
    // Laid out for the 1920x1080 view (the screen positions in the comments)
    constexpr double CROP_ROUTE_AHEAD_M = 2.5e8;
    constexpr double CROP_ROUTE_DISTANCE_M = 1.2e8;
    constexpr double CROP_ROUTE_PITCH_RAD = 0.6;
    const FVector3d CROP_ROUTE_OFFSETS_M[] =
    {
        FVector3d(-1.63e7, -1.82e7, -3.0e6),    // (680, 720), below the plane: the target
        FVector3d(2.02e6, -1.41e7, 8.0e6),      // (760, 430), above: the Waypoints crop's pair...
        FVector3d(1.17e7, -7.26e6, -8.0e6),     // (870, 540), below: ... with this one
        FVector3d(9.81e6, 1.27e7, 6.0e6),       // (1130, 400), above
        FVector3d(-2.51e6, 2.06e7, 6.0e4),      // (1250, 560), just above the plane (a filled marker, too low for
                                                // a stem): the Route legs crop's corner
    };
    constexpr int32 CROP_ROUTE_WAYPOINTS = UE_ARRAY_COUNT(CROP_ROUTE_OFFSETS_M);
    const FVector3d CROP_GRID_OFFSETS_M[] =
    {
        FVector3d(1.731e7, -2.418e7, 8.0e6),    // (650, 330), above: the target
        FVector3d(6.679e6, 3.394e6, 8.194e6),   // (1005, 395), above, its foot at (1005, 490) in the Grid crop
        FVector3d(1.025e7, 9.65e6, 7.035e6),    // (1085, 385), above, its foot at (1085, 465) in the Grid crop
        FVector3d(1.4e7, 3.359e7, 8.0e6),       // (1400, 350), above
        FVector3d(-1.073e7, 3.028e7, -6.0e6),   // (1400, 700), below
        FVector3d(-6.358e5, 6.103e6, -7.212e6), // (1045, 625), below, its foot at (1045, 545) in the Grid crop
    };
    constexpr int32 CROP_GRID_WAYPOINTS = UE_ARRAY_COUNT(CROP_GRID_OFFSETS_M);
    constexpr int32 CROP_MAX_WAYPOINTS = FMath::Max(CROP_ROUTE_WAYPOINTS, CROP_GRID_WAYPOINTS);

    // Pick: around the ship (empty space, so the ship is the reference); press at the view centre plus an offset, drag,
    // release, Shift, the cursor up, then the height locked. Script frames count from the scene's start
    constexpr double CROP_PICK_DISTANCE_M = 2.0e7;
    constexpr double CROP_PICK_PITCH_RAD = 0.6;
    // Every scripted cursor sits this far off the pixel grid: the marker then lands a quarter pixel inside a pixel, so
    // the run-to-run float jitter of its projection cannot flip its crisp, odd-width lines by a whole row
    const FVector2D CROP_PICK_SUBPIXEL_PX(0.25, 0.25);
    const FVector2D CROP_PICK_PRESS_PX(0.0, 0.0);           // From the view centre (the ship)
    const FVector2D CROP_PICK_DRAG_PX(55.0, 18.0);          // ... where the drag ends
    constexpr double CROP_PICK_HEIGHT_PX = 65.0;            // The cursor's rise for the height
    constexpr bool CROP_PICK_LOCK_HEIGHT = false;           // Lock the height (else the preview: a pick in progress)
    constexpr int32 CROP_PICK_FRAME_CURSOR = 3;
    constexpr int32 CROP_PICK_FRAME_PRESS = 5;
    constexpr int32 CROP_PICK_FRAME_DRAG = 8;
    constexpr int32 CROP_PICK_FRAME_RELEASE = 12;
    constexpr int32 CROP_PICK_FRAME_SHIFT = 14;
    constexpr int32 CROP_PICK_FRAME_RISE = 17;
    constexpr int32 CROP_PICK_FRAME_LOCK = 21;
    constexpr int32 CROP_PICK_FRAME_READY = 23;

    //////////////////////////////////////////////////////////////////////////
    // Returns the game window, or nullptr
    TSharedPtr<SWindow> CropGameWindow()
    {
        return GEngine != nullptr && GEngine->GameViewport != nullptr ? GEngine->GameViewport->GetWindow() : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether no shader is still compiling (an editor-binary -game run compiles them on demand)
    bool CropShadersReady()
    {
#if WITH_EDITOR
        return GShaderCompilingManager == nullptr || GShaderCompilingManager->GetNumRemainingJobs() == 0;
#else
        return true;
#endif
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the scene a crop is cut from, or Count for an id without a source
    ECropScene CropSceneOf(const TCHAR* id)
    {
        for (const FCropSource& source : CROP_SOURCES)
        {
            if (FCString::Strcmp(source.Id, id) == 0)
            {
                return source.Scene;
            }
        }
        return ECropScene::Count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a scene shot's capture path
    FString CropShotPath(const int32 scene)
    {
        return SOLPlayerGuide::CapturePath(SOL::Paths::GLOSSARY_CROP_SHOT_FILES[scene]);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the map camera yaw that looks along a horizontal direction (SOLMapCamera: forward = (cos, sin) of yaw)
    double CropYawFor(const FVector3d& forward)
    {
        return FMath::Atan2(forward.Y, forward.X);
    }

    //////////////////////////////////////////////////////////////////////////
    // Turns a developer console switch on or off (the orbit lines, the map hologram; the player's settings are left
    // alone)
    void CropSetSwitch(const TCHAR* name, const bool bOn)
    {
        if (IConsoleVariable* variable = IConsoleManager::Get().FindConsoleVariable(name))
        {
            variable->Set(bOn ? 1 : 0);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Asks the engine to quit with an exit code (graceful: the settings snapshot is still given back at shutdown)
    void CropQuit(const uint8 exitCode)
    {
        FPlatformMisc::RequestExitWithStatus(false, exitCode, TEXT("FSOLGlossaryCropWriter"));
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems, asks for the shots' window size and starts waiting for the view
void FSOLGlossaryCropWriter::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mRoutes = world->GetSubsystem<USOLRouteSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mSimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    mLastTimeS = FPlatformTime::Seconds();
    mShotSizes.Init(INDEX_NONE, CROP_SCENE_COUNT);

    // The map cursor follows only the script, never the real mouse
    pawn.SetMapCursorScripted(true);

    // A windowed game is clamped to the desktop's work area: lift the limit and ask for the shots' size
    if (const TSharedPtr<SWindow> window = CropGameWindow())
    {
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(CROP_MAX_WINDOW_PX).SetMaxHeight(CROP_MAX_WINDOW_PX));
    }
    if (GEngine != nullptr)
    {
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), CROP_VIEW.X, CROP_VIEW.Y));
    }
    UE_LOG(LogSOL, Log, TEXT("GlossaryCrops: capturing %d crops from %d scenes at %s"), SOLGlossary::CropCount(),
        CROP_SCENE_COUNT, SOL::Help::GLOSSARY_CROP_SIM_DATE);
    ToStage(EStage::WaitView);
    if (!FSOLSimClock::TryParseUtcJulianDate(SOL::Help::GLOSSARY_CROP_SIM_DATE, mPinnedJulianDate))
    {
        Fail(FString::Printf(TEXT("the pinned date %s does not parse"), SOL::Help::GLOSSARY_CROP_SIM_DATE));
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the run: the view, each scene and its shot, the shots on disk, then the crops
bool FSOLGlossaryCropWriter::Update(ASOLShipPawn& pawn)
{
    if (mIsFinished)
    {
        return true;
    }
    const double nowS = FPlatformTime::Seconds();
    const double deltaS = nowS - mLastTimeS;
    mLastTimeS = nowS;
    mStageS += deltaS;
    mRunS += deltaS;
    ++mStageFrames;
    if (mRunS > CROP_WATCHDOG_S)
    {
        Fail(FString::Printf(TEXT("watchdog: stuck in stage %d (scene %d) after %.0f s"), static_cast<int32>(mStage),
            mScene, mRunS));
        return true;
    }
    if (!mController.IsValid() || !mMapMode.IsValid() || !mRoutes.IsValid() || !mShips.IsValid()
        || !mTargeting.IsValid() || !mRegistry.IsValid() || !mSimClock.IsValid())
    {
        Fail(TEXT("the controller or a subsystem went away"));
        return true;
    }

    // Every frame once placed: the date and the ship held, the scene's camera, the cursor out of the shots
    if (mIsPlaced)
    {
        HoldScene();
        AimCamera();
        ParkCursor();
    }
    switch (mStage)
    {
    case EStage::WaitView:
    {
        // The view at the shots' size for a few frames, the ship spawned and the shaders compiled
        const FIntPoint view = GetViewSize();
        mViewFrames = view == CROP_VIEW && mShips->HasPlayerShip() && CropShadersReady() ? mViewFrames + 1 : 0;
        if (mViewFrames < CROP_VIEW_STABLE_FRAMES || mStageS < CROP_START_SETTLE_S)
        {
            if (mStageS > CROP_VIEW_TIMEOUT_S)
            {
                Fail(FString::Printf(TEXT("no %dx%d view (view %dx%d, ship %d, shaders ready %d)"), CROP_VIEW.X,
                    CROP_VIEW.Y, view.X, view.Y, mShips->HasPlayerShip() ? 1 : 0, CropShadersReady() ? 1 : 0));
                return true;
            }
            return false;
        }
        if (const TSharedPtr<SWindow> window = CropGameWindow())
        {
            window->BringToFront(true);
        }

        // The clock back at the pinned date; the bodies stand there from the next universe update
        mSimClock->SetJulianDate(mPinnedJulianDate);
        ToStage(EStage::Place);
        return false;
    }
    case EStage::Place:
    {
        // The ship placed from the pinned positions and held from now on, then the map in Jump mode (no draft)
        if (!PlaceShip())
        {
            Fail(TEXT("Earth or the Sun is not in the body registry"));
            return true;
        }
        HoldScene();
        if (!mMapMode->OpenMap(mController.Get(), true))
        {
            Fail(TEXT("the map did not open"));
            return true;
        }
        mIsMapOpen = true;
        mScene = 0;
        ToStage(EStage::Setup);
        return false;
    }
    case EStage::Setup:
    {
        // The scene set up, its old shot removed so only this run's file counts
        FString reason;
        if (!SetupScene(pawn, reason))
        {
            Fail(reason);
            return true;
        }
        const FString shot = CropShotPath(mScene);
        if (!IFileManager::Get().Delete(*shot, false, true, true) && IFileManager::Get().FileExists(*shot))
        {
            Fail(FString::Printf(TEXT("could not remove the old shot %s"), *shot));
            return true;
        }
        mScriptStep = 0;
        ToStage(EStage::Settle);
        return false;
    }
    case EStage::Settle:
    {
        // The scene's script run, then settled (time, frames, shaders) and checked, then a window shot with the UI
        const bool bReady = ScriptScene(pawn);
        if (!bReady || mStageS < CROP_SHOT_SETTLE_S || mStageFrames < CROP_SHOT_SETTLE_FRAMES || !CropShadersReady())
        {
            if (mStageS > CROP_SCENE_TIMEOUT_S)
            {
                Fail(FString::Printf(TEXT("scene %d did not settle (script ready %d, shaders ready %d)"), mScene,
                    bReady ? 1 : 0, CropShadersReady() ? 1 : 0));
                return true;
            }
            return false;
        }
        const FString missing = CheckScene();
        if (!missing.IsEmpty())
        {
            Fail(FString::Printf(TEXT("scene %d: %s"), mScene, *missing));
            return true;
        }
        if (GetViewSize() != CROP_VIEW)
        {
            Fail(FString::Printf(TEXT("the view changed size before scene %d's shot"), mScene));
            return true;
        }
        const FString path = CropShotPath(mScene);
        FScreenshotRequest::RequestScreenshot(path, true, false);
        UE_LOG(LogSOL, Log, TEXT("GlossaryCrops: shot %s"), *path);
        ToStage(EStage::Captured);
        return false;
    }
    case EStage::Captured:
        if (mStageFrames < CROP_SHOT_FRAMES)
        {
            return false;
        }
        ++mScene;
        ToStage(mScene < CROP_SCENE_COUNT ? EStage::Setup : EStage::WaitFiles);
        return false;
    case EStage::WaitFiles:
    {
        // Every shot written, then the crops cut and put in place
        if (!AreShotsWritten())
        {
            if (mStageS > CROP_FILES_TIMEOUT_S)
            {
                Fail(TEXT("a shot was not written"));
                return true;
            }
            return false;
        }
        FString reason;
        if (!WriteCrops(reason))
        {
            Fail(reason);
            return true;
        }
        Finish();
        return true;
    }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Moves to a stage, its time and frames counted from now
void FSOLGlossaryCropWriter::ToStage(const EStage stage)
{
    mStage = stage;
    mStageS = 0.0;
    mStageFrames = 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the game viewport's size in pixels
FIntPoint FSOLGlossaryCropWriter::GetViewSize() const
{
    FIntPoint size = FIntPoint::ZeroValue;
    if (mController.IsValid())
    {
        mController->GetViewportSize(size.X, size.Y);
    }
    return size;
}

//////////////////////////////////////////////////////////////////////////
// Parks the OS cursor (the hologram pointer draws at it) in the view's top-right corner and tells Slate
void FSOLGlossaryCropWriter::ParkCursor() const
{
    const FIntPoint size = GetViewSize();
    if (mController.IsValid() && size.X > CROP_CURSOR_INSET_PX)
    {
        mController->SetMouseLocation(size.X - CROP_CURSOR_INSET_PX, CROP_CURSOR_INSET_PX);
    }
}

//////////////////////////////////////////////////////////////////////////
// Fixes the scenes' view axes (forward along Earth's orbital direction, right per SOLMapCamera) and the ship's place,
// CROP_SHIP_OFFSET_M ahead of Earth along that direction; HoldScene keeps it there from now on
bool FSOLGlossaryCropWriter::PlaceShip()
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    mEarthIndex = registry.FindByName(FName(SOL::BodyNames::EARTH));
    mSunIndex = registry.FindByName(FName(SOL::BodyNames::SUN));
    if (mEarthIndex == INDEX_NONE || mSunIndex == INDEX_NONE)
    {
        return false;
    }
    const FVector3d earthM = registry.GetPositionM(mEarthIndex);
    FVector3d radial = earthM - registry.GetPositionM(mSunIndex);
    radial.Z = 0.0;
    radial.Normalize();
    mForward = FVector3d(-radial.Y, radial.X, 0.0);
    mRight = FVector3d(mForward.Y, -mForward.X, 0.0);
    mShipOffsetM = mForward * CROP_SHIP_OFFSET_M;
    mIsPlaced = true;
    UE_LOG(LogSOL, Log, TEXT("GlossaryCrops: ship placed %.3g m from Earth at JD %.5f, view axis (%.4f, %.4f)"),
        CROP_SHIP_OFFSET_M, mSimClock->GetClock().GetJulianDate(), mForward.X, mForward.Y);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Puts the sim clock back at the pinned date (the bodies and the space-fixed grid stand still, so every run sees the
// same scene) and the ship back at its offset from Earth with Earth's velocity and no spin
void FSOLGlossaryCropWriter::HoldScene() const
{
    mSimClock->SetJulianDate(mPinnedJulianDate);
    const FSOLBodyFrameCache& cache = mRegistry->GetUnrealFrameCache();
    if (!mShips->HasPlayerShip() || mEarthIndex == INDEX_NONE || mEarthIndex >= cache.Num())
    {
        return;
    }
    FSOLShipState state = mShips->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(mRegistry->GetRegistry().GetPositionM(mEarthIndex) + mShipOffsetM);
    state.VelocityMps = cache.VelocitiesMps[mEarthIndex];
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Sets the scene's map camera, the focus at its offset from the scene's body this frame
void FSOLGlossaryCropWriter::AimCamera() const
{
    if (!mIsMapOpen || mFocusBodyIndex == INDEX_NONE)
    {
        return;
    }
    FSOLOrbitCameraState state = mCamera;
    state.FocusPositionM = mRegistry->GetRegistry().GetPositionM(mFocusBodyIndex) + mFocusOffsetM;
    mMapMode->SetCameraState(state);
}

//////////////////////////////////////////////////////////////////////////
// Sets up a scene: the route, the target and the pick as it needs them, and its camera
bool FSOLGlossaryCropWriter::SetupScene(ASOLShipPawn& pawn, FString& outReason)
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FVector3d earthM = registry.GetPositionM(mEarthIndex);
    const FVector3d shipM = earthM + mShipOffsetM;
    mMapMode->ClearPick();
    mRoutes->ClearRoute();
    mTargeting->ClearTarget();
    mCamera = FSOLOrbitCameraState();
    mCamera.YawRad = CropYawFor(mForward);
    const bool bOrbits = static_cast<ECropScene>(mScene) == ECropScene::Orbits;
    CropSetSwitch(SOL::ORBIT_LINE_CVAR, bOrbits);
    CropSetSwitch(SOL::Hologram::CVAR_ENABLED, !bOrbits);
    switch (static_cast<ECropScene>(mScene))
    {
    case ECropScene::Orbits:
    {
        // Earth targeted (its orbit gold); the focus on the far side of the Sun, between Earth's and Venus's orbits
        if (!mTargeting->SelectCandidate(mEarthIndex))
        {
            outReason = TEXT("could not target Earth");
            return false;
        }
        FVector3d radial = earthM - registry.GetPositionM(mSunIndex);
        radial.Z = 0.0;
        radial.Normalize();
        mFocusBodyIndex = mSunIndex;
        mFocusOffsetM = -radial * CROP_ORBIT_FOCUS_RADIUS_M;
        mCamera.DistanceM = CROP_ORBIT_DISTANCE_M;
        mCamera.PitchRad = CROP_ORBIT_PITCH_RAD;
        break;
    }
    case ECropScene::Route:
    case ECropScene::Grid:
    {
        // The scene's route ahead of the ship, each waypoint following Earth; it starts (active, full alpha)
        const bool bLoop = static_cast<ECropScene>(mScene) == ECropScene::Route;
        const TConstArrayView<FVector3d> offsets = bLoop ? MakeArrayView(CROP_ROUTE_OFFSETS_M)
            : MakeArrayView(CROP_GRID_OFFSETS_M);
        const FVector3d centreM = shipM + mForward * CROP_ROUTE_AHEAD_M;
        FSOLRouteWaypoint waypoints[CROP_MAX_WAYPOINTS];
        for (int32 index = 0; index < offsets.Num(); ++index)
        {
            const FVector3d& offset = offsets[index];
            const FVector3d pointM = centreM + mForward * offset.X + mRight * offset.Y
                + FVector3d::UpVector * offset.Z;
            waypoints[index] = SOLRoute::MakeWaypointAtPoint(mEarthIndex, pointM, earthM);
        }
        if (!mRoutes->StartRoute(bLoop ? ESOLRouteKind::PatrolLoop : ESOLRouteKind::Move,
            MakeArrayView(waypoints, offsets.Num())))
        {
            outReason = FString::Printf(TEXT("the scene %d route did not start"), mScene);
            return false;
        }
        mFocusBodyIndex = mEarthIndex;
        mFocusOffsetM = centreM - earthM;
        mCamera.DistanceM = CROP_ROUTE_DISTANCE_M;
        mCamera.PitchRad = CROP_ROUTE_PITCH_RAD;
        break;
    }
    case ECropScene::Pick:
        // Around the ship, which is the pick's reference (the script presses on empty space)
        mFocusBodyIndex = mEarthIndex;
        mFocusOffsetM = shipM - earthM;
        mCamera.DistanceM = CROP_PICK_DISTANCE_M;
        mCamera.PitchRad = CROP_PICK_PITCH_RAD;
        break;
    default:
        outReason = FString::Printf(TEXT("no scene %d"), mScene);
        return false;
    }
    AimCamera();
    UE_LOG(LogSOL, Log, TEXT("GlossaryCrops: scene %d set up (distance %.3g m, pitch %.2f, yaw %.3f)"), mScene,
        mCamera.DistanceM, mCamera.PitchRad, mCamera.YawRad);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Runs the scene's per-frame script: the pick's press, drag, release, Shift and rise (other scenes are ready at once)
bool FSOLGlossaryCropWriter::ScriptScene(ASOLShipPawn& pawn)
{
    if (static_cast<ECropScene>(mScene) != ECropScene::Pick)
    {
        return true;
    }
    const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
    const FVector2D centrePx = FVector2D(GetViewSize()) * 0.5 + CROP_PICK_SUBPIXEL_PX;
    const FVector2D pressPx = centrePx + CROP_PICK_PRESS_PX;
    const FVector2D dragPx = centrePx + CROP_PICK_DRAG_PX;
    switch (mStageFrames)
    {
    case CROP_PICK_FRAME_CURSOR:
        pawn.HandleMapCursorMoved(pressPx);
        break;
    case CROP_PICK_FRAME_PRESS:
        mMapMode->PressPick(hud != nullptr ? &hud->GetMapBodyOverlay() : nullptr, false);
        break;
    case CROP_PICK_FRAME_DRAG:
        pawn.HandleMapCursorMoved(dragPx);
        break;
    case CROP_PICK_FRAME_RELEASE:
        mMapMode->ReleasePick();
        break;
    case CROP_PICK_FRAME_SHIFT:
        mMapMode->SetHeightModifierHeld(true);
        break;
    case CROP_PICK_FRAME_RISE:
        pawn.HandleMapCursorMoved(dragPx - FVector2D(0.0, CROP_PICK_HEIGHT_PX));
        break;
    case CROP_PICK_FRAME_LOCK:
        if (CROP_PICK_LOCK_HEIGHT)
        {
            mMapMode->PressPick(hud != nullptr ? &hud->GetMapBodyOverlay() : nullptr, true);
        }
        break;
    default:
        break;
    }
    return mStageFrames >= CROP_PICK_FRAME_READY;
}

//////////////////////////////////////////////////////////////////////////
// Returns what the scene is missing for its crops (empty when it shows everything)
FString FSOLGlossaryCropWriter::CheckScene() const
{
    const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
    if (hud == nullptr || !mMapMode->IsMapOpen())
    {
        return TEXT("no HUD or the map is closed");
    }
    switch (static_cast<ECropScene>(mScene))
    {
    case ECropScene::Orbits:
    {
        const ASOLGameMode* gameMode = mController->GetWorld()->GetAuthGameMode<ASOLGameMode>();
        const ASOLOrbitLineVisuals* lines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
        if (lines == nullptr || lines->CountVisibleLines() == 0 || lines->GetHighlightedSlot() == INDEX_NONE)
        {
            return FString::Printf(TEXT("orbit lines %d, highlighted slot %d"), lines != nullptr
                ? lines->CountVisibleLines() : -1, lines != nullptr ? lines->GetHighlightedSlot() : -1);
        }
        return FString();
    }
    case ECropScene::Route:
    case ECropScene::Grid:
    {
        // The loop draws a leg per waypoint, each with its arrow, its closing one dashed; the Move one leg less (its
        // short leg is too short for an arrow, which the Grid crop does not show)
        const FSOLMapRouteOverlay& overlay = hud->GetRouteOverlay();
        const bool bLoop = static_cast<ECropScene>(mScene) == ECropScene::Route;
        const int32 legs = bLoop ? CROP_ROUTE_WAYPOINTS : CROP_GRID_WAYPOINTS - 1;
        if (mRoutes->GetState() != ESOLRouteState::Active || overlay.GetLastLegsDrawn() < legs
            || (bLoop && (overlay.GetLastDashesDrawn() == 0 || overlay.GetLastArrowsDrawn() < legs)))
        {
            return FString::Printf(TEXT("route state %d, %d legs, %d dashes, %d arrows"),
                static_cast<int32>(mRoutes->GetState()), overlay.GetLastLegsDrawn(), overlay.GetLastDashesDrawn(),
                overlay.GetLastArrowsDrawn());
        }
        return FString();
    }
    case ECropScene::Pick:
    {
        const FSOLMapPickOverlay& overlay = hud->GetMapPickOverlay();
        const FSOLMapPickState& pick = mMapMode->GetPickState();
        if (!pick.bHasReference || pick.bReferenceIsBody || !pick.bPlanarLocked || !overlay.HasDrawnGuide()
            || !overlay.HasDrawnMarker())
        {
            return FString::Printf(TEXT("pick reference %d (body %d), planar locked %d, guide %d, marker %d"),
                pick.bHasReference ? 1 : 0, pick.bReferenceIsBody ? 1 : 0, pick.bPlanarLocked ? 1 : 0,
                overlay.HasDrawnGuide() ? 1 : 0, overlay.HasDrawnMarker() ? 1 : 0);
        }
        return FString();
    }
    default:
        return TEXT("no such scene");
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether every shot is on disk, not empty and the same size as at the last check
bool FSOLGlossaryCropWriter::AreShotsWritten()
{
    if (mStageS - mLastFileCheckS < CROP_FILE_CHECK_S && mStageFrames > 1)
    {
        return false;
    }
    mLastFileCheckS = mStageS;
    bool bAll = true;
    for (int32 scene = 0; scene < CROP_SCENE_COUNT; ++scene)
    {
        const int64 size = IFileManager::Get().FileSize(*CropShotPath(scene));
        bAll &= size > 0 && size == mShotSizes[scene];
        mShotSizes[scene] = size;
    }
    return bAll;
}

//////////////////////////////////////////////////////////////////////////
// Cuts each crop out of its scene's shot; keeps the image in place when the crop is it plus capture noise (so an
// unchanged scene leaves the files untouched), else stages the crop next to it. Then swaps the staged crops in, each
// old image backed up first: any failure leaves the repository's images as they were (backups restored, staged copies
// removed), a success removes the backups
bool FSOLGlossaryCropWriter::WriteCrops(FString& outReason) const
{
    IFileManager& files = IFileManager::Get();
    const int32 cropCount = SOLGlossary::CropCount();
    TArray<bool> changed;       // The crop differs from its image (staged, to be swapped in)
    TArray<bool> backedUp;      // Its old image was moved to the backup
    TArray<bool> placed;        // The staged crop was moved over the image
    changed.Init(false, cropCount);
    backedUp.Init(false, cropCount);
    placed.Init(false, cropCount);

    // Puts every image back as it was (a placed crop replaced by its backup, or removed when there was none) and
    // removes the staged copies and the captures
    const auto rollBack = [&files, &backedUp, &placed, cropCount]()
    {
        for (int32 index = 0; index < cropCount; ++index)
        {
            const TCHAR* imageFile = SOL::Paths::GLOSSARY_CROPS[index].ImageFile;
            const FString image = SOLPlayerGuide::ImagePath(imageFile);
            const FString backup = image + CROP_BACKUP_SUFFIX;
            if (backedUp[index])
            {
                if (!files.Move(*image, *backup, true, true))
                {
                    UE_LOG(LogSOL, Error, TEXT("GlossaryCrops: could not restore %s from its backup %s"), *image,
                        *backup);
                }
            }
            else if (placed[index])
            {
                files.Delete(*image, false, true, true);
            }
            files.Delete(*(image + CROP_STAGED_SUFFIX), false, true, true);
            files.Delete(*SOLPlayerGuide::CapturePath(imageFile), false, true, true);
        }
    };

    // Every shot loaded once, as 8-bit BGRA in sRGB, at the capture size
    FImage shots[CROP_SCENE_COUNT];
    for (int32 scene = 0; scene < CROP_SCENE_COUNT; ++scene)
    {
        const FString path = CropShotPath(scene);
        if (!FImageUtils::LoadImage(*path, shots[scene]))
        {
            outReason = FString::Printf(TEXT("could not read the shot %s"), *path);
            return false;
        }
        shots[scene].ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
        if (shots[scene].SizeX != CROP_VIEW.X || shots[scene].SizeY != CROP_VIEW.Y)
        {
            outReason = FString::Printf(TEXT("the shot %s is %dx%d, not %dx%d"), *path, shots[scene].SizeX,
                shots[scene].SizeY, CROP_VIEW.X, CROP_VIEW.Y);
            return false;
        }
    }

    // Each crop cut (opaque) and compared with its image: kept, or saved to its capture file and copied next to its
    // image (the same folder: the swap renames)
    const int32 side = SOL::Help::GLOSSARY_CROP_PX;
    FImage crop;
    crop.Init(side, side, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    const FSOLImageNoise noise = SOLPlayerGuide::CropNoise();
    for (int32 index = 0; index < cropCount; ++index)
    {
        const SOL::Paths::FGlossaryCrop& row = SOL::Paths::GLOSSARY_CROPS[index];
        const ECropScene scene = CropSceneOf(row.Id);
        const FIntRect rect = SOLGlossary::CropRect(index);
        if (scene == ECropScene::Count || rect.Min.X < 0 || rect.Min.Y < 0 || rect.Max.X > CROP_VIEW.X
            || rect.Max.Y > CROP_VIEW.Y)
        {
            rollBack();
            outReason = FString::Printf(TEXT("crop %s has no scene or its rect %s is outside the shot"), row.Id,
                *rect.ToString());
            return false;
        }
        const FImage& shot = shots[static_cast<int32>(scene)];
        for (int32 y = 0; y < side; ++y)
        {
            FMemory::Memcpy(crop.GetPixelPointer(0, y), shot.GetPixelPointer(rect.Min.X, rect.Min.Y + y),
                side * sizeof(FColor));
        }
        for (FColor& pixel : crop.AsBGRA8())
        {
            pixel.A = CROP_OPAQUE_ALPHA;
        }
        const FString image = SOLPlayerGuide::ImagePath(row.ImageFile);
        const FSOLImageDiff diff = SOLPlayerGuide::CompareWithFile(crop, image, noise);
        if (SOLPlayerGuide::IsCaptureNoise(diff, noise))
        {
            UE_LOG(LogSOL, Log, TEXT("GlossaryCrops KEPT %s (max delta %d, %d pixels above %d)"),
                *FPaths::ConvertRelativePathToFull(image), diff.MaxDelta, diff.NoisyPixels, noise.NoiseDelta);
            continue;
        }
        UE_LOG(LogSOL, Log, TEXT("GlossaryCrops: %s changed (comparable %d, max delta %d, %d pixels above %d)"),
            row.Id, diff.bComparable ? 1 : 0, diff.MaxDelta, diff.NoisyPixels, noise.NoiseDelta);
        const FString capture = SOLPlayerGuide::CapturePath(row.ImageFile);
        const FString staged = image + CROP_STAGED_SUFFIX;
        if (!FImageUtils::SaveImageByExtension(*capture, crop) || files.Copy(*staged, *capture, true, true) != COPY_OK)
        {
            rollBack();
            outReason = FString::Printf(TEXT("could not write crop %s (%s, staged %s)"), row.Id, *capture, *staged);
            return false;
        }
        changed[index] = true;
    }

    // The swap: each changed crop's old image moved to its backup, then the staged crop renamed over it
    for (int32 index = 0; index < cropCount; ++index)
    {
        if (!changed[index])
        {
            continue;
        }
        const FString image = SOLPlayerGuide::ImagePath(SOL::Paths::GLOSSARY_CROPS[index].ImageFile);
        const FString backup = image + CROP_BACKUP_SUFFIX;
        if (files.FileExists(*image))
        {
            if (!files.Move(*backup, *image, true, true))
            {
                rollBack();
                outReason = FString::Printf(TEXT("could not back %s up to %s"), *image, *backup);
                return false;
            }
            backedUp[index] = true;
        }
        if (!files.Move(*image, *(image + CROP_STAGED_SUFFIX), true, true))
        {
            rollBack();
            outReason = FString::Printf(TEXT("could not rename the staged crop over %s"), *image);
            return false;
        }
        placed[index] = true;
    }

    // Every crop in place: the backups and captures removed
    for (int32 index = 0; index < cropCount; ++index)
    {
        if (!changed[index])
        {
            continue;
        }
        const TCHAR* imageFile = SOL::Paths::GLOSSARY_CROPS[index].ImageFile;
        const FString image = SOLPlayerGuide::ImagePath(imageFile);
        files.Delete(*(image + CROP_BACKUP_SUFFIX), false, true, true);
        files.Delete(*SOLPlayerGuide::CapturePath(imageFile), false, true, true);
        UE_LOG(LogSOL, Log, TEXT("GlossaryCrops WROTE %s (%lld bytes)"), *FPaths::ConvertRelativePathToFull(image),
            files.FileSize(*image));
    }

    // The scene shots served their purpose (a failed run keeps them in the screenshot folder, to see why)
    for (int32 scene = 0; scene < CROP_SCENE_COUNT; ++scene)
    {
        files.Delete(*CropShotPath(scene), false, true, true);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Logs the failure and quits
void FSOLGlossaryCropWriter::Fail(const FString& reason)
{
    UE_LOG(LogSOL, Error, TEXT("GlossaryCrops FAIL %s"), *reason);
    mIsFinished = true;
    CropQuit(CROP_EXIT_FAILED);
}

//////////////////////////////////////////////////////////////////////////
// Logs the end and quits
void FSOLGlossaryCropWriter::Finish()
{
    UE_LOG(LogSOL, Log, TEXT("GlossaryCrops DONE %d crops in %.1f s"), SOLGlossary::CropCount(), mRunS);
    mIsFinished = true;
    CropQuit(0);
}

#endif
