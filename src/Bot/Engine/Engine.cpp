/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Engine.h"
#include "Action.h"
#include "Event.h"
#include "PerfMonitor.h"
#include "Playerbots.h"
#include "Queue.h"
#include "Strategy.h"
#include "Timer.h"
#include <mutex>
#include <typeindex>

#include <algorithm>

Engine::Engine(PlayerbotAI* botAI, AiObjectContext* factory) : PlayerbotAIAware(botAI), aiObjectContext(factory)
{
    lastRelevance = 0.0f;
    testMode = false;
}

bool ActionExecutionListeners::Before(Action* action, Event event)
{
    bool result = true;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i != listeners.end(); i++)
    {
        result &= (*i)->Before(action, event);
    }

    return result;
}

void ActionExecutionListeners::After(Action* action, bool executed, Event event)
{
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i != listeners.end(); i++)
    {
        (*i)->After(action, executed, event);
    }
}

bool ActionExecutionListeners::OverrideResult(Action* action, bool executed, Event event)
{
    bool result = executed;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i != listeners.end(); i++)
    {
        result = (*i)->OverrideResult(action, result, event);
    }

    return result;
}

bool ActionExecutionListeners::AllowExecution(Action* action, Event event)
{
    bool result = true;
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i != listeners.end(); i++)
    {
        result &= (*i)->AllowExecution(action, event);
    }

    return result;
}

ActionExecutionListeners::~ActionExecutionListeners()
{
    for (std::list<ActionExecutionListener*>::iterator i = listeners.begin(); i != listeners.end(); i++)
    {
        delete *i;
    }

    listeners.clear();
}

Engine::~Engine(void)
{
    Reset();

    // for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    // {
    //     Strategy* strategy = i->second;
    //     if (strategy)
    //     {
    //         delete strategy;
    //     }
    // }

    strategies.clear();
}

void Engine::Reset()
{
    strategyTypeMask = 0;

    ActionNode* action = nullptr;

    while ((action = queue.Pop()) != nullptr)
    {
        delete action;
    }

    for (TriggerNode* node : ownedTriggerNodes)
    {
        delete node;
    }

    ownedTriggerNodes.clear();
    triggers.clear();
    nodeTriggers.clear();
    checkNodes.clear();
    externalNodes.clear();
    nodesPartitioned = false;

    for (Multiplier* multiplier : multipliers)
    {
        delete multiplier;
    }

    multipliers.clear();

    actionNodeCreators.reset();
}

std::shared_ptr<Engine::ActionNodeCreators const> Engine::SharedActionNodeCreators(
    std::map<std::string, Strategy*> const& strategies)
{
    // Factories are process-wide singletons, so the sequence of factory addresses identifies the union exactly
    std::string key;
    for (auto const& pair : strategies)
        for (NamedObjectFactory<ActionNode>* factory : pair.second->actionNodeFactories.factories)
            key.append(reinterpret_cast<char const*>(&factory), sizeof(factory));

    static std::mutex cacheLock;
    static std::unordered_map<std::string, std::shared_ptr<ActionNodeCreators const>> cache;

    std::lock_guard<std::mutex> lock(cacheLock);
    auto cached = cache.find(key);
    if (cached != cache.end())
        return cached->second;

    auto creators = std::make_shared<ActionNodeCreators>();
    for (auto const& pair : strategies)
        for (NamedObjectFactory<ActionNode>* factory : pair.second->actionNodeFactories.factories)
            for (auto const& creator : factory->creators)
                (*creators)[creator.first] = creator.second;

    return cache[key] = creators;
}

std::vector<TriggerNode*> const& Engine::SharedTriggerNodes(Strategy* strategy, bool reaction)
{
    static std::mutex cacheLock;
    static std::map<std::pair<std::type_index, bool>, std::vector<TriggerNode*>> cache;

    std::lock_guard<std::mutex> lock(cacheLock);
    auto key = std::make_pair(std::type_index(typeid(*strategy)), reaction);
    auto cached = cache.find(key);
    if (cached != cache.end())
        return cached->second;

    std::vector<TriggerNode*>& nodes = cache[key];
    if (reaction)
        strategy->InitReactionTriggers(nodes);
    else
        strategy->InitTriggers(nodes);

    return nodes;
}

