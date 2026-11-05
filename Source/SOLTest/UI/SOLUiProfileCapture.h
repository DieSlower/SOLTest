/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Misc/OutputDevice.h"
#include "Templates/Function.h"

class UWorld;

/**
 * Verification-only capture of the UI's cost in a standalone -game run, shared by the profiling smokes
 * (-SOLSmokeHoloMenuProfile, SDD 42 section 11; -SOLSmokeSolidMenuProfile, SDD 46 section 13). It is an output device:
 * Start registers it with GLog and pins the ProfileGPU table settings its parser relies on, Stop restores the
 * visualizer and unregisters it. One capture pass at a time (BeginPass, then TickPass every frame) runs GPU_CAPTURES
 * ProfileGPU frames, then one `stat dumpave -num=DUMP_FRAMES -ms=0 -root=<stat>` window per root stat, and reads the
 * numbers back from the log (LogRHI and LogStats lines, on any thread). The caller decides when each capture may start
 * (canStart: a settled menu, a card refreshed and given its open time) and what the numbers are compared against.
 */
class SOLTEST_API FSOLUiProfileCapture : public FOutputDevice
{
public:

    static constexpr int32 MAX_CPU_ROOTS = 4;               // Root stats one pass can read
    static constexpr int32 GPU_CAPTURES = 3;                // ProfileGPU frames per pass; the median is reported
    static constexpr int32 DUMP_FRAMES = 240;               // Frames of each stat dumpave window

    // One ProfileGPU frame's UI numbers (ms; -1 when the event was not in the profile)
    struct FGpuCapture
    {
        double UiMs = -1.0;         // "SlateUI Title = <game window>" inclusive: the game window's Slate pass
        double SlateMs = -1.0;      // "RenderGraphExecute - Slate" inclusive
        double MaterialMs = 0.0;    // Sum of the "Slate Material: " draws whose material name has the token
        double FrameMs = -1.0;      // <root> inclusive
        int32 UiDraws = 0;          // Draws in the game window's Slate pass
    };

    // One pass's numbers
    struct FPassResult
    {
        double CpuMs[MAX_CPU_ROOTS] = { -1.0, -1.0, -1.0, -1.0 };  // Inclusive average per root stat; -1 if not read
        FGpuCapture Gpu;                                            // The median frame by UiMs (all -1 if none read)
        FString GpuUiAll;                                           // Every frame's UiMs, '/'-separated
        double FrameMs = 0.0;                                       // Mean real frame time over the stat windows
    };

    // Builds the stat dumpave command and log token of each root stat (at most MAX_CPU_ROOTS, e.g.
    // STAT_SlateDrawWindowTime) and keeps the material name token whose Slate draws are summed into MaterialMs (empty:
    // none)
    FSOLUiProfileCapture(TConstArrayView<const TCHAR*> cpuRootStats, const TCHAR* gpuMaterialToken);

    // Stops listening to the log
    virtual ~FSOLUiProfileCapture() override;

    // Starts listening to the log and pins the ProfileGPU settings the parser relies on
    void Start(UWorld* world);

    // Restores the ProfileGPU visualizer setting and stops listening
    void Stop(UWorld* world);

    // Starts a capture pass: the GPU frames first, then the CPU windows
    void BeginPass();

    // Runs one frame of the pass; canStart is asked only when a capture (a ProfileGPU frame or a stat window) is about
    // to start, and the capture starts when it returns true; returns true once the pass is done and its result is set
    bool TickPass(UWorld* world, float realDeltaSeconds, TFunctionRef<bool()> canStart);

    // Returns true while the pass's GPU frames or CPU windows run
    bool IsCapturing() const { return mPhase == EPhase::Gpu || mPhase == EPhase::Cpu; }

    // Returns the last finished pass's numbers
    const FPassResult& GetResult() const { return mResult; }

    // Runs a console command in the world
    static void RunCommand(const TCHAR* command, UWorld* world);

    // Reads the stat dump and GPU profile lines this capture asked for (any thread)
    virtual void Serialize(const TCHAR* text, ELogVerbosity::Type verbosity, const FName& category) override;

    // The log may call in from any thread; Serialize locks
    virtual bool CanBeUsedOnAnyThread() const override { return true; }

private:

    // Where a pass is
    enum class EPhase : uint8
    {
        Idle,           // No pass running
        Gpu,            // ProfileGPU frames
        Cpu,            // stat dumpave windows, one per root stat
        Done,           // The result is set
    };

    // Sets the result from the captures read: the median GPU frame and the CPU windows
    void FinishPass();

    // Parses one ProfileGPU table row into the capture being read (called with the lock held)
    void ParseGpuRow(const TCHAR* text);

    TArray<FString> mCpuCommands;                           // stat dumpave command per root stat
    TArray<FString> mCpuTokens;                             // " - <stat> - " per root stat
    FString mMaterialToken;                                 // "Slate Material: *<token>*" events summed; empty: none
    FPassResult mResult;
    double mPhaseTimeS = 0.0;                               // Real time in the phase
    double mWindowTimeS = 0.0;                              // Real time over the stat dumpave windows
    int32 mWindowFrames = 0;                                // Frames over the stat dumpave windows
    int32 mPhaseFrames = 0;                                 // Frames in the phase
    int32 mCpuIndex = 0;                                    // Root stat of the stat dumpave window running
    int32 mGpuRequested = 0;                                // ProfileGPU commands run in this pass
    int32 mGpuLastFrame = 0;                                // Phase frame of the last ProfileGPU command
    int32 mCpuStartFrame = INDEX_NONE;                      // Phase frame the running stat dumpave started; none yet
    EPhase mPhase = EPhase::Idle;
    bool mIsListening = false;                              // Registered with GLog

    // Read from the log (any thread), under mLogLock
    FCriticalSection mLogLock;
    TArray<FGpuCapture> mGpuCaptures;                       // This pass's finished ProfileGPU frames, in order
    FGpuCapture mGpuReading;                                // The one being read
    FString mCpuToken;                                      // The token of the stat dumpave asked for
    double mCpuReadMs = -1.0;                               // Its inclusive average, summed over its root lines
    bool mIsGpuReading = false;
    bool mIsCpuArmed = false;                               // Waiting for the dump's header
    bool mIsCpuReading = false;                             // Header seen, reading its lines
};
