#pragma once

#include "ECSTypes.h"
#include "ComponentPool.h"

#include <unordered_map>
#include <typeindex>
#include <memory>
#include <vector>
#include <cassert>
#include <functional>
#include <tuple>
#include <cstdint>

class Registry
{
public:
    Registry() = default;

    EntityID CreateEntity()
    {
        ++m_structureVersion;

        if (!m_freeList.empty())
        {
            const EntityIndex idx = m_freeList.back();
            m_freeList.pop_back();
            ++m_generations[idx];
            m_alive[idx] = true;
            return EntityID::Make(idx, m_generations[idx]);
        }

        const EntityIndex idx = static_cast<EntityIndex>(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(true);
        return EntityID::Make(idx, 0);
    }

    void DestroyEntity(EntityID id)
    {
        if (!IsAlive(id))
            return;

        const EntityIndex idx = id.Index();
        m_alive[idx] = false;
        m_freeList.push_back(idx);
        ++m_structureVersion;

        for (auto& kv : m_pools)
            kv.second->Remove(id);
    }

    bool IsAlive(EntityID id) const
    {
        const EntityIndex idx = id.Index();
        if (idx >= m_generations.size())
            return false;
        return m_alive[idx] && (m_generations[idx] == id.Generation());
    }

    size_t EntityCount() const
    {
        size_t count = 0;
        for (bool b : m_alive)
            count += b ? 1u : 0u;
        return count;
    }

    uint64_t GetStructureVersion() const noexcept { return m_structureVersion; }

    template<typename T, typename... Args>
    T& Add(EntityID id, Args&&... args)
    {
        assert(IsAlive(id) && "Add: entity is not alive");
        ++m_structureVersion;
        return Pool<T>().Emplace(id, std::forward<Args>(args)...);
    }

    template<typename T>
    T& Add(EntityID id, T value)
    {
        assert(IsAlive(id) && "Add: entity is not alive");
        ++m_structureVersion;
        return Pool<T>().Insert(id, std::move(value));
    }

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

    template<typename T>
    bool Has(EntityID id) const
    {
        const auto* p = FindPool<T>();
        return p && p->Has(id);
    }

    template<typename T>
    void Remove(EntityID id)
    {
        auto* p = FindPool<T>();
        if (p)
        {
            ++m_structureVersion;
            p->Remove(id);
        }
    }

    template<typename T>
    void MarkComponentChanged(EntityID id)
    {
        auto* p = FindPool<T>();
        if (p)
            p->MarkChanged(id);
    }

    template<typename T>
    uint64_t GetPoolVersion() const
    {
        const auto* p = FindPool<T>();
        return p ? p->StructureVersion() : 0ull;
    }

    template<typename T>
    uint64_t GetComponentVersion(EntityID id) const
    {
        const auto* p = FindPool<T>();
        return p ? p->ChangeVersion(id) : 0ull;
    }

    template<typename First, typename... Rest, typename Func>
    void View(Func&& func)
    {
        ViewSmallestPivot<First, Rest...>(std::forward<Func>(func));
    }

    template<typename First, typename... Rest, typename Func>
    void View(Func&& func) const
    {
        const_cast<Registry*>(this)->View<First, Rest...>(std::forward<Func>(func));
    }

    size_t PoolCount() const { return m_pools.size(); }

    template<typename T>
    size_t ComponentCount() const
    {
        const auto* p = FindPool<T>();
        return p ? p->Count() : 0u;
    }

    template<typename T>
    ComponentPool<T>* TryGetPool()
    {
        return FindPool<T>();
    }

    template<typename T>
    const ComponentPool<T>* TryGetPool() const
    {
        return FindPool<T>();
    }

private:
    template<typename... Ts, typename Func>
    void ViewSmallestPivot(Func&& func)
    {
        auto pools = std::tuple<ComponentPool<Ts>*...>{ FindPool<Ts>()... };
        if ((... || (std::get<ComponentPool<Ts>*>(pools) == nullptr)))
            return;

        size_t pivotIndex = 0u;
        size_t pivotCount = static_cast<size_t>(-1);
        DeterminePivotIndex<Ts...>(pools, pivotIndex, pivotCount);
        DispatchViewByPivot<Ts...>(pivotIndex, pools, std::forward<Func>(func));
    }

    template<typename... Ts>
    static void DeterminePivotIndex(const std::tuple<ComponentPool<Ts>*...>& pools,
                                    size_t& pivotIndex,
                                    size_t& pivotCount)
    {
        DeterminePivotIndexImpl<0u, Ts...>(pools, pivotIndex, pivotCount);
    }

    template<size_t I, typename... Ts>
    static void DeterminePivotIndexImpl(const std::tuple<ComponentPool<Ts>*...>& pools,
                                        size_t& pivotIndex,
                                        size_t& pivotCount)
    {
        if constexpr (I < sizeof...(Ts))
        {
            const size_t count = std::get<I>(pools)->Count();
            if (count < pivotCount)
            {
                pivotCount = count;
                pivotIndex = I;
            }

            DeterminePivotIndexImpl<I + 1u, Ts...>(pools, pivotIndex, pivotCount);
        }
    }

    template<typename... Ts, typename Func>
    void DispatchViewByPivot(size_t pivotIndex,
                             const std::tuple<ComponentPool<Ts>*...>& pools,
                             Func&& func)
    {
        DispatchViewByPivotImpl<0u, Ts...>(pivotIndex, pools, std::forward<Func>(func));
    }

    template<size_t I, typename... Ts, typename Func>
    void DispatchViewByPivotImpl(size_t pivotIndex,
                                 const std::tuple<ComponentPool<Ts>*...>& pools,
                                 Func&& func)
    {
        if constexpr (I < sizeof...(Ts))
        {
            if (pivotIndex == I)
            {
                IteratePivot<I, Ts...>(pools, std::forward<Func>(func));
                return;
            }

            DispatchViewByPivotImpl<I + 1u, Ts...>(pivotIndex, pools, std::forward<Func>(func));
        }
    }

    template<size_t PivotIndex, typename... Ts, typename Func>
    void IteratePivot(const std::tuple<ComponentPool<Ts>*...>& pools, Func&& func)
    {
        auto* pivot = std::get<PivotIndex>(pools);
        auto& entities = pivot->Entities();
        for (size_t i = 0; i < entities.size(); ++i)
        {
            const EntityID id = entities[i];
            if (!IsAlive(id))
                continue;

            if (!AllComponentsPresentExcept<PivotIndex, Ts...>(id, pools))
                continue;

            InvokeViewCallback<Ts...>(id, pools, std::forward<Func>(func));
        }
    }

    template<size_t SkipIndex, typename... Ts>
    static bool AllComponentsPresentExcept(EntityID id, const std::tuple<ComponentPool<Ts>*...>& pools)
    {
        return AllComponentsPresentExceptImpl<SkipIndex, 0u, Ts...>(id, pools);
    }

    template<size_t SkipIndex, size_t I, typename... Ts>
    static bool AllComponentsPresentExceptImpl(EntityID id, const std::tuple<ComponentPool<Ts>*...>& pools)
    {
        if constexpr (I >= sizeof...(Ts))
        {
            return true;
        }
        else
        {
            if constexpr (I == SkipIndex)
            {
                return AllComponentsPresentExceptImpl<SkipIndex, I + 1u, Ts...>(id, pools);
            }
            else
            {
                return std::get<I>(pools)->Has(id)
                    && AllComponentsPresentExceptImpl<SkipIndex, I + 1u, Ts...>(id, pools);
            }
        }
    }

    template<typename... Ts, typename Func>
    static void InvokeViewCallback(EntityID id, const std::tuple<ComponentPool<Ts>*...>& pools, Func&& func)
    {
        func(id, (*std::get<ComponentPool<Ts>*>(pools)->Get(id))...);
    }

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
        const auto it = m_pools.find(std::type_index(typeid(T)));
        return it != m_pools.end()
            ? static_cast<ComponentPool<T>*>(it->second.get())
            : nullptr;
    }

    template<typename T>
    const ComponentPool<T>* FindPool() const
    {
        const auto it = m_pools.find(std::type_index(typeid(T)));
        return it != m_pools.end()
            ? static_cast<const ComponentPool<T>*>(it->second.get())
            : nullptr;
    }

    std::vector<EntityGeneration> m_generations;
    std::vector<bool> m_alive;
    std::vector<EntityIndex> m_freeList;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> m_pools;
    uint64_t m_structureVersion = 0ull;
};
