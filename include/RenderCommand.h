#pragma once
#include "Handle.h"
#include "ECS/ECSTypes.h"
#include "GDXResourceBinding.h"
#include "GDXPipelineState.h"
#include "RenderPassTypes.h"
#include "RenderSortKey.h"
// Material params are authored data and live in MaterialResource.
// Render commands only carry handles and binding/pipeline keys.

#include <cstdint>
#include "Core/GDXMath.h"

enum class RenderBatchExecutionKind : uint8_t
{
    SingleDraw = 0,
    SharedStateDrawSequence,
    Instanced,
};

struct RenderBatchRange
{
    uint32_t firstCommand = 0u;
    uint32_t commandCount = 0u;
    uint32_t representativeCommand = 0u;
    RenderBatchExecutionKind executionKind = RenderBatchExecutionKind::SingleDraw;

    RenderPass            pass = RenderPass::Opaque;
    MeshHandle            mesh;
    MaterialHandle        material;
    ShaderHandle          shader;
    uint32_t              submeshIndex = 0u;
    GDXPipelineStateKey   pipelineStateKey{};
    uint64_t              passBindingsKey = 0ull;
    uint64_t              materialBindingsKey = 0ull;
    uint64_t              instancingKey = 0ull;

    bool IsValid() const noexcept { return commandCount != 0u; }
    bool IsInstanced() const noexcept { return executionKind == RenderBatchExecutionKind::Instanced; }
};


struct RenderCommand
{
    MeshHandle     mesh;
    MaterialHandle material;
    ShaderHandle   shader;
    uint32_t       submeshIndex = 0u;
    EntityID       ownerEntity = NULL_ENTITY;
    RenderPass     pass = RenderPass::Opaque;

    Matrix4 worldMatrix = {};
    ResourceBindingSet   resourceBindings{};
    GDXPipelineStateDesc pipelineState{};
    GDXPipelineStateKey  pipelineStateKey{};
    uint64_t             passBindingsKey = 0ull;
    uint64_t             materialBindingsKey = 0ull;
    uint64_t             drawBindingsKey = 0ull;
    uint64_t sortKey = 0ull;
    uint8_t  renderPriority = 128u;
    bool     receiveShadows = true;
    bool     instancingEligible = false;
    uint64_t instancingKey = 0ull;

    // Weltraum-Bounds — von VisibleRenderCandidate übertragen.
    // Werden im Backend für per-Kaskaden-Culling genutzt.
    // hasBounds=false → Objekt wird immer gezeichnet (konservativ).
    Float3   worldBoundsCenter = {};
    float    worldBoundsRadius = 0.0f;
    bool     hasBounds         = false;

    static uint64_t MakeSortKey(const RenderSortKeyParams& params)
    {
        return RenderSortKey::BuildPackedKey(params);
    }

    void SetSortKey(const RenderSortKeyParams& params)
    {
        sortKey = MakeSortKey(params);
    }

    // Legacy-Fallback fuer aeltere Call-Sites. Neue Queue-/Gather-Logik soll
    // stattdessen RenderSortKeyParams explizit befuellen und SetSortKey(params)
    // verwenden.
    void SetSortKey(RenderPass pass,
                    uint32_t shaderSortID,
                    uint32_t pipelineSortID,
                    uint32_t materialSortID,
                    float depth = 0.0f)
    {
        sortKey = RenderSortKey::BuildLegacyFallbackKeyFromPass(
            pass, renderPriority, shaderSortID, pipelineSortID, materialSortID, depth);
    }

    const ResourceBindingSet& GetEffectiveBindings() const noexcept
    {
        return resourceBindings;
    }

    const GDXPipelineStateDesc& GetEffectivePipelineState() const noexcept
    {
        return pipelineState;
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
};