void Engine::AddTriggerNodes(Strategy* strategy, bool reaction)
{
    if (strategy->HasSharedTriggers())
    {
        std::vector<TriggerNode*> const& shared = SharedTriggerNodes(strategy, reaction);
        triggers.insert(triggers.end(), shared.begin(), shared.end());
        return;
    }

    size_t first = triggers.size();
    if (reaction)
        strategy->InitReactionTriggers(triggers);
    else
        strategy->InitTriggers(triggers);

    ownedTriggerNodes.insert(ownedTriggerNodes.end(), triggers.begin() + first, triggers.end());
}

void Engine::Init()
{
    Reset();

    hasTargetExclusions = false;
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        strategyTypeMask |= strategy->GetType();
        hasTargetExclusions |= strategy->HasTargetExclusions();
        strategy->InitMultipliers(multipliers);
        AddTriggerNodes(strategy, false);
    }

    nodeTriggers.assign(triggers.size(), nullptr);
    actionNodeCreators = SharedActionNodeCreators(strategies);

    if (testMode)
    {
        FILE* file = fopen("test.log", "w");
        fprintf(file, "\n");
        fclose(file);
    }
}

bool Engine::DoNextAction(Unit* /*unit*/, uint32 /*depth*/, bool minimal)
{
    LogAction("--- AI Tick ---");

    if (sPlayerbotAIConfig.LogValuesPerTick)
        LogValues();

    bool actionExecuted = false;
    ActionBasket* basket = nullptr;
    time_t currentTime = time(nullptr);

    if (!minimal)
        botAI->forceRebuff.RollBuffPendingCycle();

    // Update triggers and push default actions
    ProcessTriggers(minimal);
    PushDefaultActions();

    uint32 iterations = 0;
    uint32 iterationsPerTick = queue.Size() * (minimal ? 2 : sPlayerbotAIConfig.IterationsPerTick);

    while (++iterations <= iterationsPerTick)
    {
        basket = queue.Peek();
        if (!basket)
            break;

        float relevance = basket->getRelevance();  // for reference
        bool skipPrerequisites = basket->isSkipPrerequisites();

        if (minimal && (relevance < 100))
            continue;

        Event event = basket->getEvent();
        ActionNode* actionNode = queue.Pop();  // NOTE: Pop() deletes basket
        Action* action = InitializeAction(actionNode);

        if (!action)
        {
            LogAction("A:%s - UNKNOWN", actionNode->getName().c_str());
        }
        else if (action->isUseful())
        {
            // Apply multipliers early to avoid unnecessary iterations
            for (Multiplier* multiplier : multipliers)
            {
                float value;
                {
                    PerfMonitorScope scope(multiplier->GetPerfData());
                    value = multiplier->GetValue(action);
                }

                relevance *= value;
                action->setRelevance(relevance);

                if (relevance <= 0)
                {
                    if (value <= 0)
                        multiplier->NoteBlock();

                    LogAction("Multiplier %s made action %s useless", multiplier->getName().c_str(), action->getName().c_str());
                    break;
                }
            }

            if (action->isPossible() && relevance > 0)
            {
                if (!skipPrerequisites)
                {
                    LogAction("A:%s - PREREQ", action->getName().c_str());

                    if (MultiplyAndPush(actionNode->getPrerequisites(), relevance + 0.002f, false, event, "prereq"))
                    {
                        PushAgain(actionNode, relevance + 0.001f, event);
                        continue;
                    }
                }

                PerfMonitorOperation* pmo = sPerfMonitor.start(PERF_MON_ACTION, action->getName(), &aiObjectContext->performanceStack);
                actionExecuted = ListenAndExecute(action, event);
                if (pmo)
                    pmo->finish();

                if (actionExecuted)
                {
                    LogAction("A:%s - OK", action->getName().c_str());
                    MultiplyAndPush(actionNode->getContinuers(), relevance, false, event, "cont");
                    lastRelevance = relevance;
                    delete actionNode;  // Safe memory management
                    break;
                }
                else
                {
                    LogAction("A:%s - FAILED", action->getName().c_str());
                    MultiplyAndPush(actionNode->getAlternatives(), relevance + 0.003f, false, event, "alt");
                }
            }
            else
            {
                LogAction("A:%s - IMPOSSIBLE", action->getName().c_str());
                MultiplyAndPush(actionNode->getAlternatives(), relevance + 0.003f, false, event, "alt");
            }
        }
        else
        {
            LogAction("A:%s - USELESS", action->getName().c_str());
            lastRelevance = relevance;
        }

        delete actionNode;  // Always delete after processing the action node
    }

    if (time(nullptr) - currentTime > 1)
    {
        LogAction("Execution time exceeded 1 second");
    }

    if (!actionExecuted)
        LogAction("no actions executed");

    queue.RemoveExpired();

    return actionExecuted;
}

