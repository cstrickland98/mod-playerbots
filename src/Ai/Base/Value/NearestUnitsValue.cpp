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

GuidVector NearestUnitsValue::Calculate()
{
    GuidVector results;

    if (!UseUnitCache())
    {
        // Direct scan path for subclasses whose FindUnits uses a narrower checker
        // (AnyFriendlyUnit, AnyUnfriendlyUnit, AnyDeadUnit).
        std::list<Unit*> targets;
        FindUnits(targets);
        for (Unit* unit : targets)
            if (AcceptUnit(unit) && (ignoreLos || bot->IsWithinLOSInMap(unit)))
                results.push_back(unit->GetGUID());
        return results;
    }

    NearestObjectCache::CacheKey key = NearestObjectCache::MakeKey(
        bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY());

    NearestObjectCache::Entry const* fresh = sNearestObjectCache.GetUnits(key);
    if (!fresh)
    {
        // Cache miss — first bot in this bucket this tick triggers one scan.
        // Extended radius covers every point in the 40y bucket from any bot inside it.
        float scanRange = sPlayerbotAIConfig.sightDistance + NEAREST_CACHE_BUCKET_SIZE * 1.5f;
        NearestObjectCache::Entry* slot = sNearestObjectCache.GetOrCreateForUnits(key);

        std::list<Unit*> rawUnits;
        Acore::AnyUnitInObjectRangeCheck u_check(bot, scanRange);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(bot, rawUnits, u_check);
        Cell::VisitObjects(bot, searcher, scanRange);

        for (Unit* u : rawUnits)
            slot->units.push_back(u->GetGUID());
        slot->unitsTimestamp = getMSTime();
        fresh = slot;
    }

    // Per-bot resolution: re-check range from this bot's actual position, then AcceptUnit + LOS.
    // IsWithinDistInMap uses squared distance (no sqrt) — cheap even for large lists.
    for (ObjectGuid const& guid : fresh->units)
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
