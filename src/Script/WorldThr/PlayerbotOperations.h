/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_PLAYERBOTOPERATIONS_H
#define PLAYERBOTS_PLAYERBOTOPERATIONS_H

#include "ArenaTeamMgr.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "PlayerbotOperation.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"
#include "RandomPlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "UseMeetingStoneAction.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"

// Group invite operation
class GroupInviteOperation : public PlayerbotOperation
{
public:
    GroupInviteOperation(ObjectGuid botGuid, ObjectGuid targetGuid)
        : m_botGuid(botGuid), m_targetGuid(targetGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);

        if (!bot || !target)
        {
            LOG_DEBUG("playerbots", "GroupInviteOperation: Bot or target not found");
            return false;
        }

        // Check if target is already in a group
        if (target->GetGroup())
        {
            LOG_DEBUG("playerbots", "GroupInviteOperation: Target {} is already in a group", target->GetName());
            return false;
        }

        Group* group = bot->GetGroup();

        // Create group if bot doesn't have one
        if (!group)
        {
            group = new Group;
            if (!group->Create(bot))
            {
                delete group;
                LOG_ERROR("playerbots", "GroupInviteOperation: Failed to create group for bot {}", bot->GetName());
                return false;
            }
            sGroupMgr->AddGroup(group);
            LOG_DEBUG("playerbots", "GroupInviteOperation: Created new group for bot {}", bot->GetName());
        }

        // Convert to raid if needed (more than 5 members)
        if (!group->isRaidGroup() && group->GetMembersCount() >= 5)
        {
            group->ConvertToRaid();
            LOG_DEBUG("playerbots", "GroupInviteOperation: Converted group to raid");
        }

        // Add member to group
        if (group->AddMember(target))
        {
            LOG_DEBUG("playerbots", "GroupInviteOperation: Successfully added {} to group", target->GetName());
            if (sPlayerbotAIConfig.SummonWhenGroup && target->GetDistance(bot) > sPlayerbotAIConfig.SightDistance)
            {
                PlayerbotAI* targetAI = sPlayerbotsMgr.GetPlayerbotAI(target);
                if (targetAI)
                {
                    SummonAction summonAction(targetAI, "group summon");
                    summonAction.Teleport(bot, target, true);
                }
            }
            return true;
        }
        else
        {
            LOG_ERROR("playerbots", "GroupInviteOperation: Failed to add {} to group", target->GetName());
            return false;
        }
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }  // High priority (player-facing)

    std::string GetName() const override { return "GroupInvite"; }

    bool IsValid() const override
    {
        // Check if bot still exists and is online
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);
        return bot && target;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_targetGuid;
};

// Group invite through the inviter's session handler, for an inviter or invitee another map thread may be updating
class GroupInviteOpcodeOperation : public PlayerbotOperation
{
public:
    GroupInviteOpcodeOperation(ObjectGuid inviterGuid, ObjectGuid targetGuid)
        : m_inviterGuid(inviterGuid), m_targetGuid(targetGuid)
    {
    }

    bool Execute() override
    {
        Player* inviter = ObjectAccessor::FindPlayer(m_inviterGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);
        if (!inviter || !target || !inviter->GetSession())
            return false;

        WorldPacket p;
        uint32 rolesMask = 0;
        p << target->GetName();
        p << rolesMask;
        inviter->GetSession()->HandleGroupInviteOpcode(p);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_inviterGuid; }
    uint32 GetPriority() const override { return 50; }
    std::string GetName() const override { return "GroupInviteOpcode"; }

    bool IsValid() const override
    {
        return ObjectAccessor::FindPlayer(m_inviterGuid) && ObjectAccessor::FindPlayer(m_targetGuid);
    }

private:
    ObjectGuid m_inviterGuid;
    ObjectGuid m_targetGuid;
};

// Remove member from group
class GroupRemoveMemberOperation : public PlayerbotOperation
{
public:
    GroupRemoveMemberOperation(ObjectGuid botGuid, ObjectGuid targetGuid)
        : m_botGuid(botGuid), m_targetGuid(targetGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* target = ObjectAccessor::FindPlayer(m_targetGuid);

        if (!bot || !target)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Bot is not in a group");
            return false;
        }

