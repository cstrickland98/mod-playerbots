/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptBattlegroundInvitationAction.h"
#include "Event.h"
#include "PlayerbotAI.h"

bool AcceptBgInvitationAction::Execute(Event /*event*/)
{
    uint8 type = 0;                      // arenatype if arena
    uint8 unk2 = 0;                      // unk, can be 0x0 (may be if was invited?) and 0x1
    uint32 bgTypeId_ = BATTLEGROUND_WS;  // type id from dbc
    uint16 unk = 0x1F90;                 // 0x1F90 constant?*/
    uint8 action = 1;

    // queued, not handled here: the port handler writes the battleground queue, which the world thread owns, and this
    // runs on a map thread (the bot session handles its queued packets on the world thread)
    WorldPacket packet(CMSG_BATTLEFIELD_PORT, 20);
    packet << type << unk2 << (uint32)bgTypeId_ << unk << action;
    bot->GetSession()->QueuePacket(new WorldPacket(packet));

    botAI->ResetStrategies();

    return true;
}
