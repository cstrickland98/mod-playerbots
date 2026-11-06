/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AIOBJECTCONTEXT_H
#define PLAYERBOTS_AIOBJECTCONTEXT_H

#include "Common.h"
#include "DynamicObject.h"
#include "NamedObjectContext.h"
#include "PlayerbotAIAware.h"
#include "Strategy.h"
#include "Trigger.h"
#include "Value.h"
#include <sstream>
#include <string>

class PlayerbotAI;

typedef Strategy* (*StrategyCreator)(PlayerbotAI* botAI);
typedef Action* (*ActionCreator)(PlayerbotAI* botAI);
typedef Trigger* (*TriggerCreator)(PlayerbotAI* botAI);
typedef UntypedValue* (*ValueCreator)(PlayerbotAI* botAI);

class AiObjectContext : public PlayerbotAIAware
{
public:
    static BoolCalculatedValue* custom_glyphs(PlayerbotAI* botAI); // Added for cutom glyphs
    AiObjectContext(PlayerbotAI* botAI,
                    SharedNamedObjectContextList<Strategy>& sharedStrategyContext = sharedStrategyContexts,
                    SharedNamedObjectContextList<Action>& sharedActionContext = sharedActionContexts,
                    SharedNamedObjectContextList<Trigger>& sharedTriggerContext = sharedTriggerContexts,
                    SharedNamedObjectContextList<UntypedValue>& sharedValueContext = sharedValueContexts);
    virtual ~AiObjectContext() {}

    virtual Strategy* GetStrategy(std::string const name);
    virtual std::set<std::string> GetSiblingStrategy(std::string const name);
    virtual Trigger* GetTrigger(std::string const name);
    virtual Action* GetAction(std::string const name);
    virtual UntypedValue* GetUntypedValue(std::string const name);
    uint32 EvictIdleValues(uint32 idleMs);

    template <class T>
    Value<T>* GetValue(std::string const name)
    {
        return dynamic_cast<Value<T>*>(GetUntypedValue(name));
    }

    template <class T>
    Value<T>* GetValue(std::string const name, std::string const param)
    {
        return GetValue<T>((std::string(name) + "::" + param));
    }

    template <class T>
    Value<T>* GetValue(std::string const name, int32 param)
    {
        std::ostringstream out;
        out << param;
        return GetValue<T>(name, out.str());
    }

    std::set<std::string> GetValues();

    // Number of objects this bot has instantiated from each shared context, for the bot stats print
    struct CreatedCounts
    {
        uint32 strategies = 0;
        uint32 actions = 0;
        uint32 triggers = 0;
        uint32 values = 0;
    };
    CreatedCounts GetCreatedCounts() const
    {
        return { uint32(strategyContexts.created.size()), uint32(actionContexts.created.size()),
                 uint32(triggerContexts.created.size()), uint32(valueContexts.created.size()) };
    }

    // Adds this bot's created values to counts, keyed by the value name without its "::qualifier"
    void CountCreatedValueNames(std::unordered_map<std::string, uint32>& counts) const
    {
        for (auto const& entry : valueContexts.created)
        {
            size_t found = entry.first.find("::");
            ++counts[found == std::string::npos ? entry.first : entry.first.substr(0, found)];
        }
    }

    std::set<std::string> GetSupportedStrategies();
    std::set<std::string> GetSupportedActions();
    std::string const FormatValues();

    std::vector<std::string> Save();
    void Load(std::vector<std::string> data);

    std::vector<std::string> performanceStack;

    static void BuildAllSharedContexts();

    static void BuildSharedContexts();
    static void BuildSharedStrategyContexts(SharedNamedObjectContextList<Strategy>& strategyContexts);
    static void BuildSharedActionContexts(SharedNamedObjectContextList<Action>& actionContexts);
    static void BuildSharedTriggerContexts(SharedNamedObjectContextList<Trigger>& triggerContexts);
    static void BuildSharedValueContexts(SharedNamedObjectContextList<UntypedValue>& valueContexts);

protected:
    NamedObjectContextList<Strategy> strategyContexts;
    NamedObjectContextList<Action> actionContexts;
    NamedObjectContextList<Trigger> triggerContexts;
    NamedObjectContextList<UntypedValue> valueContexts;

private:
    static SharedNamedObjectContextList<Strategy> sharedStrategyContexts;
    static SharedNamedObjectContextList<Action> sharedActionContexts;
    static SharedNamedObjectContextList<Trigger> sharedTriggerContexts;
    static SharedNamedObjectContextList<UntypedValue> sharedValueContexts;
};

#endif
