#pragma once

#include "Handle.h"
#include "GDXResourceState.h"
#include "RenderCommand.h"
#include "GDXBindingGroupData.h"

#include <array>
#include <cstdint>
#include <vector>

enum class GDXRecordedResourceKind : uint8_t
{
    Texture = 0,
    RenderTarget = 1,
};

struct GDXRecordedResourceTransition
{
    GDXRecordedResourceKind kind = GDXRecordedResourceKind::Texture;
    TextureHandle texture = TextureHandle::Invalid();
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    ResourceState before = ResourceState::Unknown;
    ResourceState after  = ResourceState::Unknown;

    static GDXRecordedResourceTransition ForTexture(TextureHandle h, ResourceState before, ResourceState after)
    {
        GDXRecordedResourceTransition t{};
        t.kind = GDXRecordedResourceKind::Texture;
        t.texture = h;
        t.before = before;
        t.after = after;
        return t;
    }

    static GDXRecordedResourceTransition ForRenderTarget(RenderTargetHandle h, ResourceState before, ResourceState after)
    {
        GDXRecordedResourceTransition t{};
        t.kind = GDXRecordedResourceKind::RenderTarget;
        t.renderTarget = h;
        t.before = before;
        t.after = after;
        return t;
    }
};


struct GDXRecordedDrawItem
{
    MeshHandle             mesh;
    MaterialHandle         material;
    ShaderHandle           shader;
    uint32_t               submeshIndex = 0u;
    EntityID               ownerEntity = NULL_ENTITY;
    RenderPass             pass = RenderPass::Opaque;
    Matrix4                worldMatrix = {};
    GDXDescriptorSetBuildDesc passBuildDesc{};
    GDXDescriptorSetBuildDesc materialBuildDesc{};
    GDXDescriptorSetBuildDesc drawBuildDesc{};
    GDXRecordedBindingGroupData passBindings{};
    GDXRecordedBindingGroupData materialBindings{};
    GDXRecordedBindingGroupData drawBindings{};
    ResourceBindingSet     resourceBindings{};
    GDXPipelineStateDesc   pipelineState{};
    GDXPipelineStateKey    pipelineStateKey{};
    uint64_t               passBindingsKey = 0ull;
    uint64_t               materialBindingsKey = 0ull;
    uint64_t               drawBindingsKey = 0ull;
    bool                   receiveShadows = true;

    Float3                 worldBoundsCenter = {};
    float                  worldBoundsRadius = 0.0f;
    bool                   hasBounds = false;

    static GDXRecordedDrawItem FromRenderCommand(const RenderCommand& cmd)
    {
        GDXRecordedDrawItem out{};
        out.mesh = cmd.mesh;
        out.material = cmd.material;
        out.shader = cmd.shader;
        out.submeshIndex = cmd.submeshIndex;
        out.ownerEntity = cmd.ownerEntity;
        out.pass = cmd.pass;
        out.worldMatrix = cmd.worldMatrix;
        out.resourceBindings = cmd.resourceBindings;
        out.pipelineState = cmd.pipelineState;
        out.pipelineStateKey = cmd.pipelineStateKey;
        out.passBindingsKey = cmd.passBindingsKey;
        out.materialBindingsKey = cmd.materialBindingsKey;
        out.drawBindingsKey = cmd.drawBindingsKey;
        out.receiveShadows = cmd.receiveShadows;
        out.worldBoundsCenter = cmd.worldBoundsCenter;
        out.worldBoundsRadius = cmd.worldBoundsRadius;
        out.hasBounds = cmd.hasBounds;
        return out;
    }

    bool HasBindingsForScope(ResourceBindingScope scope) const noexcept
    {
        return resourceBindings.HasBindingsForScope(scope);
    }

    uint64_t GetBindingsKeyForScope(ResourceBindingScope scope) const noexcept
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
};

enum class GDXRecordedOpType : uint8_t
{
    Transition = 0,
    SetPipeline,
    BindPassResources,
    BindMaterialResources,
    BindDrawResources,
    DrawMesh,
};

struct GDXRecordedCommand
{
    GDXRecordedOpType type = GDXRecordedOpType::SetPipeline;
    uint32_t drawItemIndex = 0u;
    GDXRecordedResourceTransition transition{};

    static GDXRecordedCommand MakeTransition(const GDXRecordedResourceTransition& t)
    {
        GDXRecordedCommand cmd{};
        cmd.type = GDXRecordedOpType::Transition;
        cmd.transition = t;
        return cmd;
    }

    static GDXRecordedCommand MakeDrawOp(GDXRecordedOpType type, uint32_t drawItemIndex)
    {
        GDXRecordedCommand cmd{};
        cmd.type = type;
        cmd.drawItemIndex = drawItemIndex;
        return cmd;
    }
};

struct GDXRecordedCommandStream
{
    std::vector<GDXRecordedDrawItem> drawItems;
    std::vector<GDXRecordedCommand> commands;
    std::vector<GDXRecordedResourceTransition> preTransitions;
    std::vector<GDXRecordedResourceTransition> postTransitions;

    void Clear()
    {
        drawItems.clear();
        commands.clear();
        preTransitions.clear();
        postTransitions.clear();
    }

    uint32_t AddDrawItem(const GDXRecordedDrawItem& item)
    {
        drawItems.push_back(item);
        return static_cast<uint32_t>(drawItems.size() - 1u);
    }

    void AddTransition(const GDXRecordedResourceTransition& t)
    {
        commands.push_back(GDXRecordedCommand::MakeTransition(t));
    }

    void AddOp(GDXRecordedOpType type, uint32_t drawItemIndex)
    {
        commands.push_back(GDXRecordedCommand::MakeDrawOp(type, drawItemIndex));
    }
};

struct GDXRecordedPassCommands
{
    std::vector<GDXRecordedResourceTransition> preTransitions;
    std::vector<GDXRecordedResourceTransition> postTransitions;

    void Clear()
    {
        preTransitions.clear();
        postTransitions.clear();
    }
};
