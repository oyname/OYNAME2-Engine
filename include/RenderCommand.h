#pragma once
#include "Handle.h"
#include "ECSTypes.h"
#include "GDXResourceBinding.h"
#include "GDXPipelineState.h"
#include "MaterialResource.h"

#include <cstdint>
#include "GDXMath.h"

enum class RenderPass : uint8_t
{
    Shadow      = 0,
    Opaque      = 1,
    Transparent = 2,
};

struct RenderCommand
{
    MeshHandle     mesh;
    MaterialHandle material;
    ShaderHandle   shader;
    uint32_t       submeshIndex = 0u;
    EntityID       ownerEntity = NULL_ENTITY;
    RenderPass     pass = RenderPass::Opaque;

    GIDX::Float4x4 worldMatrix = {};
    ResourceBindingSet   resourceBindings{};
    GDXPipelineStateDesc pipelineState{};
    GDXPipelineStateKey  pipelineStateKey{};
    uint64_t             passBindingsKey = 0ull;
    uint64_t             materialBindingsKey = 0ull;
    uint64_t             drawBindingsKey = 0ull;
    MaterialData         materialData{};

    uint64_t sortKey = 0ull;
    bool     receiveShadows = true;

    static uint64_t MakeSortKey(RenderPass pass,
                                 uint32_t shaderSortID,
                                 uint32_t pipelineSortID,
                                 uint32_t materialSortID,
                                 float    depth)
    {
        const uint32_t depthBits = *reinterpret_cast<const uint32_t*>(&depth);
        return  (static_cast<uint64_t>(static_cast<uint8_t>(pass)) << 62)
              | (static_cast<uint64_t>(shaderSortID    & 0x0FFFu) << 50)
              | (static_cast<uint64_t>(pipelineSortID  & 0x00FFu) << 42)
              | (static_cast<uint64_t>(materialSortID  & 0x03FFu) << 32)
              | static_cast<uint64_t>(depthBits);
    }

    void SetSortKey(RenderPass pass,
                    uint32_t shaderSortID,
                    uint32_t pipelineSortID,
                    uint32_t materialSortID,
                    float depth = 0.0f)
    {
        sortKey = MakeSortKey(pass, shaderSortID, pipelineSortID, materialSortID, depth);
    }

    const ResourceBindingSet& GetEffectiveBindings() const noexcept
    {
        return resourceBindings;
    }

    const GDXPipelineStateDesc& GetEffectivePipelineState() const noexcept
    {
        return pipelineState;
    }

    GDXPipelineStateKey GetEffectivePipelineStateKey() const noexcept
    {
        return pipelineStateKey;
    }

    bool HasBindingsForScope(ResourceBindingScope scope) const noexcept
    {
        return resourceBindings.HasBindingsForScope(scope);
    }

    uint64_t GetEffectiveBindingsKeyForScope(ResourceBindingScope scope) const noexcept
    {
        if (!HasBindingsForScope(scope))
            return 0ull;

        switch (scope)
        {
        case ResourceBindingScope::Pass:     return passBindingsKey;
        case ResourceBindingScope::Material: return materialBindingsKey;
        case ResourceBindingScope::Draw:     return drawBindingsKey;
        default:                             return 0ull;
        }
    }

    void SetBindings(const ResourceBindingSet& bindings,
                     uint64_t passKey,
                     uint64_t materialKey,
                     uint64_t drawKey) noexcept
    {
        resourceBindings = bindings;
        passBindingsKey = passKey;
        materialBindingsKey = materialKey;
        drawBindingsKey = drawKey;
    }

    void SetPipelineState(const GDXPipelineStateDesc& state) noexcept
    {
        pipelineState = state;
        pipelineStateKey = GDXPipelineStateKey::FromDesc(state);
    }

    // Rückwärtskompatible Alt-Callsites: laufen jetzt auf denselben Primärpfad.
    void SetLegacyBindings(const ResourceBindingSet& bindings) noexcept
    {
        SetBindings(bindings,
                    BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Pass, shader.value),
                    BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Material, material.value),
                    BuildResourceBindingScopeKey(bindings, ResourceBindingScope::Draw, ownerEntity.value));
    }

    void SetPreparedBindings(const ResourceBindingSet& bindings,
                             uint64_t passKey,
                             uint64_t materialKey,
                             uint64_t drawKey) noexcept
    {
        SetBindings(bindings, passKey, materialKey, drawKey);
    }

    void SetLegacyPipelineState(const GDXPipelineStateDesc& state) noexcept
    {
        SetPipelineState(state);
    }

    void SetPreparedPipelineState(const GDXPipelineStateDesc& state) noexcept
    {
        SetPipelineState(state);
    }
};
