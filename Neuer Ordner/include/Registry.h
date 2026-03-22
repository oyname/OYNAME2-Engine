#pragma once

#include "ECSTypes.h"
#include "ComponentPool.h"

#include <memory>
#include <typeindex>    // EntityID-Hash bleibt in ECSTypes; typeindex hier nicht mehr gebraucht
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

        ++m_aliveCount;

        if (!m_freeList.empty())
        {
            const EntityIndex idx = m_freeList.back();
            m_freeList.pop_back();
            ++m_generations[idx];
            m_alive[idx] = 1u;
            return EntityID::Make(idx, m_generations[idx]);
        }

        const EntityIndex idx = static_cast<EntityIndex>(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(1u);
        return EntityID::Make(idx, 0);
    }

    void DestroyEntity(EntityID id)
    {
        if (!IsAlive(id))
            return;

        const EntityIndex idx = id.Index();
        m_alive[idx] = 0u;
        --m_aliveCount;
        m_freeList.push_back(idx);
        ++m_structureVersion;

        for (auto& pool : m_pools)
            if (pool) pool->Remove(id);
    }

    bool IsAlive(EntityID id) const
    {
        const EntityIndex idx = id.Index();
        if (idx >= m_generations.size())
            return false;
        return m_alive[idx] != 0u && (m_generations[idx] == id.Generation());
    }

    size_t EntityCount() const { return m_aliveCount; }

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

    size_t PoolCount() const { return m_poolCount; }

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
        const auto& entities = pivot->Entities();

        for (size_t i = 0; i < entities.size(); ++i)
        {
            const EntityID id = entities[i];
            if (!IsAlive(id))
                continue;

            // Ein Get() pro Pool = ein DenseIndex()-Aufruf pro Pool.
            // Vorher: AllComponentsPresentExcept (Has → DenseIndex) +
            //         InvokeViewCallback (Get → DenseIndex) = 2× pro Pool.
            // Jetzt:  Get() gibt nullptr zurück wenn fehlend → 1× pro Pool.
            auto ptrs = std::make_tuple(std::get<ComponentPool<Ts>*>(pools)->Get(id)...);

            // Alle Pointer müssen gültig sein (keiner null)
            if ((... || !std::get<Ts*>(ptrs)))
                continue;

            func(id, (*std::get<Ts*>(ptrs))...);
        }
    }

    template<typename T>
    ComponentPool<T>& Pool()
    {
        const uint32_t id = ComponentTypeID<T>::value;
        if (id >= m_pools.size())
            m_pools.resize(static_cast<size_t>(id) + 1u);

        if (!m_pools[id])
        {
            m_pools[id] = std::make_unique<ComponentPool<T>>();
            ++m_poolCount;
        }

        return *static_cast<ComponentPool<T>*>(m_pools[id].get());
    }

    template<typename T>
    ComponentPool<T>* FindPool()
    {
        const uint32_t id = ComponentTypeID<T>::value;
        if (id >= m_pools.size() || !m_pools[id])
            return nullptr;
        return static_cast<ComponentPool<T>*>(m_pools[id].get());
    }

    template<typename T>
    const ComponentPool<T>* FindPool() const
    {
        const uint32_t id = ComponentTypeID<T>::value;
        if (id >= m_pools.size() || !m_pools[id])
            return nullptr;
        return static_cast<const ComponentPool<T>*>(m_pools[id].get());
    }

    std::vector<EntityGeneration> m_generations;
    std::vector<uint8_t> m_alive;       // uint8_t statt vector<bool>: kein Bit-Packing, direkter Byte-Zugriff
    size_t m_aliveCount = 0u;           // O(1) EntityCount() — wird in Create/Destroy gepflegt
    std::vector<EntityIndex> m_freeList;
    std::vector<std::unique_ptr<IComponentPool>> m_pools;  // direkt per ComponentTypeID<T>::value indexiert — O(1), kein Hash
    size_t m_poolCount = 0u;            // Anzahl tatsächlich angelegter Pools (Slots ohne nullptr)
    uint64_t m_structureVersion = 0ull;
};