        if (!group->IsMember(target->GetGUID()))
        {
            LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Target is not in bot's group");
            return false;
        }

        group->RemoveMember(target->GetGUID());
        LOG_DEBUG("playerbots", "GroupRemoveMemberOperation: Removed {} from group", target->GetName());
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupRemoveMember"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_targetGuid;
};

// Convert group to raid
class GroupConvertToRaidOperation : public PlayerbotOperation
{
public:
    GroupConvertToRaidOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Bot is not in a group");
            return false;
        }

        if (group->isRaidGroup())
        {
            LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Group is already a raid");
            return true;  // Success - already in desired state
        }

        group->ConvertToRaid();
        LOG_DEBUG("playerbots", "GroupConvertToRaidOperation: Converted group to raid");
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupConvertToRaid"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
};

// Set group leader
class GroupSetLeaderOperation : public PlayerbotOperation
{
public:
    GroupSetLeaderOperation(ObjectGuid botGuid, ObjectGuid newLeaderGuid)
        : m_botGuid(botGuid), m_newLeaderGuid(newLeaderGuid)
    {
    }

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* newLeader = ObjectAccessor::FindPlayer(m_newLeaderGuid);

        if (!bot || !newLeader)
            return false;

        Group* group = bot->GetGroup();
        if (!group)
        {
            LOG_DEBUG("playerbots", "GroupSetLeaderOperation: Bot is not in a group");
            return false;
        }

        if (!group->IsMember(newLeader->GetGUID()))
        {
            LOG_DEBUG("playerbots", "GroupSetLeaderOperation: New leader is not in the group");
            return false;
        }

        group->ChangeLeader(newLeader->GetGUID());
        group->SendUpdate();
        LOG_DEBUG("playerbots", "GroupSetLeaderOperation: Changed leader to {}", newLeader->GetName());
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }

    std::string GetName() const override { return "GroupSetLeader"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* newLeader = ObjectAccessor::FindPlayer(m_newLeaderGuid);
        return bot && newLeader;
    }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_newLeaderGuid;
};

// Form a rated arena group around the team captain and queue it; group create/disband runs only here
class ArenaGroupFormationOperation : public PlayerbotOperation
{
public:
    ArenaGroupFormationOperation(ObjectGuid leaderGuid, std::vector<ObjectGuid> memberGuids, uint32 arenaTeamId,
                                 ObjectGuid battleMasterGuid)
        : m_leaderGuid(leaderGuid), m_memberGuids(std::move(memberGuids)), m_arenaTeamId(arenaTeamId),
          m_battleMasterGuid(battleMasterGuid)
    {
    }

    bool Execute() override
    {
        Player* leader = ObjectAccessor::FindPlayer(m_leaderGuid);
        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(m_arenaTeamId);
        if (!leader || !team || team->GetCaptain() != m_leaderGuid)
            return false;

        if (leader->InBattleground() || leader->InBattlegroundQueue() || leader->IsInCombat())
            return false;

        uint32 const teamSize = team->GetType();

        // state may have changed since the map thread picked these members
        std::vector<ObjectGuid> readyMembers;
        for (ObjectGuid const& memberGuid : m_memberGuids)
        {
            Player* member = ObjectAccessor::FindPlayer(memberGuid);
            if (!member || memberGuid == m_leaderGuid || !team->IsMember(memberGuid))
                continue;

            if (member->InBattleground() || member->InBattlegroundQueue() || member->IsInCombat())
                continue;

            PlayerbotAI* memberBotAI = PlayerbotsMgr::instance().GetPlayerbotAI(member);
            if (!memberBotAI)
                continue;

            if (Group* memberGroup = member->GetGroup())
                memberGroup->RemoveMember(memberGuid);

            memberBotAI->Reset();
            readyMembers.push_back(memberGuid);
        }

        if (readyMembers.size() + 1 < teamSize)
        {
            LOG_INFO("playerbots", "Team #{} <{}> has not enough members for match", team->GetId(), team->GetName());
            return false;
        }

        if (Group* leaderGroup = leader->GetGroup())
            leaderGroup->Disband(true);

        Group* group = new Group();
        if (!group->Create(leader))
        {
            delete group;
            LOG_INFO("playerbots", "Team #{} <{}>: Can't create group for arena queue", team->GetId(),
                     team->GetName());
            return false;
        }

        sGroupMgr->AddGroup(group);
        LOG_INFO("playerbots", "Bot {} <{}>: Leader of <{}>", leader->GetGUID().ToString(), leader->GetName(),
                 team->GetName());

        for (ObjectGuid const& memberGuid : readyMembers)
        {
            if (group->GetMembersCount() >= teamSize)
                break;

            Player* member = ObjectAccessor::FindPlayer(memberGuid);
            if (!member || !group->AddMember(member))
                continue;

            member->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
            member->TeleportTo(leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(),
                               leader->GetPositionZ(), 0);

            LOG_INFO("playerbots", "Bot {} <{}>: Member of <{}>", member->GetGUID().ToString(), member->GetName(),
                     team->GetName());
        }

        if (group->GetMembersCount() < teamSize)
        {
            LOG_INFO("playerbots", "Team #{} <{}> Group is not ready for match (not enough members)", team->GetId(),
                     team->GetName());
            group->Disband();
            return false;
        }

        LOG_INFO("playerbots", "Team #{} <{}> Group is ready for match", team->GetId(), team->GetName());

        WorldPacket* packet = new WorldPacket(CMSG_BATTLEMASTER_JOIN_ARENA, 20);
        *packet << m_battleMasterGuid << ArenaTeam::GetSlotByType(teamSize) << uint8(1) << uint8(1);
        leader->GetSession()->QueuePacket(packet);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_leaderGuid; }

