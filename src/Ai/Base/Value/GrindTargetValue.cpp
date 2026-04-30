/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "GrindTargetValue.h"

#include "NewRpgInfo.h"
#include "Playerbots.h"
#include "ReputationMgr.h"
#include "ServerFacade.h"
#include "SharedDefines.h"

Unit* GrindTargetValue::Calculate()
{
    uint32 memberCount = 1;
    Group* group = bot->GetGroup();
    if (group)
        memberCount = group->GetMembersCount();

    Unit* target = nullptr;
    uint32 assistCount = 0;
    while (!target && assistCount < memberCount)
    {
        target = FindTargetForGrinding(assistCount++);
    }

    return target;
}

Unit* GrindTargetValue::FindTargetForGrinding(uint32 assistCount)
{
    Group* group = bot->GetGroup();
    Player* master = GetMaster();

    if (master && (master == bot || master->GetMapId() != bot->GetMapId() || master->IsBeingTeleported() ||
                   !GET_PLAYERBOT_AI(master)))
        master = nullptr;

    GuidVector const& attackers = context->GetValue<GuidVector>("attackers")->RefGet();
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        return unit;
    }

    GuidVector const& targets = context->GetValue<GuidVector>("possible targets")->RefGet();
    if (targets.empty())
        return nullptr;

    float distance = 0;
    Unit* result = nullptr;
    uint32 const now = getMSTime();

    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        if (!unit->IsInWorld() || unit->IsDuringRemoveFromWorld())
            continue;

        if (unit->ToCreature() && !unit->ToCreature()->GetCreatureTemplate()->lootid &&
            bot->GetReactionTo(unit) >= REP_NEUTRAL)
            continue;

        if (!bot->IsHostileTo(unit) && unit->GetNpcFlags() != UNIT_NPC_FLAG_NONE)
            continue;

        if (!bot->isHonorOrXPTarget(unit))
            continue;

        if (abs(bot->GetPositionZ() - unit->GetPositionZ()) > INTERACTION_DISTANCE)
            continue;

        if (!bot->InBattleground() && GetTargetingPlayerCount(unit) > assistCount)
            continue;

        // if (!bot->InBattleground() && master && master->GetDistance(unit) >= sPlayerbotAIConfig.grindDistance &&
        // !sRandomPlayerbotMgr.IsRandomBot(bot)) continue;

        // Bots in bot-groups no have a more limited range to look for grind target
        if (!bot->InBattleground() && master && botAI->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) &&
            ServerFacade::instance().GetDistance2d(master, unit) > sPlayerbotAIConfig.lootDistance)
        {
            if (botAI->HasStrategy("debug grind", BotState::BOT_STATE_NON_COMBAT))
                botAI->TellMaster(chat->FormatWorldobject(unit) + " ignored (far from master).");
            continue;
        }

        if (!bot->InBattleground() && (int)unit->GetLevel() - (int)bot->GetLevel() > 4 && !unit->GetGUID().IsPlayer())
            continue;

        if (Creature* creature = unit->ToCreature())
            if (CreatureTemplate const* CreatureTemplate = creature->GetCreatureTemplate())
                if (CreatureTemplate->rank > CREATURE_ELITE_NORMAL && !AI_VALUE(bool, "can fight elite"))
                    continue;

        if (!bot->IsWithinLOSInMap(unit))
        {
            continue;
        }

        bool inactiveGrindStatus = botAI->rpgInfo.GetStatus() != RPG_WANDER_RANDOM && botAI->rpgInfo.GetStatus() != RPG_IDLE;

        float aggroRange = 30.0f;
        if (unit->ToCreature())
            aggroRange = std::min(30.0f, unit->ToCreature()->GetAggroRange(bot) + 10.0f);
        bool outOfAggro = unit->ToCreature() && bot->GetDistance(unit) > aggroRange;
        if (inactiveGrindStatus && outOfAggro)
        {
            uint32 entry = unit->GetEntry();
            auto cit = needForQuestCache.find(entry);
            bool needed;
            if (cit != needForQuestCache.end() && now - cit->second.timestamp < 5000)
                needed = cit->second.result;
            else
            {
                needed = needForQuest(unit);
                needForQuestCache[entry] = {needed, now};
            }
            if (!needed)
                continue;
        }

        if (group)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsAlive())
                    continue;

                float d = member->GetDistance(unit);
                if (!result || d < distance)
                {
                    distance = d;
                    result = unit;
                }
            }
        }
        else
        {
            float newdistance = bot->GetDistance(unit);
            if (!result || (newdistance < distance))
            {
                distance = newdistance;
                result = unit;
            }
        }
    }

    return result;
}

bool GrindTargetValue::needForQuest(Unit* target)
{
    QuestStatusMap& questMap = bot->getQuestStatusMap();
    for (auto& quest : questMap)
    {
        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(quest.first);
        if (!questTemplate)
            continue;

        uint32 questId = questTemplate->GetQuestId();
        if (!questId)
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);

        if (status == QUEST_STATUS_INCOMPLETE)
        {
            const QuestStatusData* questStatus = &bot->getQuestStatusMap()[questId];

            if (questTemplate->GetQuestLevel() > bot->GetLevel() + 5)
                continue;

            for (int j = 0; j < QUEST_OBJECTIVES_COUNT; j++)
            {
                int32 entry = questTemplate->RequiredNpcOrGo[j];

                if (entry && entry > 0)
                {
                    int required = questTemplate->RequiredNpcOrGoCount[j];
                    int available = questStatus->CreatureOrGOCount[j];

                    if (required && available < required && target->GetEntry() == entry)
                        return true;
                }
            }
        }
    }

    if (CreatureTemplate const* data = sObjectMgr->GetCreatureTemplate(target->GetEntry()))
    {
        if (uint32 lootId = data->lootid)
        {
            if (LootTemplates_Creature.HaveQuestLootForPlayer(lootId, bot))
            {
                return true;
            }
        }
    }

    return false;
}

uint32 GrindTargetValue::GetTargetingPlayerCount(Unit* unit)
{
    Group* group = bot->GetGroup();
    if (!group)
        return 0;

    uint32 count = 0;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsAlive() || member == bot)
            continue;

        PlayerbotAI* memberBotAI = GET_PLAYERBOT_AI(member);
        if ((memberBotAI && *memberBotAI->GetAiObjectContext()->GetValue<Unit*>("current target") == unit) ||
            (!memberBotAI && member->GetTarget() == unit->GetGUID()))
            ++count;
    }

    return count;
}
