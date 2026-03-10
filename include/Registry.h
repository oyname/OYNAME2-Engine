#pragma once

#include "ECSTypes.h"
#include "ComponentPool.h"

#include <unordered_map>
#include <typeindex>
#include <memory>
#include <vector>
#include <cassert>
#include <functional>

// ---------------------------------------------------------------------------
// Registry — the heart of the ECS.
//
// Responsibilities:
//   1. Entity lifecycle  — CreateEntity / DestroyEntity / IsAlive
//   2. Component storage — Add / Get / Remove / Has
//   3. Iteration         — View<T...>(callback) iterates all entities
//                          that have every listed component type
//
// Design decisions:
//
//   - Entities are generation-safe uint32_t handles.  A destroyed entity's
//     slot is recycled with an incremented generation so stale handles are
//     detected immediately.
//
//   - Components live in per-type ComponentPool<T> objects stored in a
//     type-indexed map.  The registry never knows the concrete types at
//     compile time — only the callers do.
//
//   - View<T...> iterates the smallest pool first and checks membership in
//     all other required pools.  This is correct for any number of pools and
//     already reasonably efficient for scenes with hundreds to low thousands
//     of entities.  Replace with sparse-set later when needed.
//
//   - NO inheritance required.  Entities are just IDs.  Components are plain
//     structs.  Systems are free functions or lambdas.
// ---------------------------------------------------------------------------
class Registry
{
public:
    Registry() = default;

    // -----------------------------------------------------------------------
    // Entity lifecycle
    // -----------------------------------------------------------------------

    // Create a new live entity and return its ID.
    EntityID CreateEntity()
    {
        if (!m_freeList.empty())
        {
            // Recycle a slot, bump its generation.
            const EntityIndex idx = m_freeList.back();
            m_freeList.pop_back();
            ++m_generations[idx];   // generation wraps around — intentional
            m_alive[idx] = true;
            return EntityID::Make(idx, m_generations[idx]);
        }

        // Brand-new slot.
        const EntityIndex idx = static_cast<EntityIndex>(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(true);
        return EntityID::Make(idx, 0);
    }

    // Destroy an entity and all its components.
    // The slot is recycled the next time CreateEntity() is called.
    void DestroyEntity(EntityID id)
    {
        if (!IsAlive(id)) return;

        const EntityIndex idx = id.Index();
        m_alive[idx] = false;
        m_freeList.push_back(idx);

        // Remove all components that belong to this entity.
        for (auto& [typeIdx, pool] : m_pools)
            pool->Remove(id);
    }

    // Returns true if id refers to a currently live entity.
    bool IsAlive(EntityID id) const
    {
        const EntityIndex idx = id.Index();
        if (idx >= m_generations.size()) return false;
        return m_alive[idx] && (m_generations[idx] == id.Generation());
    }

    // Total number of currently live entities.
    size_t EntityCount() const
    {
        size_t count = 0;
        for (bool b : m_alive) count += b ? 1 : 0;
        return count;
    }

    // -----------------------------------------------------------------------
    // Component management
    // -----------------------------------------------------------------------

    // Add a component T to entity id and return a reference to it.
    // If the entity already has a T, it is overwritten.
    // Asserts that the entity is alive.
    template<typename T, typename... Args>
    T& Add(EntityID id, Args&&... args)
    {
        assert(IsAlive(id) && "Add: entity is not alive");
        return Pool<T>().Emplace(id, std::forward<Args>(args)...);
    }

    // Add a pre-constructed component by value / move.
    template<typename T>
    T& Add(EntityID id, T value)
    {
        assert(IsAlive(id) && "Add: entity is not alive");
        return Pool<T>().Insert(id, std::move(value));
    }

    // Returns a pointer to T for this entity, or nullptr if absent.
    template<typename T>
    T* Get(EntityID id)
    {
        auto* p = FindPool<T>();
        return p ? p->Get(id) : nullptr;
    }

    template<typename T>
    const T* Get(EntityID id) const
    {
        const auto* p = FindPool<T>();
        return p ? p->Get(id) : nullptr;
    }

    // Returns true if entity has component T.
    template<typename T>
    bool Has(EntityID id) const
    {
        const auto* p = FindPool<T>();
        return p && p->Has(id);
    }

    // Remove component T from entity (no-op if absent).
    template<typename T>
    void Remove(EntityID id)
    {
        auto* p = FindPool<T>();
        if (p) p->Remove(id);
    }

    // -----------------------------------------------------------------------
    // View — iterate all live entities that have ALL of Ts...
    // -----------------------------------------------------------------------
    //
    // Usage:
    //   registry.View<TransformComponent, MeshComponent>(
    //       [](EntityID id, TransformComponent& t, MeshComponent& m) { ... });
    //
    // Iterates the first pool (pivot), skips entities missing any other pool.

    template<typename First, typename... Rest, typename Func>
    void View(Func&& func)
    {
        auto& pivot = Pool<First>();

        for (auto& [id, firstComp] : pivot.All())
        {
            if (!IsAlive(id)) continue;

            // Check all Rest pools contain this entity.
            if (!(... && Pool<Rest>().Has(id))) continue;

            // Invoke callback: (EntityID, First&, Rest&...)
            func(id, firstComp, *Pool<Rest>().Get(id)...);
        }
    }

    // Const View — delegates to mutable version (components are non-const in callback).
    template<typename First, typename... Rest, typename Func>
    void View(Func&& func) const
    {
        const_cast<Registry*>(this)->View<First, Rest...>(std::forward<Func>(func));
    }

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    // Number of registered component pools.
    size_t PoolCount() const { return m_pools.size(); }

    // Number of components of type T currently stored.
    template<typename T>
    size_t ComponentCount() const
    {
        const auto* p = FindPool<T>();
        return p ? p->Count() : 0u;
    }

private:
    // -----------------------------------------------------------------------
    // Internal pool management
    // -----------------------------------------------------------------------

    template<typename T>
    ComponentPool<T>& Pool()
    {
        const auto key = std::type_index(typeid(T));
        auto it = m_pools.find(key);
        if (it == m_pools.end())
        {
            auto pool = std::make_unique<ComponentPool<T>>();
            auto* raw = pool.get();
            m_pools.emplace(key, std::move(pool));
            return *raw;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }

    template<typename T>
    ComponentPool<T>* FindPool()
    {
        const auto key = std::type_index(typeid(T));
        auto it = m_pools.find(key);
        return it != m_pools.end()
            ? static_cast<ComponentPool<T>*>(it->second.get())
            : nullptr;
    }

    template<typename T>
    const ComponentPool<T>* FindPool() const
    {
        const auto key = std::type_index(typeid(T));
        auto it = m_pools.find(key);
        return it != m_pools.end()
            ? static_cast<const ComponentPool<T>*>(it->second.get())
            : nullptr;
    }

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------
    std::vector<EntityGeneration>                           m_generations;
    std::vector<bool>                                       m_alive;
    std::vector<EntityIndex>                                m_freeList;
    std::unordered_map<std::type_index,
                       std::unique_ptr<IComponentPool>>     m_pools;
};