    uint32 GetPriority() const override { return 60; }  // Very high priority (arena/BG operations)

    std::string GetName() const override { return "ArenaGroupFormation"; }

    bool IsValid() const override { return ObjectAccessor::FindPlayer(m_leaderGuid) != nullptr; }

private:
    ObjectGuid m_leaderGuid;
    std::vector<ObjectGuid> m_memberGuids;
    uint32 m_arenaTeamId;
    ObjectGuid m_battleMasterGuid;
};

// Bot logout group cleanup operation
class BotLogoutGroupCleanupOperation : public PlayerbotOperation
{
public:
    BotLogoutGroupCleanupOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        PlayerbotAI* botAI = PlayerbotsMgr::instance().GetPlayerbotAI(bot);
        if (!botAI)
            return false;

        Group* group = bot->GetGroup();
        if (group && !bot->InBattleground() && !bot->InBattlegroundQueue() && IsRealPlayer(botAI->GetMaster()))
            PlayerbotRepository::instance().Save(botAI);

        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 70; }
    std::string GetName() const override { return "BotLogoutGroupCleanup"; }

    bool IsValid() const override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        return bot != nullptr;
    }

private:
    ObjectGuid m_botGuid;
};

// Logout requested from the bot's own update while its map runs a parallel phase: removing a player from
// its map is only allowed between phases
class BotLogoutOperation : public PlayerbotOperation
{
public:
    BotLogoutOperation(ObjectGuid botGuid, ObjectGuid masterGuid) : m_botGuid(botGuid), m_masterGuid(masterGuid) {}

    bool Execute() override
    {
        if (!ObjectAccessor::FindConnectedPlayer(m_botGuid))
            return false;

        PlayerbotHolder* holder = nullptr;
        if (Player* master = ObjectAccessor::FindConnectedPlayer(m_masterGuid))
            holder = PlayerbotsMgr::instance().GetPlayerbotMgr(master);

        if (!holder)
            holder = &RandomPlayerbotMgr::instance();

        holder->LogoutPlayerBot(m_botGuid);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 70; }
    std::string GetName() const override { return "BotLogout"; }
    bool IsValid() const override { return ObjectAccessor::FindConnectedPlayer(m_botGuid) != nullptr; }

private:
    ObjectGuid m_botGuid;
    ObjectGuid m_masterGuid;
};

// Logs out or cancels the logout of a master's bots, which may be anywhere, when the master's thread asks
class MasterBotsLogoutOperation : public PlayerbotOperation
{
public:
    MasterBotsLogoutOperation(ObjectGuid masterGuid, bool cancel) : m_masterGuid(masterGuid), m_cancel(cancel) {}

