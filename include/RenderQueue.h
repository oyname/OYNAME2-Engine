#pragma once
#include "ICommandList.h"
#include "Core/GDXMath.h"
#include "RenderCommand.h"

#include <vector>
#include <algorithm>
#include <cstddef>
#include <utility>

struct RenderQueue : public ICommandList
{
    std::vector<RenderCommand> commands;
    mutable std::vector<RenderBatchRange> batchRanges;
    mutable bool batchRangesDirty = true;

    void Clear() { commands.clear(); batchRanges.clear(); batchRangesDirty = false; }

    void Submit(RenderCommand cmd)
    {
        commands.push_back(std::move(cmd));
        batchRangesDirty = true;
    }

    // Legacy-Convenience-Pfad. Neue Gather-/Queue-Pfade sollen RenderCommand
    // direkt mit explizitem Packed Sort Key erzeugen und Submit(RenderCommand)
    // nutzen.
    void Submit(MeshHandle mesh, MaterialHandle material, ShaderHandle shader,
        uint32_t submeshIdx, EntityID ownerEntity,
        const Matrix4& worldMatrix,
        RenderPass pass, uint32_t shaderSortID, uint32_t pipelineSortID, uint32_t materialSortID,
        float depth = 0.0f,
        bool receiveShadows = true,
        const ResourceBindingSet* resourceBindings = nullptr,
        const GDXPipelineStateDesc* pipelineState = nullptr)
    {
        RenderCommand cmd;
        cmd.mesh = mesh;
        cmd.material = material;
        cmd.shader = shader;
        cmd.submeshIndex = submeshIdx;
        cmd.ownerEntity = ownerEntity;
        cmd.pass = pass;
        cmd.worldMatrix = worldMatrix;
        if (resourceBindings)
            cmd.SetBindings(*resourceBindings,
                BuildResourceBindingScopeKey(*resourceBindings, ResourceBindingScope::Pass, shader.value),
                BuildResourceBindingScopeKey(*resourceBindings, ResourceBindingScope::Material, material.value),
                BuildResourceBindingScopeKey(*resourceBindings, ResourceBindingScope::Draw, ownerEntity.value));
        if (pipelineState)
            cmd.SetPipelineState(*pipelineState);
        else
            cmd.SetPipelineState({});
        cmd.receiveShadows = receiveShadows;
        cmd.renderPriority = 128u;
        cmd.SetSortKey(pass, shaderSortID, pipelineSortID, materialSortID, depth);
        commands.push_back(cmd);
        batchRangesDirty = true;
    }

    static bool CanShareBatchState(const RenderCommand& a, const RenderCommand& b) noexcept
    {
        return a.pass == b.pass
            && a.mesh == b.mesh
            && a.material == b.material
            && a.shader == b.shader
            && a.submeshIndex == b.submeshIndex
            && a.pipelineStateKey.value == b.pipelineStateKey.value
            && a.passBindingsKey == b.passBindingsKey
            && a.materialBindingsKey == b.materialBindingsKey;
    }

    static bool CanInstanceTogether(const RenderCommand& a, const RenderCommand& b) noexcept
    {
        return CanShareBatchState(a, b)
            && a.instancingEligible
            && b.instancingEligible
            && a.instancingKey != 0ull
            && a.instancingKey == b.instancingKey;
    }

    void RebuildBatchRanges() const
    {
        batchRanges.clear();
        if (commands.empty())
        {
            batchRangesDirty = false;
            return;
        }

        size_t i = 0u;
        while (i < commands.size())
        {
            const size_t first = i;
            const RenderCommand& base = commands[first];

            size_t end = first + 1u;
            while (end < commands.size() && CanShareBatchState(base, commands[end]))
                ++end;

            RenderBatchRange batch{};
            batch.firstCommand = static_cast<uint32_t>(first);
            batch.commandCount = static_cast<uint32_t>(end - first);
            batch.representativeCommand = static_cast<uint32_t>(first);
            batch.pass = base.pass;
            batch.mesh = base.mesh;
            batch.material = base.material;
            batch.shader = base.shader;
            batch.submeshIndex = base.submeshIndex;
            batch.pipelineStateKey = base.pipelineStateKey;
            batch.passBindingsKey = base.passBindingsKey;
            batch.materialBindingsKey = base.materialBindingsKey;
            batch.instancingKey = base.instancingKey;

            if (batch.commandCount == 1u)
            {
                batch.executionKind = RenderBatchExecutionKind::SingleDraw;
            }
            else
            {
                bool instanced = base.instancingEligible && base.instancingKey != 0ull;
                for (size_t j = first + 1u; instanced && j < end; ++j)
                    instanced = CanInstanceTogether(base, commands[j]);
                batch.executionKind = instanced ? RenderBatchExecutionKind::Instanced
                                                : RenderBatchExecutionKind::SharedStateDrawSequence;
                if (!instanced)
                    batch.instancingKey = 0ull;
            }

            batchRanges.push_back(batch);
            i = end;
        }

        batchRangesDirty = false;
    }

    void Sort()
    {
        std::stable_sort(commands.begin(), commands.end(),
            [](const RenderCommand& a, const RenderCommand& b)
            {
                return a.sortKey < b.sortKey;
            });
        batchRangesDirty = true;
        RebuildBatchRanges();
    }

    void DebugDump(const char* queueName, size_t maxCommands = 16u) const;

    const std::vector<RenderCommand>& GetCommands() const override { return commands; }
    const std::vector<RenderBatchRange>& GetBatchRanges() const override { if (batchRangesDirty) RebuildBatchRanges(); return batchRanges; }
    size_t Count() const noexcept override { return commands.size(); }
    bool   Empty() const noexcept override { return commands.empty(); }
};