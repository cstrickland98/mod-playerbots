/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_NEARESTOBJECTCACHE_H
#define _PLAYERBOT_NEARESTOBJECTCACHE_H

#include "ObjectGuid.h"
#include "Timer.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

// Side length in yards of each spatial bucket. Bots within the same 40x40y square share one scan.
// Bots at opposite corners are ≤57y apart; the extended scan radius (sightDistance + BUCKET_SIZE*1.5)
// covers the full bucket from any bot inside it.
constexpr float NEAREST_CACHE_BUCKET_SIZE = 40.0f;

// How long (ms) a cached entry remains valid. 200ms lets all bots on the same tick share one scan
// while keeping data fresh enough that moving units don't linger too long.
constexpr uint32 NEAREST_CACHE_TTL_MS = 200u;

// Thread-safe singleton. The map is shared across all bot AI ticks and is reached from
// any thread that runs Map::Update for a map containing playerbots, so every access must
// hold _mutex.
//
// Cache hits return a shared_ptr<GuidVector> snapshot — one atomic refcount inc, no copy
// of the (potentially large, hundreds of GUIDs) vector. The shared_mutex lets concurrent
// readers proceed without serializing on the cache.
//
// Once a snapshot pointer is returned to a caller it is immutable: the cache only ever
// publishes new snapshots by replacing the pointer inside the entry, never by mutating
// the vector that an existing snapshot points at. So callers can iterate the snapshot
// freely outside any lock without worrying about a concurrent rehash or rewrite.
class NearestObjectCache
{
public:
    using GuidSnapshot = std::shared_ptr<GuidVector const>;

    // Packed 64-bit key: mapId(16b) | (bucketX+512)(16b) | (bucketY+512)(16b)
    // WoW coords ±17000y → bucket ±425 → +512 bias fits in uint16 with room to spare.
    using CacheKey = uint64_t;

    static NearestObjectCache& instance()
    {
        static NearestObjectCache inst;
        return inst;
    }

    static CacheKey MakeKey(uint32 mapId, float x, float y);

    // Returns a snapshot of the cached unit GUIDs for 'key' if a fresh entry exists.
    // Returns an empty shared_ptr on miss. The returned snapshot is immutable and safe
    // to iterate without holding any lock.
    GuidSnapshot TryGetUnits(CacheKey key) const;
    GuidSnapshot TryGetGameObjects(CacheKey key) const;

    // Atomically replaces the cached entry for 'key' with 'value' and stamps it now.
    // The caller passes the freshly scanned GUIDs by value; the cache wraps them in a
    // shared snapshot that subsequent readers can share without copying. The same
    // snapshot is returned to the caller so it can iterate the just-scanned list
    // without re-querying the cache (and without making a redundant copy).
    GuidSnapshot StoreUnits(CacheKey key, GuidVector value);
    GuidSnapshot StoreGameObjects(CacheKey key, GuidVector value);

private:
    NearestObjectCache() = default;

    struct Entry
    {
        GuidSnapshot units;
        GuidSnapshot gameObjects;
        uint32 unitsTimestamp = 0; // 0 = not yet populated
        uint32 goTimestamp    = 0;
    };

    void PruneStaleLocked(uint32 now);

    mutable std::shared_mutex _mutex;
    std::unordered_map<CacheKey, Entry> _cache;
};

#define sNearestObjectCache NearestObjectCache::instance()

#endif // _PLAYERBOT_NEARESTOBJECTCACHE_H
