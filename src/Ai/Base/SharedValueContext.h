/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SHAREDVALUECONTEXT_H
#define PLAYERBOTS_SHAREDVALUECONTEXT_H

#include "LootValues.h"
#include "NamedObjectContext.h"
#include "PlayerbotAI.h"
#include "PvpValues.h"
#include "QuestValues.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class SharedValueContext : public NamedObjectContext<UntypedValue>
{
public:
    static SharedValueContext& instance()
    {
        static SharedValueContext instance;

        return instance;
    }

    // Values shared by all bots: created once per name and read under one lock, since bots on any map thread
    // ask for them and calculating one may ask for another
    template <class T>
    T GetGlobal(std::string const& name)
    {
        std::lock_guard<std::recursive_mutex> lock(globalsLock);
        auto it = globals.find(name);
        if (it == globals.end())
        {
            UntypedValue* created = NamedObjectFactory<UntypedValue>::CreateFrom(creators, name, globalAI);
            it = globals.emplace(name, std::unique_ptr<UntypedValue>(created)).first;
        }

        Value<T>* value = dynamic_cast<Value<T>*>(it->second.get());
        return value ? value->Get() : T();
    }

    template <class T>
    T GetGlobal(std::string const& name, std::string const& param)
    {
        return GetGlobal<T>(name + "::" + param);
    }

    template <class T>
    T GetGlobal(std::string const& name, uint32 param)
    {
        return GetGlobal<T>(name, std::to_string(param));
    }

private:
    SharedValueContext() : NamedObjectContext(true)
    {
        creators["bg masters"] = &SharedValueContext::bg_masters;
        creators["drop map"] = &SharedValueContext::drop_map;
        creators["item drop list"] = &SharedValueContext::item_drop_list;
        creators["entry loot list"] = &SharedValueContext::entry_loot_list;

        creators["entry quest relation"] = &SharedValueContext::entry_quest_relation;
        creators["quest guidp map"] = &SharedValueContext::quest_guidp_map;
    }
    ~SharedValueContext() = default;

    SharedValueContext(SharedValueContext const&) = delete;
    SharedValueContext& operator=(SharedValueContext const&) = delete;

    SharedValueContext(SharedValueContext&&) = delete;
    SharedValueContext& operator=(SharedValueContext&&) = delete;

    static UntypedValue* bg_masters(PlayerbotAI* botAI) { return new BgMastersValue(botAI); }
    static UntypedValue* drop_map(PlayerbotAI* botAI) { return new DropMapValue(botAI); }
    static UntypedValue* item_drop_list(PlayerbotAI* botAI) { return new ItemDropListValue(botAI); }
    static UntypedValue* entry_loot_list(PlayerbotAI* botAI) { return new EntryLootListValue(botAI); }

    static UntypedValue* entry_quest_relation(PlayerbotAI* botAI) { return new EntryQuestRelationMapValue(botAI); }
    static UntypedValue* quest_guidp_map(PlayerbotAI* botAI) { return new QuestGuidpMapValue(botAI); }

    PlayerbotAI* const globalAI = new PlayerbotAI();  // botless owner for the shared values; lives with the process
    std::recursive_mutex globalsLock;
    std::unordered_map<std::string, std::unique_ptr<UntypedValue>> globals;
};

#define sSharedValueContext SharedValueContext::instance()

#endif
