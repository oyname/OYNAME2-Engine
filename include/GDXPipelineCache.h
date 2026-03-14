#pragma once

#include "Handle.h"
#include "GDXPipelineState.h"

#include <cstdint>
#include <cstddef>
#include <unordered_map>

struct GDXGraphicsPipelineDesc
{
    ShaderHandle shader = ShaderHandle::Invalid();
    GDXPipelineStateDesc state{};
    uint32_t colorFormat = 0u;
    uint32_t depthFormat = 0u;

    uint64_t MakeKey() const noexcept
    {
        return (static_cast<uint64_t>(shader.value) << 32) |
               static_cast<uint64_t>(GDXPipelineStateKey::FromDesc(state).value);
    }
};

struct GDXDX11CachedPipelineState
{
    GDXGraphicsPipelineDesc desc{};
    uint32_t key = 0u;
};

class GDXDX11PipelineCache
{
public:
    const GDXDX11CachedPipelineState& GetOrCreate(const GDXGraphicsPipelineDesc& desc)
    {
        const uint64_t key = desc.MakeKey();
        auto it = m_cache.find(key);
        if (it != m_cache.end())
            return it->second;

        GDXDX11CachedPipelineState cached{};
        cached.desc = desc;
        cached.key = GDXPipelineStateKey::FromDesc(desc.state).value;
        auto [inserted, _] = m_cache.emplace(key, cached);
        return inserted->second;
    }

    void Clear() { m_cache.clear(); }
    size_t Size() const noexcept { return m_cache.size(); }

private:
    std::unordered_map<uint64_t, GDXDX11CachedPipelineState> m_cache;
};
