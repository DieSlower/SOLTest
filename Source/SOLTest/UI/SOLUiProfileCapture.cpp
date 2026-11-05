/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLUiProfileCapture.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"

namespace
{
    constexpr int32 CAPTURE_DUMP_TAIL_FRAMES = 30;              // Frames left for a stat dump to print
    constexpr int32 CAPTURE_DUMP_TIMEOUT_FRAMES = 240;          // ... and at most this many more
    constexpr int32 CAPTURE_GPU_GAP_FRAMES = 20;                // Frames between two ProfileGPU commands
    constexpr int32 CAPTURE_GPU_TAIL_FRAMES = 20;               // Frames after the last one before its table counts
    constexpr int32 CAPTURE_GPU_RESERVE = 2 * FSOLUiProfileCapture::GPU_CAPTURES;
    constexpr double CAPTURE_GPU_TIMEOUT_S = 10.0;              // The GPU phase gives up on missing tables after this
    const TCHAR* const CAPTURE_GPU_CMD = TEXT("ProfileGPU");
    const TCHAR* const CAPTURE_MATERIAL_EVENT = TEXT("Slate Material: ");

    // The ProfileGPU setup, run at Start: the visualizer window would add its own Slate pass to the next profiles (the
    // log gets the table either way), and ParseGpuRow reads the unicode table with its stat, percent, exclusive and
    // inclusive columns and every event (the engine defaults, pinned here)
    const TCHAR* const CAPTURE_GPU_SETUP_CMDS[] = {
        TEXT("r.ProfileGPU.ShowUI 0"),
        TEXT("r.ProfileGPU.TableFormatting 1"),
        TEXT("r.ProfileGPU.UnicodeOutput 1"),
        TEXT("r.ProfileGPU.ShowStats 1"),
        TEXT("r.ProfileGPU.ShowPercentColumn 1"),
        TEXT("r.ProfileGPU.ShowInclusive 1"),
        TEXT("r.ProfileGPU.ShowExclusive 1"),
        TEXT("r.ProfileGPU.ThresholdPercent 0"),
        TEXT("r.ProfileGPU.Root *") };
    // Run at Stop: the visualizer back on for normal use (the engine default)
    const TCHAR* const CAPTURE_GPU_UI_RESTORE_CMD = TEXT("r.ProfileGPU.ShowUI 1");

    // The ProfileGPU table's box characters: the column rule, the cell rule and the bottom-left corner
    constexpr TCHAR CAPTURE_BOX_COLUMN = 0x2503;
    constexpr TCHAR CAPTURE_BOX_CELL = 0x2502;
    constexpr TCHAR CAPTURE_BOX_END = 0x2517;

