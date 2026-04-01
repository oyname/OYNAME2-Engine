#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// GDXDescriptorSystem
//
// Backend-neutrales Fundament fuer moderne Descriptor-/Binding-Systeme.
// DX11 kann das weitgehend ignorieren; DX12/Vulkan koennen darauf echte
// Descriptor-Heaps / Pools / Set-Allocation aufsetzen.
//
// Ziel:
//   - persistente Descriptor-Slots (Materiale, statische Views)
//   - frame-transiente Descriptor-Slots (pro Frame / pro Pass / pro Draw)
//   - generationssichere Handles statt rohe Slot-Indizes
//   - Heap-Typen sauber getrennt (Resource vs. Sampler)
// ---------------------------------------------------------------------------

enum class GDXDescriptorHeapType : uint8_t
{
    Resource = 0, // CBV/SRV/UAV aehnliche Heaps / Pools
    Sampler  = 1,
};

enum class GDXDescriptorType : uint8_t
{
    ConstantBuffer = 0,
    ShaderResource = 1,
    UnorderedAccess = 2,
    Sampler = 3,
};

enum class GDXDescriptorLifetime : uint8_t
{
    Persistent = 0,
    FrameTransient = 1,
};

struct GDXDescriptorHandle
{
    static constexpr uint32_t InvalidIndex = (std::numeric_limits<uint32_t>::max)();

    GDXDescriptorHeapType heapType = GDXDescriptorHeapType::Resource;
    uint32_t index = InvalidIndex;
    uint32_t generation = 0u;
    GDXDescriptorType type = GDXDescriptorType::ShaderResource;
    GDXDescriptorLifetime lifetime = GDXDescriptorLifetime::Persistent;

    bool IsValid() const noexcept { return index != InvalidIndex; }
    static GDXDescriptorHandle Invalid() noexcept { return GDXDescriptorHandle(); }
};

struct GDXDescriptorRange
{
    GDXDescriptorHeapType heapType = GDXDescriptorHeapType::Resource;
    uint32_t start = GDXDescriptorHandle::InvalidIndex;
    uint32_t count = 0u;
    GDXDescriptorType type = GDXDescriptorType::ShaderResource;
    GDXDescriptorLifetime lifetime = GDXDescriptorLifetime::Persistent;

    bool IsValid() const noexcept
    {
        return start != GDXDescriptorHandle::InvalidIndex && count > 0u;
    }

    uint32_t EndExclusive() const noexcept
    {
        return IsValid() ? (start + count) : start;
    }
};

struct GDXDescriptorAllocation
{
    GDXDescriptorRange range{};
    uint32_t generation = 0u;

    bool IsValid() const noexcept { return range.IsValid(); }
};

struct GDXDescriptorHeapDesc
{
    GDXDescriptorHeapType heapType = GDXDescriptorHeapType::Resource;
    uint32_t persistentCapacity = 0u;
    uint32_t frameTransientCapacity = 0u;
    uint32_t framesInFlight = 3u;
    bool shaderVisible = true;
};

struct GDXDescriptorAllocatorDesc
{
    GDXDescriptorHeapDesc resourceHeap{};
    GDXDescriptorHeapDesc samplerHeap{};
};

struct GDXDescriptorSetAllocation
{
    GDXDescriptorAllocation resources{};
    GDXDescriptorAllocation samplers{};
    uint64_t layoutHash = 0u;

    bool IsValid() const noexcept
    {
        return resources.IsValid() || samplers.IsValid();
    }
};

class GDXDescriptorArena
{
public:
    bool Initialize(const GDXDescriptorHeapDesc& desc)
    {
        m_desc = desc;
        m_generations.assign(desc.persistentCapacity, 1u);
        m_persistentUsed.assign(desc.persistentCapacity, false);
        m_freePersistent.clear();
        for (uint32_t i = 0; i < desc.persistentCapacity; ++i)
            m_freePersistent.push_back(desc.persistentCapacity - 1u - i);

        const uint32_t frameCount = (desc.framesInFlight == 0u) ? 1u : desc.framesInFlight;
        m_frameTransientHeads.assign(frameCount, 0u);
        m_lastCompletedFrame = 0u;
        m_activeFrame = 0u;
        return true;
    }

