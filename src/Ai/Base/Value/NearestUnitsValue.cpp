/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NearestUnitsValue.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"

namespace
{
    // Captures GUIDs directly during the grid visit, while the Unit* is
    // guaranteed to still be linked in the cell. Returning false from the
    // check prevents UnitListSearcher from inserting the raw pointer into
    // a container that would otherwise be iterated after the visit ends —
    // a window in which the pointer could be invalidated before GetGUID().
    class GuidCollectorCheck
    {
    public:
        GuidCollectorCheck(WorldObject const* origin, float range, GuidVector& out)
            : _origin(origin), _range(range), _out(out) {}

        bool operator()(Unit* u)
        {
            if (!u || !u->IsInWorld() || u->IsDuringRemoveFromWorld())
                return false;
            if (!u->IsAlive())
                return false;
            if (!_origin->IsWithinDistInMap(u, _range))
                return false;
            _out.push_back(u->GetGUID());
            return false;
        }

    private:
        WorldObject const* _origin;
        float _range;
        GuidVector& _out;
    };
}

GuidVector NearestUnitsValue::Calculate()
{
    GuidVector results;

    if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
        return results;

    if (!UseUnitCache())
    {
        // Direct scan path for subclasses whose FindUnits uses a narrower checker
        // (AnyFriendlyUnit, AnyUnfriendlyUnit, AnyDeadUnit).
        std::list<Unit*> targets;
        FindUnits(targets);
        for (Unit* unit : targets)
        {
            if (!unit || !unit->IsInWorld() || unit->IsDuringRemoveFromWorld())
                continue;
            if (AcceptUnit(unit) && (ignoreLos || bot->IsWithinLOSInMap(unit)))
                results.push_back(unit->GetGUID());
        }
        return results;
    }

    NearestObjectCache::CacheKey key = NearestObjectCache::MakeKey(
        bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY());

    GuidVector cachedUnits;
    if (!sNearestObjectCache.TryGetUnits(key, cachedUnits))
    {
        // Cache miss — scan with extended radius to cover all bots in this 40y bucket,
        // collect GUIDs into a local vector during the visit (so we never hold raw
        // Unit* pointers across iteration), then publish the snapshot under the cache lock.
        float scanRange = sPlayerbotAIConfig.sightDistance + NEAREST_CACHE_BUCKET_SIZE * 1.5f;

        GuidVector freshUnits;
        std::list<Unit*> dummy;
        GuidCollectorCheck u_check(bot, scanRange, freshUnits);
        Acore::UnitListSearcher<GuidCollectorCheck> searcher(bot, dummy, u_check);
        Cell::VisitObjects(bot, searcher, scanRange);

        cachedUnits = freshUnits;
        sNearestObjectCache.StoreUnits(key, std::move(freshUnits));
    }

    // Per-bot resolution: re-check range from this bot's actual position, then AcceptUnit + LOS.
    // IsWithinDistInMap uses squared distance (no sqrt) — cheap even for large lists.
    for (ObjectGuid const& guid : cachedUnits)
    {
        Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
        if (!unit || !unit->IsInWorld() || unit->IsDuringRemoveFromWorld())
            continue;
        if (!bot->IsWithinDistInMap(unit, range))
            continue;
        if (!AcceptUnit(unit))
            continue;
        if (!ignoreLos && !bot->IsWithinLOSInMap(unit))
            continue;
        results.push_back(guid);
    }

    return results;
}
