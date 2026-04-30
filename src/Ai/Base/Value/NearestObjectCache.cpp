/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NearestObjectCache.h"

NearestObjectCache::CacheKey NearestObjectCache::MakeKey(uint32 mapId, float x, float y)
{
    int32 bx = static_cast<int32>(std::floor(x / NEAREST_CACHE_BUCKET_SIZE));
    int32 by = static_cast<int32>(std::floor(y / NEAREST_CACHE_BUCKET_SIZE));

    uint64_t kmap = static_cast<uint64_t>(mapId & 0xFFFFu) << 32u;
    uint64_t kbx  = static_cast<uint64_t>((bx + 512) & 0xFFFFu) << 16u;
    uint64_t kby  = static_cast<uint64_t>((by + 512) & 0xFFFFu);
    return kmap | kbx | kby;
}

NearestObjectCache::GuidSnapshot NearestObjectCache::TryGetUnits(CacheKey key) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    auto it = _cache.find(key);
    if (it == _cache.end())
        return GuidSnapshot();

    Entry const& e = it->second;
    if (!e.unitsTimestamp || getMSTimeDiff(e.unitsTimestamp, getMSTime()) >= NEAREST_CACHE_TTL_MS)
        return GuidSnapshot();

    return e.units;
}

NearestObjectCache::GuidSnapshot NearestObjectCache::TryGetGameObjects(CacheKey key) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);

    auto it = _cache.find(key);
    if (it == _cache.end())
        return GuidSnapshot();

    Entry const& e = it->second;
    if (!e.goTimestamp || getMSTimeDiff(e.goTimestamp, getMSTime()) >= NEAREST_CACHE_TTL_MS)
        return GuidSnapshot();

    return e.gameObjects;
}

NearestObjectCache::GuidSnapshot NearestObjectCache::StoreUnits(CacheKey key, GuidVector value)
{
    auto snap = std::make_shared<GuidVector const>(std::move(value));
    uint32 const now = getMSTime();

    std::unique_lock<std::shared_mutex> lock(_mutex);

    PruneStaleLocked(now);

    Entry& e = _cache[key];
    e.units = snap;
    e.unitsTimestamp = now;
    return snap;
}

NearestObjectCache::GuidSnapshot NearestObjectCache::StoreGameObjects(CacheKey key, GuidVector value)
{
    auto snap = std::make_shared<GuidVector const>(std::move(value));
    uint32 const now = getMSTime();

    std::unique_lock<std::shared_mutex> lock(_mutex);

    PruneStaleLocked(now);

    Entry& e = _cache[key];
    e.gameObjects = snap;
    e.goTimestamp = now;
    return snap;
}

void NearestObjectCache::PruneStaleLocked(uint32 now)
{
    if (_cache.size() <= 500)
        return;

    for (auto it = _cache.begin(); it != _cache.end();)
    {
        Entry const& ce = it->second;
        bool unitStale = !ce.unitsTimestamp || getMSTimeDiff(ce.unitsTimestamp, now) > 5000;
        bool goStale   = !ce.goTimestamp    || getMSTimeDiff(ce.goTimestamp,    now) > 5000;
        if (unitStale && goStale)
            it = _cache.erase(it);
        else
            ++it;
    }
}