    void Shutdown()
    {
        m_generations.clear();
        m_persistentUsed.clear();
        m_freePersistent.clear();
        m_frameTransientHeads.clear();
        m_desc = {};
        m_lastCompletedFrame = 0u;
        m_activeFrame = 0u;
    }

    void BeginFrame(uint32_t frameIndex) noexcept
    {
        if (m_frameTransientHeads.empty())
            return;
        m_activeFrame = frameIndex % static_cast<uint32_t>(m_frameTransientHeads.size());
        m_frameTransientHeads[m_activeFrame] = 0u;
    }

    void MarkFrameComplete(uint32_t frameIndex) noexcept
    {
        m_lastCompletedFrame = frameIndex;
        (void)m_lastCompletedFrame;
    }

    GDXDescriptorAllocation AllocatePersistent(uint32_t count, GDXDescriptorType type) noexcept
    {
        GDXDescriptorAllocation out{};
        if (count == 0u || count > 1u)
        {
            // Persistente Multi-Range-Allocation wird bewusst noch nicht 
            // vorgetaeuscht. Der erste echte Use-Case soll das spaeter
            // sauber erzwingen.
            return out;
        }

        if (m_freePersistent.empty())
            return out;

        const uint32_t slot = m_freePersistent.back();
        m_freePersistent.pop_back();
        m_persistentUsed[slot] = true;

        out.range.heapType = m_desc.heapType;
        out.range.start = slot;
        out.range.count = 1u;
        out.range.type = type;
        out.range.lifetime = GDXDescriptorLifetime::Persistent;
        out.generation = m_generations[slot];
        return out;
    }

    bool FreePersistent(const GDXDescriptorAllocation& allocation) noexcept
    {
        if (!allocation.IsValid())
            return false;
        if (allocation.range.lifetime != GDXDescriptorLifetime::Persistent)
            return false;
        if (allocation.range.count != 1u)
            return false;
        if (allocation.range.start >= m_persistentUsed.size())
            return false;
        if (!m_persistentUsed[allocation.range.start])
            return false;
        if (m_generations[allocation.range.start] != allocation.generation)
            return false;

        m_persistentUsed[allocation.range.start] = false;
        ++m_generations[allocation.range.start];
        if (m_generations[allocation.range.start] == 0u)
            m_generations[allocation.range.start] = 1u;
        m_freePersistent.push_back(allocation.range.start);
        return true;
    }

    GDXDescriptorAllocation AllocateFrameTransient(uint32_t count, GDXDescriptorType type) noexcept
    {
        GDXDescriptorAllocation out{};
        if (count == 0u || m_frameTransientHeads.empty())
            return out;

        uint32_t& head = m_frameTransientHeads[m_activeFrame];
        if ((head + count) > m_desc.frameTransientCapacity)
            return out;

        out.range.heapType = m_desc.heapType;
        out.range.start = head;
        out.range.count = count;
        out.range.type = type;
        out.range.lifetime = GDXDescriptorLifetime::FrameTransient;
        out.generation = m_activeFrame + 1u;
        head += count;
        return out;
    }

    uint32_t GetPersistentCapacity() const noexcept { return m_desc.persistentCapacity; }
    uint32_t GetFrameTransientCapacity() const noexcept { return m_desc.frameTransientCapacity; }
    uint32_t GetPersistentFreeCount() const noexcept { return static_cast<uint32_t>(m_freePersistent.size()); }
    uint32_t GetActiveFrameTransientUsed() const noexcept
    {
        if (m_frameTransientHeads.empty())
            return 0u;
        return m_frameTransientHeads[m_activeFrame];
    }
    GDXDescriptorHeapType GetHeapType() const noexcept { return m_desc.heapType; }
    bool IsShaderVisible() const noexcept { return m_desc.shaderVisible; }

private:
    GDXDescriptorHeapDesc m_desc{};
    std::vector<uint32_t> m_generations;
    std::vector<uint8_t>  m_persistentUsed;
    std::vector<uint32_t> m_freePersistent;
    std::vector<uint32_t> m_frameTransientHeads;
    uint32_t m_lastCompletedFrame = 0u;
    uint32_t m_activeFrame = 0u;
};

