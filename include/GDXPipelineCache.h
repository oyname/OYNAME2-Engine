#pragma once

#include "Handle.h"
#include "GDXPipelineState.h"
#include "GDXShaderLayout.h"
#include "GDXShaderResource.h"
#include "GDXResourceBinding.h"

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

struct GDXDX11CachedShaderLayout
{
    ShaderHandle shader = ShaderHandle::Invalid();
    GDXShaderLayout layout{};
};

class GDXDX11ShaderLayoutCache
{
public:
    const GDXDX11CachedShaderLayout& GetOrCreate(ShaderHandle shaderHandle, const GDXShaderResource& shader)
    {
        auto it = m_cache.find(shaderHandle.value);
        if (it != m_cache.end())
            return it->second;

        GDXDX11CachedShaderLayout cached{};
        cached.shader = shaderHandle;
        cached.layout = shader.layout;
        auto [inserted, _] = m_cache.emplace(shaderHandle.value, cached);
        return inserted->second;
    }

    void Clear() { m_cache.clear(); }
    size_t Size() const noexcept { return m_cache.size(); }

private:
    std::unordered_map<uint32_t, GDXDX11CachedShaderLayout> m_cache;
};

class GDXDX11BindingCache
{
public:
    bool ShouldApply(ResourceBindingScope scope, uint64_t key) const noexcept
    {
        if (key == 0ull)
            return true;

        return key != GetLastKey(scope);
    }

    void MarkApplied(ResourceBindingScope scope, uint64_t key) noexcept
    {
        switch (scope)
        {
        case ResourceBindingScope::Pass:     m_lastPassKey = key; break;
        case ResourceBindingScope::Material: m_lastMaterialKey = key; break;
        case ResourceBindingScope::Draw:     m_lastDrawKey = key; break;
        default: break;
        }
    }

    uint64_t GetLastKey(ResourceBindingScope scope) const noexcept
    {
        switch (scope)
        {
        case ResourceBindingScope::Pass:     return m_lastPassKey;
        case ResourceBindingScope::Material: return m_lastMaterialKey;
        case ResourceBindingScope::Draw:     return m_lastDrawKey;
        default:                             return 0ull;
        }
    }

    void Invalidate(ResourceBindingScope scope) noexcept
    {
        MarkApplied(scope, 0ull);
    }

    void Reset() noexcept
    {
        m_lastPassKey = 0ull;
        m_lastMaterialKey = 0ull;
        m_lastDrawKey = 0ull;
    }

private:
    uint64_t m_lastPassKey = 0ull;
    uint64_t m_lastMaterialKey = 0ull;
    uint64_t m_lastDrawKey = 0ull;
};
