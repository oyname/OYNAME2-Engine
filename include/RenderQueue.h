#pragma once
#include "RenderCommand.h"

#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// RenderQueue — sortierbare, flache Liste von RenderCommands.
// ---------------------------------------------------------------------------
struct RenderQueue
{
    std::vector<RenderCommand> commands;

    void Clear() { commands.clear(); }

    void Submit(RenderCommand cmd)
    {
        commands.push_back(std::move(cmd));
    }

    // Convenience-Submit mit ShaderHandle (neu)
    void Submit(MeshHandle mesh, MaterialHandle material, ShaderHandle shader,
                uint32_t submeshIdx, EntityID ownerEntity,
                const DirectX::XMFLOAT4X4& worldMatrix,
                RenderPass pass, uint32_t shaderSortID, uint32_t materialSortID,
                float depth = 0.0f)
    {
        RenderCommand cmd;
        cmd.mesh         = mesh;
        cmd.material     = material;
        cmd.shader       = shader;
        cmd.submeshIndex = submeshIdx;
        cmd.ownerEntity = ownerEntity;
        cmd.pass        = pass;
        cmd.worldMatrix = worldMatrix;
        cmd.SetSortKey(pass, shaderSortID, materialSortID, depth);
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

    size_t Count() const noexcept { return commands.size(); }
    bool   Empty() const noexcept { return commands.empty(); }
};