class GDXDescriptorAllocator
{
public:
    bool Initialize(const GDXDescriptorAllocatorDesc& desc)
    {
        m_desc = desc;
        return m_resourceArena.Initialize(desc.resourceHeap)
            && m_samplerArena.Initialize(desc.samplerHeap);
    }

    void Shutdown()
    {
        m_resourceArena.Shutdown();
        m_samplerArena.Shutdown();
        m_desc = {};
    }

    void BeginFrame(uint32_t frameIndex) noexcept
    {
        m_resourceArena.BeginFrame(frameIndex);
        m_samplerArena.BeginFrame(frameIndex);
    }

    void MarkFrameComplete(uint32_t frameIndex) noexcept
    {
        m_resourceArena.MarkFrameComplete(frameIndex);
        m_samplerArena.MarkFrameComplete(frameIndex);
    }

    GDXDescriptorAllocation AllocatePersistent(GDXDescriptorHeapType heapType,
                                               GDXDescriptorType type,
                                               uint32_t count = 1u) noexcept
    {
        return SelectArena(heapType).AllocatePersistent(count, type);
    }

    GDXDescriptorAllocation AllocateFrameTransient(GDXDescriptorHeapType heapType,
                                                   GDXDescriptorType type,
                                                   uint32_t count = 1u) noexcept
    {
        return SelectArena(heapType).AllocateFrameTransient(count, type);
    }

    bool FreePersistent(const GDXDescriptorAllocation& allocation) noexcept
    {
        if (!allocation.IsValid())
            return false;
        return SelectArena(allocation.range.heapType).FreePersistent(allocation);
    }

    GDXDescriptorSetAllocation AllocateDescriptorSet(uint32_t resourceCount,
                                                     uint32_t samplerCount,
                                                     uint64_t layoutHash,
                                                     GDXDescriptorLifetime lifetime) noexcept
    {
        GDXDescriptorSetAllocation set{};
        set.layoutHash = layoutHash;

        if (resourceCount > 0u)
        {
            set.resources = (lifetime == GDXDescriptorLifetime::Persistent)
                ? AllocatePersistent(GDXDescriptorHeapType::Resource, GDXDescriptorType::ShaderResource, resourceCount)
                : AllocateFrameTransient(GDXDescriptorHeapType::Resource, GDXDescriptorType::ShaderResource, resourceCount);
        }

        if (samplerCount > 0u)
        {
            set.samplers = (lifetime == GDXDescriptorLifetime::Persistent)
                ? AllocatePersistent(GDXDescriptorHeapType::Sampler, GDXDescriptorType::Sampler, samplerCount)
                : AllocateFrameTransient(GDXDescriptorHeapType::Sampler, GDXDescriptorType::Sampler, samplerCount);
        }

        const bool resourceOkay = (resourceCount == 0u) || set.resources.IsValid();
        const bool samplerOkay  = (samplerCount == 0u) || set.samplers.IsValid();
        if (resourceOkay && samplerOkay)
            return set;

        if (set.resources.IsValid() && set.resources.range.lifetime == GDXDescriptorLifetime::Persistent)
            FreePersistent(set.resources);
        if (set.samplers.IsValid() && set.samplers.range.lifetime == GDXDescriptorLifetime::Persistent)
            FreePersistent(set.samplers);
        return {};
    }

    const GDXDescriptorArena& ResourceArena() const noexcept { return m_resourceArena; }
    const GDXDescriptorArena& SamplerArena() const noexcept { return m_samplerArena; }

private:
    GDXDescriptorArena& SelectArena(GDXDescriptorHeapType heapType) noexcept
    {
        return (heapType == GDXDescriptorHeapType::Sampler) ? m_samplerArena : m_resourceArena;
    }

    const GDXDescriptorArena& SelectArena(GDXDescriptorHeapType heapType) const noexcept
    {
        return (heapType == GDXDescriptorHeapType::Sampler) ? m_samplerArena : m_resourceArena;
    }

    GDXDescriptorAllocatorDesc m_desc{};
    GDXDescriptorArena m_resourceArena{};
    GDXDescriptorArena m_samplerArena{};
};
