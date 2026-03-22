#pragma once

#include "ECSTypes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <utility>
#include <vector>

struct IComponentPool
{
    virtual ~IComponentPool() = default;
    virtual void Remove(EntityID id) = 0;
    virtual bool Has(EntityID id) const = 0;
    virtual size_t Count() const = 0;
    virtual uint64_t StructureVersion() const = 0;
    virtual uint64_t ChangeVersion(EntityID id) const = 0;
};

struct TransformComponent;
struct WorldTransformComponent;

namespace detail
{
    template<typename T>
    struct ComponentPoolTraits
    {
        static constexpr size_t ChunkSize = 64u;
        static constexpr bool IsHotComponent = false;
    };

    template<>
    struct ComponentPoolTraits<::TransformComponent>
    {
        static constexpr size_t ChunkSize = 256u;
        static constexpr bool IsHotComponent = true;
    };

    template<>
    struct ComponentPoolTraits<::WorldTransformComponent>
    {
        static constexpr size_t ChunkSize = 256u;
        static constexpr bool IsHotComponent = true;
    };
}

template<typename T>
class ComponentPool final : public IComponentPool
{
public:
    static constexpr size_t kChunkSize = detail::ComponentPoolTraits<T>::ChunkSize;
    static constexpr bool kIsHotComponent = detail::ComponentPoolTraits<T>::IsHotComponent;

    template<typename... Args>
    T& Emplace(EntityID id, Args&&... args)
    {
        const size_t denseIndex = EnsureDenseSlot(id);
        m_components[denseIndex] = T(std::forward<Args>(args)...);
        TouchDenseIndex(denseIndex);
        return m_components[denseIndex];
    }

    T& Insert(EntityID id, T value)
    {
        const size_t denseIndex = EnsureDenseSlot(id);
        m_components[denseIndex] = std::move(value);
        TouchDenseIndex(denseIndex);
        return m_components[denseIndex];
    }

    T* Get(EntityID id)
    {
        const size_t denseIndex = DenseIndex(id);
        return denseIndex != InvalidDenseIndex() ? &m_components[denseIndex] : nullptr;
    }

    const T* Get(EntityID id) const
    {
        const size_t denseIndex = DenseIndex(id);
        return denseIndex != InvalidDenseIndex() ? &m_components[denseIndex] : nullptr;
    }

    void Remove(EntityID id) override
    {
        const size_t denseIndex = DenseIndex(id);
        if (denseIndex == InvalidDenseIndex())
            return;

        const size_t lastIndex = m_components.size() - 1u;
        const EntityIndex removedSparseIndex = id.Index();

        if (denseIndex != lastIndex)
        {
            m_components[denseIndex] = std::move(m_components[lastIndex]);
            m_entities[denseIndex] = m_entities[lastIndex];
            m_denseChangeVersions[denseIndex] = ++m_structureVersion;
            m_sparseDensePlusOne[m_entities[denseIndex].Index()] = denseIndex + 1u;
        }

        m_components.pop_back();
        m_entities.pop_back();
        m_denseChangeVersions.pop_back();
        m_sparseDensePlusOne[removedSparseIndex] = 0u;
        ++m_structureVersion;
    }

    bool Has(EntityID id) const override
    {
        return DenseIndex(id) != InvalidDenseIndex();
    }

    size_t Count() const override { return m_components.size(); }
    uint64_t StructureVersion() const override { return m_structureVersion; }

    uint64_t ChangeVersion(EntityID id) const override
    {
        const size_t denseIndex = DenseIndex(id);
        return denseIndex != InvalidDenseIndex() ? m_denseChangeVersions[denseIndex] : 0ull;
    }

    void MarkChanged(EntityID id)
    {
        const size_t denseIndex = DenseIndex(id);
        if (denseIndex != InvalidDenseIndex())
            TouchDenseIndex(denseIndex);
    }

    const std::vector<EntityID>& Entities() const { return m_entities; }
    std::vector<EntityID>& Entities() { return m_entities; }
    const std::vector<T>& Components() const { return m_components; }
    std::vector<T>& Components() { return m_components; }

    template<typename Fn>
    void ForEachChunk(Fn&& fn) const
    {
        const size_t total = m_entities.size();
        for (size_t begin = 0; begin < total; begin += kChunkSize)
        {
            const size_t end = (std::min)(begin + kChunkSize, total);
            fn(begin, end, m_entities.data() + begin, m_components.data() + begin);
        }
    }

    template<typename Fn>
    void ForEachChunk(Fn&& fn)
    {
        const size_t total = m_entities.size();
        for (size_t begin = 0; begin < total; begin += kChunkSize)
        {
            const size_t end = (std::min)(begin + kChunkSize, total);
            fn(begin, end, m_entities.data() + begin, m_components.data() + begin);
        }
    }

private:
    static constexpr size_t InvalidDenseIndex() { return static_cast<size_t>(-1); }

    void EnsureSparseCapacity(EntityIndex sparseIndex)
    {
        const size_t need = static_cast<size_t>(sparseIndex) + 1u;
        if (m_sparseDensePlusOne.size() < need)
            m_sparseDensePlusOne.resize(need, 0u);
        if (m_sparseGenerations.size() < need)
            m_sparseGenerations.resize(need, 0u);
    }

    size_t DenseIndex(EntityID id) const
    {
        const size_t sparseIndex = static_cast<size_t>(id.Index());
        if (sparseIndex >= m_sparseDensePlusOne.size())
            return InvalidDenseIndex();

        const size_t densePlusOne = m_sparseDensePlusOne[sparseIndex];
        if (densePlusOne == 0u)
            return InvalidDenseIndex();

        if (m_sparseGenerations[sparseIndex] != id.Generation())
            return InvalidDenseIndex();

        const size_t denseIndex = densePlusOne - 1u;
        if (denseIndex >= m_entities.size())
            return InvalidDenseIndex();

        return m_entities[denseIndex] == id ? denseIndex : InvalidDenseIndex();
    }

    size_t EnsureDenseSlot(EntityID id)
    {
        assert(id.IsValid() && "ComponentPool::EnsureDenseSlot: invalid EntityID");
        EnsureSparseCapacity(id.Index());

        const size_t denseIndex = DenseIndex(id);
        if (denseIndex != InvalidDenseIndex())
            return denseIndex;

        const size_t newIndex = m_components.size();
        m_entities.push_back(id);
        m_components.emplace_back();
        m_denseChangeVersions.push_back(0ull);
        m_sparseDensePlusOne[id.Index()] = newIndex + 1u;
        m_sparseGenerations[id.Index()] = id.Generation();
        ++m_structureVersion;
        return newIndex;
    }

    void TouchDenseIndex(size_t denseIndex)
    {
        m_denseChangeVersions[denseIndex] = ++m_structureVersion;
    }

    std::vector<EntityID> m_entities;
    std::vector<T> m_components;
    std::vector<uint64_t> m_denseChangeVersions;
    std::vector<size_t> m_sparseDensePlusOne;
    std::vector<EntityGeneration> m_sparseGenerations;
    uint64_t m_structureVersion = 0ull;
};