ActionNode* Engine::CreateActionNode(std::string const name)
{
    if (actionNodeCreators)
        if (ActionNode* node = NamedObjectFactory<ActionNode>::CreateFrom(*actionNodeCreators, name, botAI))
            return node;

    return new ActionNode(name,
                          /*P*/ {},
                          /*A*/ {},
                          /*C*/ {});
}

bool Engine::MultiplyAndPush(
    std::vector<NextAction> actions,
    float forceRelevance,
    bool skipPrerequisites,
    Event event,
    char const* pushType
)
{
    bool pushed = false;

    for (NextAction nextAction : actions)
    {
        ActionNode* action = this->CreateActionNode(nextAction.getName());

        this->InitializeAction(action);

        float k = nextAction.getRelevance();

        if (forceRelevance > 0.0f)
        {
            k = forceRelevance;
        }

        if (k > 0)
        {
            this->LogAction("PUSH:%s - %f (%s)", action->getName().c_str(), k, pushType);
            queue.Push(new ActionBasket(action, k, skipPrerequisites, event));
            pushed = true;

            continue;
        }

        delete action;

    }

    return pushed;
}

ActionResult Engine::ExecuteAction(std::string const name, Event event, std::string const qualifier)
{
    bool result = false;

    ActionNode* actionNode = CreateActionNode(name);
    if (!actionNode)
        return ACTION_RESULT_UNKNOWN;

    Action* action = InitializeAction(actionNode);
    if (!action)
    {
        delete actionNode;
        return ACTION_RESULT_UNKNOWN;
    }

    if (!qualifier.empty())
    {
        if (Qualified* q = dynamic_cast<Qualified*>(action))
            q->Qualify(qualifier);
    }

    if (!action->isUseful())
    {
        delete actionNode;
        return ACTION_RESULT_USELESS;
    }

    if (!action->isPossible())
    {
        delete actionNode;
        return ACTION_RESULT_IMPOSSIBLE;
    }

    action->MakeVerbose();

    result = ListenAndExecute(action, event);
    MultiplyAndPush(action->getContinuers(), 0.0f, false, event, "default");

    delete actionNode;

    return result ? ACTION_RESULT_OK : ACTION_RESULT_FAILED;
}

void Engine::addStrategy(std::string const name, bool init)
{
    removeStrategy(name, init);

    if (Strategy* strategy = aiObjectContext->GetStrategy(name))
    {
        std::set<std::string> siblings = aiObjectContext->GetSiblingStrategy(name);
        for (std::set<std::string>::iterator i = siblings.begin(); i != siblings.end(); i++)
            removeStrategy(*i, init);

        LogAction("S:+%s", strategy->getName().c_str());
        strategies[strategy->getName()] = strategy;
    }
    if (init)
        Init();
}

void Engine::addStrategies(std::string first, ...)
{
    addStrategy(first, false);

    va_list vl;
    va_start(vl, first);

    char const* cur;
    do
    {
        cur = va_arg(vl, char const*);
        if (cur)
            addStrategy(cur, false);
    } while (cur);

    Init();

    va_end(vl);
}

void Engine::addStrategiesNoInit(std::string first, ...)
{
    addStrategy(first, false);

    va_list vl;
    va_start(vl, first);

    char const* cur;
    do
    {
        cur = va_arg(vl, char const*);
        if (cur)
            addStrategy(cur, false);
    } while (cur);

    va_end(vl);
}

bool Engine::removeStrategy(std::string const name, bool init)
{
    std::map<std::string, Strategy*>::iterator i = strategies.find(name);
    if (i == strategies.end())
        return false;

    LogAction("S:-%s", name.c_str());
    strategies.erase(i);
    if (init)
        Init();

    return true;
}

void Engine::removeAllStrategies()
{
    strategies.clear();
    Init();
}

void Engine::toggleStrategy(std::string const name)
{
    if (!removeStrategy(name))
        addStrategy(name);
}

bool Engine::HasStrategy(std::string const name) { return strategies.find(name) != strategies.end(); }