    bool Execute() override
    {
        Player* master = ObjectAccessor::FindConnectedPlayer(m_masterGuid);
        PlayerbotMgr* mgr = master ? PlayerbotsMgr::instance().GetPlayerbotMgr(master) : nullptr;
        if (!mgr)
            return false;

        if (m_cancel)
            mgr->CancelLogout();
        else
            mgr->LogoutAllBots();

        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_masterGuid; }
    uint32 GetPriority() const override { return 70; }
    std::string GetName() const override { return "MasterBotsLogout"; }
    bool IsValid() const override { return ObjectAccessor::FindConnectedPlayer(m_masterGuid) != nullptr; }

private:
    ObjectGuid m_masterGuid;
    bool m_cancel;
};

// Add player bot operation (for logging in bots from map threads)
class AddPlayerBotOperation : public PlayerbotOperation
{
public:
    AddPlayerBotOperation(ObjectGuid botGuid, uint32 masterAccountId)
        : m_botGuid(botGuid), m_masterAccountId(masterAccountId)
    {
    }

    bool Execute() override
    {
        sRandomPlayerbotMgr.AddPlayerBot(m_botGuid, m_masterAccountId);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    uint32 GetPriority() const override { return 50; }  // High priority

    std::string GetName() const override { return "AddPlayerBot"; }

    bool IsValid() const override
    {
        return !ObjectAccessor::FindConnectedPlayer(m_botGuid);
    }

private:
    ObjectGuid m_botGuid;
    uint32 m_masterAccountId;
};

class OnBotLoginOperation : public PlayerbotOperation
{
public:
    OnBotLoginOperation(ObjectGuid botGuid, uint32 masterAccountId)
        : m_botGuid(botGuid), m_masterAccountId(masterAccountId)
    {
    }

    bool Execute() override
    {
        // find and verify bot still exists
        Player* bot = ObjectAccessor::FindConnectedPlayer(m_botGuid);
        if (!bot)
            return false;

        PlayerbotHolder* holder = &RandomPlayerbotMgr::instance();
        if (m_masterAccountId)
        {
            WorldSession* masterSession = sWorldSessionMgr->FindSession(m_masterAccountId);
            Player* masterPlayer = masterSession ? masterSession->GetPlayer() : nullptr;
            if (masterPlayer)
                holder = PlayerbotsMgr::instance().GetPlayerbotMgr(masterPlayer);
        }

        if (!holder)
            return false;

        holder->OnBotLogin(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 100; }
    std::string GetName() const override { return "OnBotLogin"; }

    bool IsValid() const override { return ObjectAccessor::FindConnectedPlayer(m_botGuid) != nullptr; }

private:
    ObjectGuid m_botGuid;
    uint32 m_masterAccountId = 0;
};

class ArenaTeamAssignOperation : public PlayerbotOperation
{
public:
    explicit ArenaTeamAssignOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        if (!bot)
            return false;

        RandomPlayerbotFactory::AssignBotToArenaTeamInternal(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    std::string GetName() const override { return "ArenaTeamAssign"; }

    bool IsValid() const override { return ObjectAccessor::FindPlayer(m_botGuid) != nullptr; }

private:
    ObjectGuid m_botGuid;
};

// Drop a bot that has left its captain's arena bracket: the team can't queue with it
class ArenaTeamDropMemberOperation : public PlayerbotOperation
{
public:
    ArenaTeamDropMemberOperation(uint32 arenaTeamId, ObjectGuid botGuid)
        : m_arenaTeamId(arenaTeamId), m_botGuid(botGuid)
    {
    }

    bool Execute() override
    {
        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(m_arenaTeamId);
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        Player* captain = team ? ObjectAccessor::FindPlayer(team->GetCaptain()) : nullptr;
        if (!bot || !captain || captain == bot || !team->IsMember(m_botGuid))
            return false;

        // levels can change before this runs
        if (RandomPlayerbotFactory::SameArenaBracket(bot->GetLevel(), captain->GetLevel()))
            return false;

        team->DelMember(m_botGuid, true);
        RandomPlayerbotFactory::AssignBotToArenaTeam(bot);
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }

    std::string GetName() const override { return "ArenaTeamDropMember"; }

    bool IsValid() const override { return ObjectAccessor::FindPlayer(m_botGuid) != nullptr; }

private:
    uint32 m_arenaTeamId;
    ObjectGuid m_botGuid;
};

#endif
