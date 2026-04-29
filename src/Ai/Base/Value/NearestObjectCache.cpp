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

NearestObjectCache::Entry const* NearestObjectCache::GetUnits(CacheKey key) const
{
    auto it = _cache.find(key);
    if (it == _cache.end())
        return nullptr;

    Entry const& e = it->second;
    if (!e.unitsTimestamp || getMSTimeDiff(e.unitsTimestamp, getMSTime()) >= NEAREST_CACHE_TTL_MS)
        return nullptr;

    return &e;
}

NearestObjectCache::Entry const* NearestObjectCache::GetGameObjects(CacheKey key) const
{
    auto it = _cache.find(key);
    if (it == _cache.end())
        return nullptr;

    Entry const& e = it->second;
    if (!e.goTimestamp || getMSTimeDiff(e.goTimestamp, getMSTime()) >= NEAREST_CACHE_TTL_MS)
        return nullptr;

    return &e;
}

NearestObjectCache::Entry* NearestObjectCache::GetOrCreateForUnits(CacheKey key)
{
    Entry& e = _cache[key];
    e.units.clear();
    e.unitsTimestamp = 0;
    return &e;
}

NearestObjectCache::Entry* NearestObjectCache::GetOrCreateForGameObjects(CacheKey key)
{
    Entry& e = _cache[key];
    e.gameObjects.clear();
    e.goTimestamp = 0;
    return &e;
}