Strategy* Engine::GetStrategy(std::string const& name)
{
    std::map<std::string, Strategy*>::iterator i = strategies.find(name);
    return i != strategies.end() ? i->second : nullptr;
}

// Resolves the trigger of every node once (a name the context does not know stays null) and splits
// the nodes into the ones checked every pass and the ones that fire only after an external event.
// The chat and packet strategies contribute about 190 of the 250 nodes of a combat engine; after
// this they cost nothing while no command or packet is pending
void Engine::PartitionTriggerNodes()
{
    checkNodes.clear();
    externalNodes.clear();
    for (size_t i = 0; i < triggers.size(); ++i)
    {
        TriggerNode* node = triggers[i];
        if (!node)
            continue;

        Trigger* trigger = aiObjectContext->GetTrigger(node->getName());
        nodeTriggers[i] = trigger;
        if (!trigger)
            continue;

        (trigger->IsExternalOnly() ? externalNodes : checkNodes).push_back(i);
    }

    nodesPartitioned = true;
}

void Engine::CheckTrigger(size_t i, uint32 now, bool minimal, std::vector<std::pair<Trigger*, Event>>& fires)
{
    Trigger* trigger = nodeTriggers[i];

    // Several strategies can list the same trigger; its answer does not change within one pass
    if (trigger->IsCheckedThisPass())
        return;

    if (!testMode && !trigger->needCheck(now))
        return;

    if (minimal && triggers[i]->getFirstRelevance() < 100)
        return;

    PerfMonitorOperation* pmo =
        sPerfMonitor.start(PERF_MON_TRIGGER, trigger->getName(), &aiObjectContext->performanceStack);
    Event event = trigger->Check();
    trigger->SetCheckedThisPass(true);
    if (pmo)
        pmo->finish();

    if (!event)
        return;

    if (trigger->IsBuffTrigger() && !trigger->IsDebuffTrigger())
        botAI->forceRebuff.NoteBuffProposed();

    fires.emplace_back(trigger, event);
    LogAction("T:%s", trigger->getName().c_str());
}

void Engine::ProcessTriggers(bool minimal)
{
    if (!nodesPartitioned)
        PartitionTriggerNodes();

    // Few triggers fire per pass; a flat list avoids the per-pass hash map allocations
    std::vector<std::pair<Trigger*, Event>> fires;
    uint32 now = getMSTime();

    // A pending chat command or packet is taken by the first engine that has a node for its trigger
    // and left in the list for another engine otherwise, as when every engine checked every trigger
    std::vector<Trigger*>& pending = aiObjectContext->GetPendingExternalTriggers();
    std::vector<Trigger*> taken;
    for (auto p = pending.begin(); p != pending.end();)
    {
        bool hasNode = false;
        for (size_t i : externalNodes)
        {
            if (nodeTriggers[i] != *p)
                continue;

            CheckTrigger(i, now, minimal, fires);
            hasNode = true;
        }

        if (!hasNode)
        {
            ++p;
            continue;
        }

        taken.push_back(*p);
        p = pending.erase(p);
    }

    for (size_t i : checkNodes)
        CheckTrigger(i, now, minimal, fires);

    if (!fires.empty())
    {
        for (size_t i = 0; i < triggers.size(); ++i)
        {
            Trigger* trigger = nodeTriggers[i];
            auto fired =
                std::find_if(fires.begin(), fires.end(), [trigger](auto const& f) { return f.first == trigger; });
            if (fired == fires.end())
                continue;

            MultiplyAndPush(triggers[i]->getHandlers(trigger), 0.0f, false, fired->second, "trigger");
        }
    }

    for (size_t i : checkNodes)
    {
        nodeTriggers[i]->Reset();
        nodeTriggers[i]->SetCheckedThisPass(false);
    }

    for (Trigger* trigger : taken)
    {
        trigger->Reset();
        trigger->SetCheckedThisPass(false);
    }
}

void Engine::PushDefaultActions()
{
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        Strategy* strategy = i->second;
        Event emptyEvent;
        MultiplyAndPush(strategy->getDefaultActions(), 0.0f, false, emptyEvent, "default");
    }
}

std::string const Engine::ListStrategies()
{
    std::string s = "Strategies: ";

    if (strategies.empty())
        return s;

    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        s.append(i->first);
        s.append(", ");
    }

    return s.substr(0, s.length() - 2);
}

