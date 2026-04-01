#pragma once

#include <array>
#include <cstdint>

#include "Handle.h"

enum class ResourceState : uint8_t
{
    Unknown = 0,
    Common,
    ShaderRead,
    RenderTarget,
    DepthWrite,
    DepthRead,
    UnorderedAccess,
    CopySource,
    CopyDest,
    Present,
};

enum class GDXResourceUsage : uint32_t
{
    None            = 0u,
    ShaderResource  = 1u << 0u,
    RenderTarget    = 1u << 1u,
    DepthStencil    = 1u << 2u,
    UnorderedAccess = 1u << 3u,
    CopySource      = 1u << 4u,
    CopyDest        = 1u << 5u,
    Present         = 1u << 6u,
};

inline constexpr GDXResourceUsage operator|(GDXResourceUsage a, GDXResourceUsage b) noexcept
{
    return static_cast<GDXResourceUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr GDXResourceUsage operator&(GDXResourceUsage a, GDXResourceUsage b) noexcept
{
    return static_cast<GDXResourceUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr GDXResourceUsage& operator|=(GDXResourceUsage& a, GDXResourceUsage b) noexcept
{
    a = (a | b);
    return a;
}

inline constexpr bool GDXHasUsageFlag(GDXResourceUsage value, GDXResourceUsage flag) noexcept
{
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0u;
}

enum class GDXResourceLifetime : uint8_t
{
    Persistent = 0,
    Transient,
    External,
};

enum class GDXResourceTemporalScope : uint8_t
{
    LongLived = 0,
    PerFrame,
    PerPass,
};

struct GDXResourceUsageDesc
{
    GDXResourceUsage usage = GDXResourceUsage::None;
    GDXResourceLifetime lifetime = GDXResourceLifetime::Persistent;
    GDXResourceTemporalScope temporalScope = GDXResourceTemporalScope::LongLived;
    ResourceState initialState = ResourceState::Unknown;
    ResourceState defaultState = ResourceState::Unknown;
};

enum class GDXPassResourceAccess : uint8_t
{
    None = 0,
    Read,
    Write,
    ReadWrite,
    RenderTarget,
    DepthTarget,
    Present,
};

enum class GDXPassResourceKind : uint8_t
{
    Texture = 0,
    RenderTarget,
    Backbuffer,
    ShadowMap,
};

struct GDXPassResourceUsageEntry
{
    GDXPassResourceKind kind = GDXPassResourceKind::Texture;
    GDXPassResourceAccess access = GDXPassResourceAccess::None;
    TextureHandle texture = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    const wchar_t* debugName = L"";
    ResourceState requiredBefore = ResourceState::Unknown;
    ResourceState stateDuring = ResourceState::Unknown;
    ResourceState stateAfter = ResourceState::Unknown;
};

struct GDXPassResourceUsageSet
{
    static constexpr uint32_t MaxEntries = 8u;

    std::array<GDXPassResourceUsageEntry, MaxEntries> entries{};
    uint32_t count = 0u;

    void Clear() noexcept
    {
        count = 0u;
        for (auto& e : entries)
            e = {};
    }

    bool Add(const GDXPassResourceUsageEntry& entry) noexcept
    {
        if (count >= MaxEntries)
            return false;
        entries[count++] = entry;
        return true;
    }
};
