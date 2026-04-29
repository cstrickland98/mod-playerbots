/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_AOEVALUES_H
#define _PLAYERBOT_AOEVALUES_H

#include "AiObjectContext.h"
#include "GameObject.h"
#include "Object.h"
#include "Value.h"

class PlayerbotAI;

class AoeDensityValue : public CalculatedValue<GuidVector>
{
public:
    AoeDensityValue(PlayerbotAI* botAI) : CalculatedValue<GuidVector>(botAI, "aoe density", 500) {}

    GuidVector Calculate() override;
};

class AoePositionValue : public CalculatedValue<WorldLocation>
{
public:
    AoePositionValue(PlayerbotAI* botAI) : CalculatedValue<WorldLocation>(botAI, "aoe position", 500) {}

    WorldLocation Calculate() override;
};

class AoeCountValue : public CalculatedValue<uint8>
{
public:
    AoeCountValue(PlayerbotAI* botAI) : CalculatedValue<uint8>(botAI, "aoe count", 500) {}

    uint8 Calculate() override;
};

class HasAreaDebuffValue : public BoolCalculatedValue, public Qualified
{
public:
    HasAreaDebuffValue(PlayerbotAI* botAI) : BoolCalculatedValue(botAI, "value", 500) {}

    Unit* GetTarget()
    {
        AiObjectContext* ctx = AiObject::context;

        return ctx->GetValue<Unit*>(qualifier)->Get();
    }
    virtual bool Calculate();
};

class AreaDebuffValue : public CalculatedValue<Aura*>
{
public:
    AreaDebuffValue(PlayerbotAI* botAI) : CalculatedValue<Aura*>(botAI, "area debuff", 500) {}

    Aura* Calculate() override;
};

#endif