std::vector<std::string> Engine::GetStrategies()
{
    std::vector<std::string> result;
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        result.push_back(i->first);
    }

    return result;
}

void Engine::PushAgain(ActionNode* actionNode, float relevance, Event event)
{
    std::vector<NextAction> nextAction = { NextAction(actionNode->getName(), relevance) };

    MultiplyAndPush(nextAction, relevance, true, event, "again");

    delete actionNode;
}

bool Engine::ContainsStrategy(StrategyType type)
{
    for (std::map<std::string, Strategy*>::iterator i = strategies.begin(); i != strategies.end(); i++)
    {
        if (i->second->GetType() & type)
            return true;
    }
    return false;
}

Action* Engine::InitializeAction(ActionNode* actionNode)
{
    Action* action = actionNode->getAction();
    if (!action)
    {
        action = aiObjectContext->GetAction(actionNode->getName());
        actionNode->setAction(action);
    }

    if (action)
    {
        action->SetReaction(false);
        // Clear duration leftovers so ListenAndExecute doesn't re-apply a stale delay
        action->ResetDuration();
    }

    return action;
}

bool Engine::ListenAndExecute(Action* action, Event event)
{
    bool actionExecuted = false;

    if (action == nullptr)
    {
        LOG_ERROR("playerbots", "Action is nullptr");

        return actionExecuted;
    }

    if (actionExecutionListeners.Before(action, event))
    {
        actionExecuted = actionExecutionListeners.AllowExecution(action, event) ? action->Execute(event) : true;
    }

    if (botAI->HasStrategy("debug", BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;
        out << "do: ";
        out << action->getName();

        if (actionExecuted)
            out << " 1 (";
        else
            out << " 0 (";

        out << action->getRelevance() << ")";

        if (!event.GetSource().empty())
            out << " [" << event.GetSource() << "]";

        botAI->TellMasterNoFacing(out);
    }

    actionExecuted = actionExecutionListeners.OverrideResult(action, actionExecuted, event);
    actionExecutionListeners.After(action, actionExecuted, event);

    if (actionExecuted && action->GetDuration() > 0)
        botAI->SetActionDuration(action);

    return actionExecuted;
}

void Engine::LogAction(char const* format, ...)
{
    Player* bot = botAI->GetBot();
    if (sPlayerbotAIConfig.LogInGroupOnly && (!bot->GetGroup() || !botAI->HasGameClientMaster()) && !testMode)
        return;

    char buf[1024];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    lastAction += "|";
    lastAction += buf;
    if (lastAction.size() > 512)
    {
        lastAction = lastAction.substr(512);
        size_t pos = lastAction.find("|");
        lastAction = (pos == std::string::npos ? "" : lastAction.substr(pos));
    }

    if (testMode)
    {
        FILE* file = fopen("test.log", "a");
        fprintf(file, "'%s'", buf);
        fprintf(file, "\n");
        fclose(file);
    }
    else
    {
        LOG_DEBUG("playerbots", "{} {}", bot->GetName().c_str(), buf);
    }
}

std::string const Engine::ResolveStrategyName(std::string const name)
{
    if (HasStrategy(name))
        return name;

    Strategy* strategy = aiObjectContext->GetStrategy(name);
    return strategy ? strategy->getName() : name;
}

void Engine::ChangeStrategy(std::string const names)
{
    std::vector<std::string> splitted = split(names, ',');
    for (std::vector<std::string>::iterator i = splitted.begin(); i != splitted.end(); i++)
    {
        char const* name = i->c_str();
        switch (name[0])
        {
            case '+':
                addStrategy(name + 1);
                break;
            case '-':
                removeStrategy(ResolveStrategyName(name + 1));
                break;
            case '~':
                toggleStrategy(ResolveStrategyName(name + 1));
                break;
            case '?':
                botAI->TellMaster(ListStrategies());
                break;
        }
    }
}

void Engine::LogValues()
{
    if (testMode)
        return;

    Player* bot = botAI->GetBot();
    if (sPlayerbotAIConfig.LogInGroupOnly && (!bot->GetGroup() || !botAI->HasGameClientMaster()))
        return;

    std::string const text = botAI->GetAiObjectContext()->FormatValues();
    LOG_DEBUG("playerbots", "Values for {}: {}", bot->GetName().c_str(), text.c_str());
}
