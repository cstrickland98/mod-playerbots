/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PassiveMultiplier.h"
#include "Action.h"

std::vector<std::string> const PassiveMultiplier::allowedActions = {"co", "nc", "reset botAI", "check mount state",
                                                                    "lfg"};
std::vector<std::string> const PassiveMultiplier::allowedParts = {"follow", "move from group", "stay", "chat shortcut"};

PassiveMultiplier::PassiveMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "passive") {}

float PassiveMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    std::string const name = action->getName();

    for (std::vector<std::string>::const_iterator i = allowedActions.begin(); i != allowedActions.end(); i++)
    {
        if (name == *i)
            return 1.0f;
    }

    for (std::vector<std::string>::const_iterator i = allowedParts.begin(); i != allowedParts.end(); i++)
    {
        if (name.find(*i) != std::string::npos)
            return 1.0f;
    }

    return 0;
}
