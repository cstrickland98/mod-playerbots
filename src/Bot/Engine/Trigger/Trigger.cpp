/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Trigger.h"

#include "AiObjectContext.h"
#include "Event.h"

Trigger::Trigger(PlayerbotAI* botAI, std::string const name, int32 checkInterval)
    : AiNamedObject(botAI, name),
      checkInterval(checkInterval == 1 ? 1 : (checkInterval < 100 ? checkInterval * 1000 : checkInterval)),
      lastCheckTime(0)
{
}

Event Trigger::Check()
{
    if (IsActive())
    {
        Event event(getName());
        return event;
    }

    Event event;
    return event;
}

Value<Unit*>* Trigger::GetTargetValue() { return context->GetValue<Unit*>(GetTargetName()); }

Unit* Trigger::GetTarget() { return GetTargetValue()->Get(); }

Event EventDrivenTrigger::Check()
{
    uint32 now = getMSTime();
    bool fallback = (now - lastFallbackCheck) >= FALLBACK_MS;
    if (!dirty && !fallback)
        return Event();
    if (fallback)
        lastFallbackCheck = now;
    dirty = false;
    return Trigger::Check();
}

bool Trigger::needCheck(uint32 now)
{
    if (checkInterval < 2)
        return true;

    if (!lastCheckTime || now - lastCheckTime >= checkInterval)
    {
        lastCheckTime = now;
        return true;
    }

    return false;
}
