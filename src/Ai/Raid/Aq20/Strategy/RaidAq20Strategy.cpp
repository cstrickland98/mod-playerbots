#include "RaidAq20Strategy.h"
#include "../RaidAq20TriggerContext.h"

#include "Strategy.h"

void RaidAq20Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode("aq20 move to crystal",
            { NextAction("aq20 use crystal", ACTION_RAID) }));

}

NamedObjectContext<Trigger>* RaidAq20Strategy::GetTriggerContext()
{
    static RaidAq20TriggerContext ctx;
    return &ctx;
}
