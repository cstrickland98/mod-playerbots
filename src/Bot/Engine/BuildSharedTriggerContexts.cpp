#include "AiObjectContext.h"
#include "TriggerContext.h"
#include "ChatTriggerContext.h"
#include "WorldPacketTriggerContext.h"

void AiObjectContext::BuildSharedTriggerContexts(SharedNamedObjectContextList<Trigger>& triggerContexts)
{
    triggerContexts.Add(new TriggerContext());
    triggerContexts.Add(new ChatTriggerContext());
    triggerContexts.Add(new WorldPacketTriggerContext());
}
