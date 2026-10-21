/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatGrid.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the integer cell coordinate of a metre value (saturating so huge inputs cannot overflow)
    int64 CellCoord(double valueM, double cellSizeM)
    {
        const double cell = FMath::FloorToDouble(valueM / cellSizeM);
        return static_cast<int64>(FMath::Clamp(cell, -4.0e18, 4.0e18));
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates an empty grid
SOLCombatGrid::FGrid::FGrid(double cellSizeM)
    : mCellSizeM(cellSizeM > 0.0 && FMath::IsFinite(cellSizeM) ? cellSizeM : 1.0)
{
}

//////////////////////////////////////////////////////////////////////////
// Mixes three cell coordinates into one 64-bit key
uint64 SOLCombatGrid::FGrid::CellKey(int64 x, int64 y, int64 z)
{
    uint64 h = static_cast<uint64>(x) * 0x9E3779B97F4A7C15ull;
    h ^= (static_cast<uint64>(y) + 0x7F4A7C15ull + (h << 6) + (h >> 2)) * 0xC2B2AE3D27D4EB4Full;
    h ^= (static_cast<uint64>(z) + 0x165667B1ull + (h << 6) + (h >> 2)) * 0x9E3779B97F4A7C15ull;
    return h;
}

//////////////////////////////////////////////////////////////////////////
// Returns the first probe slot of a key (the table size is a power of two)
int32 SOLCombatGrid::FGrid::ProbeStart(uint64 key) const
{
    const uint64 mixed = (key ^ (key >> 29)) * 0xBF58476D1CE4E5B9ull;
    return static_cast<int32>((mixed >> 32) & static_cast<uint64>(mSlotHeads.Num() - 1));
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the cell table at twice its size (or the given minimum), keeping every key's list
void SOLCombatGrid::FGrid::GrowTable(int32 minimumSlots)
{
    const TArray<uint64> oldKeys = MoveTemp(mSlotKeys);
    const TArray<int32> oldHeads = MoveTemp(mSlotHeads);
    const int32 slots = FMath::RoundUpToPowerOfTwo(FMath::Max3(minimumSlots, oldHeads.Num() * 2, 64));
    mSlotKeys.SetNumUninitialized(slots);
    mSlotHeads.Init(INDEX_NONE, slots);
    for (int32 slot = 0; slot < oldHeads.Num(); ++slot)
    {
        if (oldHeads[slot] != INDEX_NONE)
        {
            int32 probe = ProbeStart(oldKeys[slot]);
            while (mSlotHeads[probe] != INDEX_NONE)
            {
                probe = (probe + 1) & (slots - 1);
            }
            mSlotKeys[probe] = oldKeys[slot];
            mSlotHeads[probe] = oldHeads[slot];
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the head slot of a cell key's list, adding the key (head INDEX_NONE) when absent
int32& SOLCombatGrid::FGrid::FindOrAddHead(uint64 key)
{
    // Keep the table at most half full so probe runs stay short
    if ((mOccupiedSlots + 1) * 2 > mSlotHeads.Num())
    {
        GrowTable(0);
    }
    const int32 mask = mSlotHeads.Num() - 1;
    int32 probe = ProbeStart(key);
    while (mSlotHeads[probe] != INDEX_NONE)
    {
        if (mSlotKeys[probe] == key)
        {
            return mSlotHeads[probe];
        }
        probe = (probe + 1) & mask;
    }
    ++mOccupiedSlots;
    mSlotKeys[probe] = key;
    return mSlotHeads[probe];
}

//////////////////////////////////////////////////////////////////////////
// Returns the head of a cell key's list, or INDEX_NONE when the cell is empty
int32 SOLCombatGrid::FGrid::FindHead(uint64 key) const
{
    if (mOccupiedSlots == 0)
    {
        return INDEX_NONE;
    }
    const int32 mask = mSlotHeads.Num() - 1;
    for (int32 probe = ProbeStart(key); mSlotHeads[probe] != INDEX_NONE; probe = (probe + 1) & mask)
    {
        if (mSlotKeys[probe] == key)
        {
            return mSlotHeads[probe];
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Pre-allocates the cell table and the entry array
void SOLCombatGrid::FGrid::Reserve(int32 cellCount, int32 entryCount)
{
    if (FMath::Max(cellCount, 0) * 2 > mSlotHeads.Num())
    {
        GrowTable(FMath::Max(cellCount, 0) * 2);
    }
    mEntries.Reserve(FMath::Max(entryCount, 0));
    mAllIds.Reserve(FMath::Max(entryCount, 0));
}

//////////////////////////////////////////////////////////////////////////
// Removes every object, keeping the table's and the arrays' allocations
void SOLCombatGrid::FGrid::Clear()
{
    if (mOccupiedSlots > 0)
    {
        FMemory::Memset(mSlotHeads.GetData(), 0xFF, mSlotHeads.Num() * sizeof(int32));
        mOccupiedSlots = 0;
    }
    mEntries.Reset();
    mAllIds.Reset();
    mOversizedIds.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Adds the object to every cell its bounding box covers
void SOLCombatGrid::FGrid::Insert(int32 id, const FVector3d& center, double radius)
{
    mAllIds.Add(id);
    const double r = radius > 0.0 && FMath::IsFinite(radius) ? radius : 0.0;
    const int64 x0 = CellCoord(center.X - r, mCellSizeM), x1 = CellCoord(center.X + r, mCellSizeM);
    const int64 y0 = CellCoord(center.Y - r, mCellSizeM), y1 = CellCoord(center.Y + r, mCellSizeM);
    const int64 z0 = CellCoord(center.Z - r, mCellSizeM), z1 = CellCoord(center.Z + r, mCellSizeM);

    // Doubles guard the product against int64 overflow for huge objects
    const double cellCount = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1)
        * static_cast<double>(z1 - z0 + 1);
    if (cellCount > static_cast<double>(MAX_CELLS_PER_OPERATION))
    {
        mOversizedIds.Add(id);
        return;
    }

    // Push the id onto the front of each covered cell's list
    for (int64 x = x0; x <= x1; ++x)
    {
        for (int64 y = y0; y <= y1; ++y)
        {
            for (int64 z = z0; z <= z1; ++z)
            {
                int32& head = FindOrAddHead(CellKey(x, y, z));
                FEntry entry;
                entry.Id = id;
                entry.Next = head;
                head = mEntries.Add(entry);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Collects the ids stored in the cells the padded segment's bounding box covers
void SOLCombatGrid::FGrid::QueryCandidates(const FVector3d& segmentStart, const FVector3d& segmentEnd, double padding,
    TArray<int32>& outIds) const
{
    outIds.Reset();
    const double pad = padding > 0.0 && FMath::IsFinite(padding) ? padding : 0.0;
    const FVector3d lo = segmentStart.ComponentMin(segmentEnd) - FVector3d(pad);
    const FVector3d hi = segmentStart.ComponentMax(segmentEnd) + FVector3d(pad);
    const int64 x0 = CellCoord(lo.X, mCellSizeM), x1 = CellCoord(hi.X, mCellSizeM);
    const int64 y0 = CellCoord(lo.Y, mCellSizeM), y1 = CellCoord(hi.Y, mCellSizeM);
    const int64 z0 = CellCoord(lo.Z, mCellSizeM), z1 = CellCoord(hi.Z, mCellSizeM);

    const double cellCount = static_cast<double>(x1 - x0 + 1) * static_cast<double>(y1 - y0 + 1)
        * static_cast<double>(z1 - z0 + 1);
    if (cellCount > static_cast<double>(MAX_CELLS_PER_OPERATION))
    {
        // Append, not assign: copy-assignment resizes the buffer to fit exactly, which would reallocate a caller's
        // reserved scratch on a worker thread; Append into the reset array reuses it while it is large enough
        outIds.Append(mAllIds);
        return;
    }

    // Gather every list the box covers; an object spanning several cells is gathered once per cell
    outIds.Append(mOversizedIds);
    for (int64 x = x0; x <= x1; ++x)
    {
        for (int64 y = y0; y <= y1; ++y)
        {
            for (int64 z = z0; z <= z1; ++z)
            {
                for (int32 entry = FindHead(CellKey(x, y, z)); entry != INDEX_NONE;
                    entry = mEntries[entry].Next)
                {
                    outIds.Add(mEntries[entry].Id);
                }
            }
        }
    }

    // De-duplicate in place (sort, then keep the first of each run), so the query allocates nothing extra
    if (outIds.Num() > 1)
    {
        outIds.Sort();
        int32 write = 1;
        for (int32 read = 1; read < outIds.Num(); ++read)
        {
            if (outIds[read] != outIds[write - 1])
            {
                outIds[write++] = outIds[read];
            }
        }
        outIds.SetNum(write, EAllowShrinking::No);
    }
}
