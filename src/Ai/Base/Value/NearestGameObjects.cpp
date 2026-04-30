/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "NearestGameObjects.h"

#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "SpellMgr.h"

namespace
{
    // Captures GUIDs directly during the grid visit so we never hold raw
    // GameObject* pointers between the visit and dereference. Returning false
    // skips the searcher's own container insertion — see GuidCollectorCheck
    // in NearestUnitsValue.cpp for rationale.
    class GameObjectGuidCollectorCheck
    {
    public:
        GameObjectGuidCollectorCheck(WorldObject const* origin, float range, GuidVector& out)
            : _origin(origin), _range(range), _out(out) {}

        bool operator()(GameObject* go)
        {
            if (!go || !go->IsInWorld())
                return false;
            if (!_origin->IsWithinDistInMap(go, _range))
                return false;
            _out.push_back(go->GetGUID());
            return false;
        }

    private:
        WorldObject const* _origin;
        float _range;
        GuidVector& _out;
    };
}

GuidVector NearestGameObjects::Calculate()
{
    GuidVector result;

    if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld() || !bot->GetMap())
        return result;

    NearestObjectCache::CacheKey key = NearestObjectCache::MakeKey(
        bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY());

    GuidVector cachedGOs;
    if (!sNearestObjectCache.TryGetGameObjects(key, cachedGOs))
    {
        // Cache miss — scan with extended radius to cover all bots in this 40y bucket.
        float scanRange = sPlayerbotAIConfig.sightDistance + NEAREST_CACHE_BUCKET_SIZE * 1.5f;

        GuidVector freshGOs;
        std::list<GameObject*> dummy;
        GameObjectGuidCollectorCheck go_check(bot, scanRange, freshGOs);
        Acore::GameObjectListSearcher<GameObjectGuidCollectorCheck> searcher(bot, dummy, go_check);
        Cell::VisitObjects(bot, searcher, scanRange);

        cachedGOs = freshGOs;
        sNearestObjectCache.StoreGameObjects(key, std::move(freshGOs));
    }

    for (ObjectGuid const& guid : cachedGOs)
    {
        GameObject* go = bot->GetMap()->GetGameObject(guid);
        if (!go || !go->isSpawned() || !go->GetGOInfo())
            continue;
        if (!bot->IsWithinDistInMap(go, range))
            continue;
        result.push_back(guid);
    }
    return result;
}

GuidVector NearestTrapWithDamageValue::Calculate()
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(bot, range);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, targets, u_check);
    Cell::VisitObjects(bot, searcher, range);

    GuidVector result;
    for (GameObject* go : targets)
    {
        if (go->GetGoType() != GAMEOBJECT_TYPE_TRAP)
        {
            continue;
        }
        Unit* owner = go->GetOwner();
        if (owner && owner->IsFriendlyTo(bot))
        {
            continue;
        }
        const GameObjectTemplate* goInfo = go->GetGOInfo();
        if (!goInfo)
        {
            continue;
        }
        uint32 spellId = goInfo->trap.spellId;
        if (!spellId)
        {
            continue;
        }
        const SpellInfo* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->IsPositive())
        {
            continue;
        }
        for (int i = 0; i < MAX_SPELL_EFFECTS; i++)
        {
            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_APPLY_AURA)
            {
                if (spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE)
                {
                    result.push_back(go->GetGUID());
                    break;
                }
            }
            else if (spellInfo->Effects[i].Effect == SPELL_EFFECT_SCHOOL_DAMAGE)
            {
                result.push_back(go->GetGUID());
                break;
            }
        }
    }
    return result;
}
