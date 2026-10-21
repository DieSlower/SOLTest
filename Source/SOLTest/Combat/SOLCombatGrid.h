/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic uniform spatial hash over double-precision positions (SDD 7): bolts query only the targets and minor bodies
// near their swept segment instead of testing the whole population.
namespace SOLCombatGrid
{
    // Maximum cells an insert or a query will enumerate before falling back to the "always a candidate" / "all ids" path
    inline constexpr int64 MAX_CELLS_PER_OPERATION = 4096;

    /**
     * Each cell is a singly linked list threaded through one flat entry array, and the cell table (open addressing,
     * linear probing, at most half full) holds plain ints, so Clear keeps every allocation and a rebuild of a similar
     * size allocates nothing: no per-cell arrays exist to be freed or regrown, and cells that went empty never linger
     * (the table itself is reset), however far the inserted objects move between rebuilds. QueryCandidates
     * de-duplicates in place (sort + unique on the output array), so a query allocates nothing either once the
     * caller's output array has grown to its working size. Queries are const and touch no shared state, so several
     * threads may query one grid at once.
     */
    class SOLTEST_API FGrid
    {
    public:
        // Creates a grid with the given cell edge length in metres (non-positive values are clamped to 1)
        explicit FGrid(double cellSizeM);

        // Pre-allocates room for this many occupied cells and cell entries, so rebuilds up to that size never allocate
        void Reserve(int32 cellCount, int32 entryCount);

        // Removes every inserted object (keeps allocations)
        void Clear();

        // Inserts an object's bounding sphere under the given id
        void Insert(int32 id, const FVector3d& center, double radius);

        // Fills outIds with the ids whose sphere may touch the segment expanded by padding (no false negatives, each id
        // once)
        void QueryCandidates(const FVector3d& segmentStart, const FVector3d& segmentEnd, double padding,
            TArray<int32>& outIds) const;

        // Returns the number of inserted objects
        int32 Num() const { return mAllIds.Num(); }

    private:
        // One id stored in one cell, linked to the cell's next entry (INDEX_NONE ends the list)
        struct FEntry
        {
            int32 Id = INDEX_NONE;
            int32 Next = INDEX_NONE;
        };

        // Returns the 64-bit hash key of an integer cell
        static uint64 CellKey(int64 x, int64 y, int64 z);

        // Returns the head slot of a cell key's list, adding the key (head INDEX_NONE) when absent
        int32& FindOrAddHead(uint64 key);

        // Returns the head of a cell key's list, or INDEX_NONE when the cell is empty
        int32 FindHead(uint64 key) const;

        // Returns the first probe slot of a key (the table size is a power of two)
        int32 ProbeStart(uint64 key) const;

        // Rebuilds the cell table at twice its size (or the given minimum), keeping every key's list
        void GrowTable(int32 minimumSlots);

        double mCellSizeM;
        TArray<uint64> mSlotKeys;           // Open-addressed cell table (linear probing): key of each slot
        TArray<int32> mSlotHeads;           // ...and the index of its first entry in mEntries (INDEX_NONE = empty slot)
        int32 mOccupiedSlots = 0;           // (SDD 11 Amendment 6: replaces a TMap, ~3x faster inserts and lookups)
        TArray<FEntry> mEntries;            // Every cell's linked list, rebuilt from scratch after each Clear
        TArray<int32> mAllIds;
        TArray<int32> mOversizedIds;        // Objects covering too many cells: always candidates
    };
}