    //////////////////////////////////////////////////////////////////////////
    // Returns the index of the median of the captures' UI times (the lower middle for an even count), or INDEX_NONE
    int32 MedianUiIndex(const TArray<FSOLUiProfileCapture::FGpuCapture>& captures)
    {
        TArray<int32> order;
        for (int32 index = 0; index < captures.Num(); ++index)
        {
            if (captures[index].UiMs >= 0.0)
            {
                order.Add(index);
            }
        }
        if (order.IsEmpty())
        {
            return INDEX_NONE;
        }
        order.Sort([&captures](const int32 a, const int32 b) { return captures[a].UiMs < captures[b].UiMs; });
        return order[(order.Num() - 1) / 2];
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the stat dumpave command and log token of each root stat and keeps the material name token
FSOLUiProfileCapture::FSOLUiProfileCapture(const TConstArrayView<const TCHAR*> cpuRootStats,
    const TCHAR* gpuMaterialToken)
{
    const int32 rootCount = FMath::Min(cpuRootStats.Num(), MAX_CPU_ROOTS);
    mCpuCommands.Reserve(rootCount);
    mCpuTokens.Reserve(rootCount);
    for (int32 index = 0; index < rootCount; ++index)
    {
        mCpuCommands.Add(FString::Printf(TEXT("stat dumpave -num=%d -ms=0 -root=%s"), DUMP_FRAMES,
            cpuRootStats[index]));
        mCpuTokens.Add(FString::Printf(TEXT(" - %s - "), cpuRootStats[index]));
    }
    if (gpuMaterialToken != nullptr)
    {
        mMaterialToken = gpuMaterialToken;
    }
    mGpuCaptures.Reserve(CAPTURE_GPU_RESERVE);
}

//////////////////////////////////////////////////////////////////////////
// Stops listening to the log
FSOLUiProfileCapture::~FSOLUiProfileCapture()
{
    if (mIsListening && GLog != nullptr)
    {
        GLog->RemoveOutputDevice(this);
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts listening to the log and pins the ProfileGPU settings the parser relies on
void FSOLUiProfileCapture::Start(UWorld* world)
{
    if (!mIsListening && GLog != nullptr)
    {
        GLog->AddOutputDevice(this);
        mIsListening = true;
    }
    for (const TCHAR* command : CAPTURE_GPU_SETUP_CMDS)
    {
        RunCommand(command, world);
    }
}

//////////////////////////////////////////////////////////////////////////
// Restores the ProfileGPU visualizer setting and stops listening
void FSOLUiProfileCapture::Stop(UWorld* world)
{
    RunCommand(CAPTURE_GPU_UI_RESTORE_CMD, world);
    if (mIsListening && GLog != nullptr)
    {
        GLog->RemoveOutputDevice(this);
    }
    mIsListening = false;
    mPhase = EPhase::Idle;
}

//////////////////////////////////////////////////////////////////////////
// Starts a capture pass: the GPU frames first, then the CPU windows
void FSOLUiProfileCapture::BeginPass()
{
    {
        FScopeLock lock(&mLogLock);
        mGpuCaptures.Reset();
        mIsCpuArmed = false;
        mIsCpuReading = false;
    }
    mResult = FPassResult();
    mPhase = EPhase::Gpu;
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    mWindowTimeS = 0.0;
    mWindowFrames = 0;
    mCpuIndex = 0;
    mGpuRequested = 0;
    mGpuLastFrame = 0;
    mCpuStartFrame = INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the pass; returns true once the pass is done and its result is set
bool FSOLUiProfileCapture::TickPass(UWorld* world, const float realDeltaSeconds, const TFunctionRef<bool()> canStart)
{
    mPhaseTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    switch (mPhase)
    {
    case EPhase::Gpu:
    {
        // One ProfileGPU at least a gap apart, each once the caller allows it; then wait for their tables (printed
        // later, on another thread)
        const bool bGapDone = mGpuRequested == 0 || mPhaseFrames - mGpuLastFrame >= CAPTURE_GPU_GAP_FRAMES;
        if (mGpuRequested < GPU_CAPTURES && bGapDone && canStart())
        {
            RunCommand(CAPTURE_GPU_CMD, world);
            ++mGpuRequested;
            mGpuLastFrame = mPhaseFrames;
        }
        int32 received = 0;
        {
            FScopeLock lock(&mLogLock);
            received = mGpuCaptures.Num();
        }
        const bool bAllSent = mGpuRequested == GPU_CAPTURES && mPhaseFrames - mGpuLastFrame >= CAPTURE_GPU_TAIL_FRAMES;
        if (bAllSent && (received >= GPU_CAPTURES || mPhaseTimeS >= CAPTURE_GPU_TIMEOUT_S))
        {
            mPhase = mCpuCommands.IsEmpty() ? EPhase::Done : EPhase::Cpu;
            mPhaseTimeS = 0.0;
            mPhaseFrames = 0;
            if (mPhase == EPhase::Done)
            {
                FinishPass();
                return true;
            }
        }
        return false;
    }
    case EPhase::Cpu:
    {
        // One stat dumpave window per root stat, back to back, each once the caller allows it; its lines arrive after
        // the window, on the stats thread
        if (mCpuStartFrame == INDEX_NONE)
        {
            if (!canStart())
            {
                return false;
            }
            {
                FScopeLock lock(&mLogLock);
                mCpuToken = mCpuTokens[mCpuIndex];
                mCpuReadMs = -1.0;
                mIsCpuArmed = true;
                mIsCpuReading = false;
            }
            RunCommand(*mCpuCommands[mCpuIndex], world);
            mCpuStartFrame = mPhaseFrames;
            return false;
        }
        const int32 windowFrames = mPhaseFrames - mCpuStartFrame;
        if (windowFrames <= DUMP_FRAMES)
        {
            mWindowTimeS += realDeltaSeconds;
            ++mWindowFrames;
        }
        bool bRead = false;
        double readMs = -1.0;
        {
            FScopeLock lock(&mLogLock);
            bRead = mIsCpuReading && mCpuReadMs >= 0.0;
            readMs = mCpuReadMs;
        }
        const int32 doneFrames = DUMP_FRAMES + CAPTURE_DUMP_TAIL_FRAMES;
        if ((windowFrames >= doneFrames && bRead) || windowFrames >= doneFrames + CAPTURE_DUMP_TIMEOUT_FRAMES)
        {
            mResult.CpuMs[mCpuIndex] = readMs;
            {
                FScopeLock lock(&mLogLock);
                mIsCpuArmed = false;
                mIsCpuReading = false;
            }
            ++mCpuIndex;
            mCpuStartFrame = INDEX_NONE;
            if (mCpuIndex >= mCpuCommands.Num())
            {
                FinishPass();
                mPhase = EPhase::Done;
                return true;
            }
        }
        return false;
    }
    case EPhase::Done:
        return true;
    default:
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the result from the captures read: the median GPU frame and the CPU windows
void FSOLUiProfileCapture::FinishPass()
{
    TArray<FGpuCapture> captures;
    {
        FScopeLock lock(&mLogLock);
        captures = mGpuCaptures;
    }
    mResult.GpuUiAll.Reset();
    for (const FGpuCapture& capture : captures)
    {
        mResult.GpuUiAll += FString::Printf(TEXT("%s%.3f"), mResult.GpuUiAll.IsEmpty() ? TEXT("") : TEXT("/"),
            capture.UiMs);
    }
    const int32 median = MedianUiIndex(captures);
    mResult.Gpu = median != INDEX_NONE ? captures[median] : FGpuCapture();
    mResult.FrameMs = mWindowFrames > 0 ? 1000.0 * mWindowTimeS / mWindowFrames : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Runs a console command in the world
void FSOLUiProfileCapture::RunCommand(const TCHAR* command, UWorld* world)
{
    if (GEngine != nullptr)
    {
        GEngine->Exec(world, command);
    }
}

//////////////////////////////////////////////////////////////////////////
// Reads the stat dump and GPU profile lines this capture asked for (any thread)
void FSOLUiProfileCapture::Serialize(const TCHAR* text, ELogVerbosity::Type /*verbosity*/, const FName& category)
{
    static const FName LOG_STATS(TEXT("LogStats"));
    static const FName LOG_RHI(TEXT("LogRHI"));
    if (text == nullptr || (category != LOG_STATS && category != LOG_RHI))
    {
        return;
    }
    FScopeLock lock(&mLogLock);
    if (category == LOG_STATS)
    {
        // "---- 240 frames, average ----", then "  0.123ms (   1)  -  <name> - <stat> - <group> - <category>" lines
        if (mIsCpuArmed && FCString::Strstr(text, TEXT("frames, average")) != nullptr)
        {
            mIsCpuArmed = false;
            mIsCpuReading = true;
            mCpuReadMs = -1.0;
        }
        else if (mIsCpuReading && !mCpuToken.IsEmpty() && FCString::Strstr(text, *mCpuToken) != nullptr)
        {
            mCpuReadMs = FMath::Max(mCpuReadMs, 0.0) + FCString::Atod(text);
        }
        return;
    }

    // ProfileGPU: "GPU Profile for Frame N - Graphics pipeline 0 - GPU 0", the table rows, then the bottom rule
    if (FCString::Strstr(text, TEXT("GPU Profile for Frame")) != nullptr)
    {
        mIsGpuReading = FCString::Strstr(text, TEXT("Graphics pipeline")) != nullptr;
        mGpuReading = FGpuCapture();
    }
    else if (mIsGpuReading && FCString::Strchr(text, CAPTURE_BOX_END) != nullptr)
    {
        mGpuCaptures.Add(mGpuReading);
        mIsGpuReading = false;
    }
    else if (mIsGpuReading && FCString::Strchr(text, CAPTURE_BOX_COLUMN) != nullptr)
    {
        ParseGpuRow(text);
    }
}

//////////////////////////////////////////////////////////////////////////
// Parses one ProfileGPU table row into the capture being read (called with the lock held)
void FSOLUiProfileCapture::ParseGpuRow(const TCHAR* text)
{
    // "| exclusive cells | inclusive cells | event name |": the inclusive cells are draws, dispatches, primitives,
    // vertices, percent and the time ("0.115 ms")
    const TCHAR columnRule[] = { CAPTURE_BOX_COLUMN, 0 };
    const TCHAR cellRule[] = { CAPTURE_BOX_CELL, 0 };
    TArray<FString> columns;
    FString(text).ParseIntoArray(columns, columnRule, false);
    if (columns.Num() < 4)
    {
        return;
    }
    TArray<FString> cells;
    columns[2].ParseIntoArray(cells, cellRule, false);
    if (cells.Num() < 6)
    {
        return;
    }
    const FString name = columns[3].TrimStartAndEnd();
    const double timeMs = FCString::Atod(*cells.Last().TrimStart());
    const int32 draws = FCString::Atoi(*cells[0].TrimStart());
    if (name == TEXT("<root>"))
    {
        mGpuReading.FrameMs = timeMs;
    }
    else if (name.StartsWith(TEXT("SlateUI Title = ")) && !name.Contains(TEXT("Visualizer")) && mGpuReading.UiMs < 0.0)
    {
        mGpuReading.UiMs = timeMs;
        mGpuReading.UiDraws = draws;
    }
    else if (name == TEXT("RenderGraphExecute - Slate") && mGpuReading.SlateMs < 0.0)
    {
        mGpuReading.SlateMs = timeMs;
    }
    else if (!mMaterialToken.IsEmpty() && name.StartsWith(CAPTURE_MATERIAL_EVENT) && name.Contains(mMaterialToken))
    {
        mGpuReading.MaterialMs += timeMs;
    }
}
