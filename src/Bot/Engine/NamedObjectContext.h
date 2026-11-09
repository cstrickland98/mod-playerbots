/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_NAMEDOBJECTCONTEXT_H
#define PLAYERBOTS_NAMEDOBJECTCONTEXT_H

#include "Common.h"
#include <functional>
#include <list>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class PlayerbotAI;

class Qualified
{
public:
    Qualified(){};
    Qualified(std::string const qualifier) : qualifier(qualifier) {}
    Qualified(int32 qualifier1) { Qualify(qualifier1); }

    virtual void Qualify(int qual);

    virtual void Qualify(std::string const qual) { qualifier = qual; }

    std::string const getQualifier() { return qualifier; }

    static std::string const MultiQualify(std::vector<std::string> const& qualifiers, std::string const& separator,
                                          const std::string_view brackets = "{}");
    static std::vector<std::string> getMultiQualifiers(std::string const& qualifier1);
    static int32 getMultiQualifier(std::string const& qualifier1, uint32 pos);

protected:
    std::string qualifier;
};

template <class T>
class NamedObjectFactory
{
public:
    using ObjectCreator = std::function<T*(PlayerbotAI* botAI)>;
    std::unordered_map<std::string, ObjectCreator> creators;

public:
    virtual ~NamedObjectFactory() = default;

    // Creates "name" or "name::qualifier" from a creator map; read-only, so several threads may share the map
    static T* CreateFrom(std::unordered_map<std::string, ObjectCreator> const& creators, std::string const& fullName,
                         PlayerbotAI* botAI)
    {
        size_t found = fullName.find("::");
        std::string name = found != std::string::npos ? fullName.substr(0, found) : fullName;

        auto creator = creators.find(name);
        if (creator == creators.end())
            return nullptr;

        T* object = creator->second(botAI);
        Qualified* q = dynamic_cast<Qualified*>(object);
        if (q && found != std::string::npos)
            q->Qualify(fullName.substr(found + 2));

        return object;
    }

    virtual T* create(std::string name, PlayerbotAI* botAI) { return CreateFrom(creators, name, botAI); }

    std::set<std::string> supports()
    {
        std::set<std::string> keys;
        for (typename std::unordered_map<std::string, ObjectCreator>::const_iterator it = creators.begin();
             it != creators.end(); it++)
            keys.insert(it->first);

        return keys;
    }
};

template <class T>
class NamedObjectContext : public NamedObjectFactory<T>
{
public:
    NamedObjectContext(bool shared = false, bool supportsSiblings = false)
        : NamedObjectFactory<T>(), shared(shared), supportsSiblings(supportsSiblings)
    {
    }

    virtual ~NamedObjectContext() { Clear(); }

    virtual T* create(std::string name, PlayerbotAI* botAI) override
    {
        if (created.find(name) == created.end())
            return created[name] = NamedObjectFactory<T>::create(name, botAI);

        return created[name];
    }

    void Clear()
    {
        for (typename std::unordered_map<std::string, T*>::const_iterator i = created.begin(); i != created.end(); i++)
        {
            if (i->second)
                delete i->second;
        }

        created.clear();
    }

    bool IsShared() { return shared; }
    bool IsSupportsSiblings() { return supportsSiblings; }

    std::set<std::string> GetCreated()
    {
        std::set<std::string> keys;
        for (typename std::unordered_map<std::string, T*>::iterator it = created.begin(); it != created.end(); it++)
            keys.insert(it->first);

        return keys;
    }

protected:
    std::unordered_map<std::string, T*> created;
    bool shared;
    bool supportsSiblings;
};

template <class T>
class SharedNamedObjectContextList
{
public:
    using ObjectCreator = std::function<T*(PlayerbotAI* botAI)>;
    std::unordered_map<std::string, ObjectCreator> creators;
    std::vector<NamedObjectContext<T>*> contexts;

    ~SharedNamedObjectContextList()
    {
        for (typename std::vector<NamedObjectContext<T>*>::const_iterator i = contexts.begin(); i != contexts.end(); i++)
            delete *i;
    }

    void Add(NamedObjectContext<T>* context)
    {
        contexts.push_back(context);
        for (auto const& iter : context->creators)
            creators[iter.first] = iter.second;
    }
};

template <class T>
class NamedObjectContextList
{
public:
    using ObjectCreator = std::function<T*(PlayerbotAI* botAI)>;
    std::unordered_map<std::string, ObjectCreator> const& creators;
    std::vector<NamedObjectContext<T>*> const& contexts;
    std::unordered_map<std::string, T*> created;

    NamedObjectContextList(SharedNamedObjectContextList<T> const& shared)
        : creators(shared.creators), contexts(shared.contexts)
    {
    }

    ~NamedObjectContextList()
    {
        for (typename std::unordered_map<std::string, T*>::const_iterator i = created.begin(); i != created.end(); i++)
        {
            if (i->second)
                delete i->second;
        }

        created.clear();
    }

    T* create(std::string name, PlayerbotAI* botAI)
    {
        return NamedObjectFactory<T>::CreateFrom(creators, name, botAI);
    }

    T* GetContextObject(std::string const& name, PlayerbotAI* botAI)
    {
        if (created.find(name) == created.end())
        {
            if (T* object = create(name, botAI))
                return created[name] = object;
        }

        return created[name];
    }

    std::set<std::string> GetSiblings(std::string const& name)
    {
        for (auto i = contexts.begin(); i != contexts.end(); i++)
        {
            if (!(*i)->IsSupportsSiblings())
                continue;

            std::set<std::string> supported = (*i)->supports();
            std::set<std::string>::iterator found = supported.find(name);
            if (found == supported.end())
                continue;

            supported.erase(found);
            return supported;
        }

        return std::set<std::string>();
    }

    std::set<std::string> supports()
    {
        std::set<std::string> result;
        for (auto i = contexts.begin(); i != contexts.end(); i++)
        {
            std::set<std::string> supported = (*i)->supports();
            for (std::set<std::string>::const_iterator j = supported.begin(); j != supported.end(); ++j)
                result.insert(*j);
        }

        return result;
    }

    std::set<std::string> GetCreated()
    {
        std::set<std::string> result;
        for (typename std::unordered_map<std::string, T*>::const_iterator i = created.begin(); i != created.end(); i++)
            result.insert(i->first);

        return result;
    }
};

// Returns the one process-wide instance of a creator-only factory. Factories hold nothing but a map of
// name -> creator function, identical for every bot, so one copy serves all bots on all threads.
template <class F>
F* SharedFactory()
{
    static F instance;
    return &instance;
}

// Non-owning list of shared factories; the last one added wins on a name both define
template <class T>
class NamedObjectFactoryList
{
public:
    using ObjectCreator = std::function<T*(PlayerbotAI* botAI)>;
    std::vector<NamedObjectFactory<T>*> factories;

    T* create(std::string const& name, PlayerbotAI* botAI)
    {
        for (auto i = factories.rbegin(); i != factories.rend(); ++i)
            if (T* object = (*i)->create(name, botAI))
                return object;

        return nullptr;
    }

    void Add(NamedObjectFactory<T>* factory) { factories.push_back(factory); }

    T* GetContextObject(std::string const& name, PlayerbotAI* botAI) { return create(name, botAI); }
};

#endif
