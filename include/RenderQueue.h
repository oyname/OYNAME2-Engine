#pragma once
#include "ICommandList.h"
#include "Core/GDXMath.h"
#include "RenderCommand.h"

#include <vector>
#include <algorithm>

struct RenderQueue : public ICommandList
{
    std::vector<RenderCommand> commands;

    void Clear() { commands.clear(); }

    void Submit(RenderCommand cmd)
    {
        commands.push_back(std::move(cmd));
    }

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
        cmd.SetSortKey(pass, shaderSortID, pipelineSortID, materialSortID, depth);
        commands.push_back(cmd);
    }

    void Sort()
    {
        std::sort(commands.begin(), commands.end(),
            [](const RenderCommand& a, const RenderCommand& b)
            {
                return a.sortKey < b.sortKey;
            });
    }


    const std::vector<RenderCommand>& GetCommands() const override { return commands; }
    size_t Count() const noexcept override { return commands.size(); }
    bool   Empty() const noexcept override { return commands.empty(); }
};