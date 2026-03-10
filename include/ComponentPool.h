#pragma once

#include "ECSTypes.h"

#include <unordered_map>
#include <typeindex>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// IComponentPool — type-erased base so Registry can hold heterogeneous pools.
// ---------------------------------------------------------------------------
struct IComponentPool
{
    virtual ~IComponentPool() = default;

    // Remove the component for this entity (no-op if absent).
    virtual void Remove(EntityID id) = 0;

    // Returns true if this entity has this component.
    virtual bool Has(EntityID id) const = 0;

    // Number of live components in this pool.
    virtual size_t Count() const = 0;
};

// ---------------------------------------------------------------------------
// ComponentPool<T> — stores one component type T, keyed by EntityID.
//
// Storage choice for Step 1: unordered_map<EntityID, T>.
//
// Why not sparse-set / archetype yet?
//   - Correct first, fast later.
//   - unordered_map gives O(1) lookup with zero migration cost.
//   - When View<> iteration shows up in a profiler, swap to sparse-set.
//     The interface stays identical; only the pool internals change.
//
// Components must be:
//   - Default-constructible (for Add with no args)
//   - Move-constructible (stored by value in the map)
// ---------------------------------------------------------------------------
template<typename T>
class ComponentPool final : public IComponentPool
{
public:
    // Add a component by constructing it in-place from args.
    // Returns a reference to the stored component.
    // If the entity already has one, it is overwritten.
    template<typename... Args>
    T& Emplace(EntityID id, Args&&... args)
    {
        auto [it, _] = m_data.try_emplace(id, std::forward<Args>(args)...);
        it->second = T{ std::forward<Args>(args)... };
        return it->second;
    }

    // Store a copy/move of an existing T.
    T& Insert(EntityID id, T value)
    {
        m_data[id] = std::move(value);
        return m_data[id];
    }

    // Returns a pointer to the component, or nullptr if absent.
    T* Get(EntityID id)
    {
        auto it = m_data.find(id);
        return it != m_data.end() ? &it->second : nullptr;
    }

    const T* Get(EntityID id) const
    {
        auto it = m_data.find(id);
        return it != m_data.end() ? &it->second : nullptr;
    }

    // IComponentPool interface
    void   Remove(EntityID id) override { m_data.erase(id); }
    bool   Has(EntityID id)    const override { return m_data.count(id) > 0; }
    size_t Count()             const override { return m_data.size(); }

    // Iterate all (EntityID, T&) pairs — used by Registry::View<>.
    // Returns a view of the internal map; stable while no insertions occur.
    const std::unordered_map<EntityID, T>& All() const { return m_data; }
          std::unordered_map<EntityID, T>& All()       { return m_data; }

private:
    std::unordered_map<EntityID, T> m_data;
};
