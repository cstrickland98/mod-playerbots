/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_NEARESTOBJECTCACHE_H
#define _PLAYERBOT_NEARESTOBJECTCACHE_H

#include "ObjectGuid.h"
#include "Timer.h"

#include <cmath>
#include <unordered_map>

// Side length in yards of each spatial bucket. Bots within the same 40x40y square share one scan.
// Bots at opposite corners are ≤57y apart; the extended scan radius (sightDistance + BUCKET_SIZE*1.5)
// covers the full bucket from any bot inside it.
constexpr float NEAREST_CACHE_BUCKET_SIZE = 40.0f;

// How long (ms) a cached entry remains valid. 200ms lets all bots on the same tick share one scan
// while keeping data fresh enough that moving units don't linger too long.
constexpr uint32 NEAREST_CACHE_TTL_MS = 200u;

class NearestObjectCache
{
public:
    // Packed 64-bit key: mapId(16b) | (bucketX+512)(16b) | (bucketY+512)(16b)
    // WoW coords ±17000y → bucket ±425 → +512 bias fits in uint16 with room to spare.
    using CacheKey = uint64_t;

    struct Entry
    {
        GuidVector units;
        GuidVector gameObjects;
        uint32 unitsTimestamp = 0; // 0 = not yet populated this cycle
        uint32 goTimestamp    = 0;
    };

    static NearestObjectCache& instance()
    {
        static NearestObjectCache inst;
        return inst;
    }

    static CacheKey MakeKey(uint32 mapId, float x, float y);

    // Returns a fresh entry (timestamp valid) or nullptr on cache miss.
    Entry const* GetUnits(CacheKey key) const;
    Entry const* GetGameObjects(CacheKey key) const;

    // Returns the entry slot to populate. Clears the relevant vector and resets the timestamp to 0.
    // The caller must set entry->unitsTimestamp = getMSTime() after populating.
    Entry* GetOrCreateForUnits(CacheKey key);
    Entry* GetOrCreateForGameObjects(CacheKey key);

private:
    NearestObjectCache() = default;

    std::unordered_map<CacheKey, Entry> _cache;
};

#define sNearestObjectCache NearestObjectCache::instance()

#endif // _PLAYERBOT_NEARESTOBJECTCACHE_H
