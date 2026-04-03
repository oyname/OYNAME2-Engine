#pragma once

#include "Handle.h"
#include "RenderPassClearDesc.h"
#include "GDXResourceBinding.h"

#include <cstdint>
#include <string>
#include <vector>

struct GDXPassViewportDesc
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct GDXPassScissorDesc
{
    bool enabled = false;
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
};

enum class GDXPassTargetKind : uint8_t
{
    Backbuffer = 0,
    RenderTarget = 1,
    ShadowMapCascade = 2,
};

struct GDXPassBeginDesc
{
    GDXPassTargetKind targetKind = GDXPassTargetKind::Backbuffer;
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    uint32_t shadowCascade = 0u;
    bool bindNormalsTarget = false;
    bool bindMotionVectorsTarget = false;
    bool bindDepthOnlyTarget = false;
    RenderPassClearDesc clear{};
    GDXPassViewportDesc viewport{};
    GDXPassScissorDesc scissor{};
    std::wstring debugName;
};

enum class GDXPassCommandType : uint8_t
{
    BeginPass = 0,
    EndPass,
    SetViewport,
    SetScissor,
    ClearColor,
    ClearDepth,
    BindPipeline,
    BindBindingGroup,
    BindVertexBuffer,
    BindIndexBuffer,
    Draw,
    DrawIndexed,
};

struct GDXPassCommand
{
    GDXPassCommandType type = GDXPassCommandType::BeginPass;
    GDXPassBeginDesc beginPass{};
    GDXPassViewportDesc viewport{};
    GDXPassScissorDesc scissor{};
    RenderTargetHandle renderTarget = RenderTargetHandle::Invalid();
    ShaderHandle shader = ShaderHandle::Invalid();
    ResourceBindingScope bindingScope = ResourceBindingScope::Pass;
    uint32_t drawItemIndex = 0u;
    MeshHandle mesh = MeshHandle::Invalid();
    uint32_t submeshIndex = 0u;
    uint32_t vertexFlags = 0u;
    uint32_t vertexCount = 0u;
    uint32_t vertexStart = 0u;
    uint32_t indexCount = 0u;
    uint32_t startIndex = 0u;
    int32_t baseVertex = 0;

    static GDXPassCommand MakeBeginPass(const GDXPassBeginDesc& desc)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::BeginPass;
        cmd.beginPass = desc;
        return cmd;
    }

    static GDXPassCommand MakeEndPass()
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::EndPass;
        return cmd;
    }
};

class GDXPassCommandList
{
public:
    void Reset()
    {
        m_commands.clear();
    }

    void BeginPass(const GDXPassBeginDesc& desc)
    {
        m_commands.push_back(GDXPassCommand::MakeBeginPass(desc));
    }

    void EndPass()
    {
        m_commands.push_back(GDXPassCommand::MakeEndPass());
    }

    void SetViewport(const GDXPassViewportDesc& viewport)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::SetViewport;
        cmd.viewport = viewport;
        m_commands.push_back(cmd);
    }

    void SetScissor(const GDXPassScissorDesc& scissor)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::SetScissor;
        cmd.scissor = scissor;
        m_commands.push_back(cmd);
    }

    void ClearColor(RenderTargetHandle renderTarget = RenderTargetHandle::Invalid())
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::ClearColor;
        cmd.renderTarget = renderTarget;
        m_commands.push_back(cmd);
    }

    void ClearDepth(RenderTargetHandle renderTarget = RenderTargetHandle::Invalid())
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::ClearDepth;
        cmd.renderTarget = renderTarget;
        m_commands.push_back(cmd);
    }

    void BindPipeline(ShaderHandle shader)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::BindPipeline;
        cmd.shader = shader;
        m_commands.push_back(cmd);
    }

    void BindBindingGroup(ResourceBindingScope scope, uint32_t drawItemIndex = 0u)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::BindBindingGroup;
        cmd.bindingScope = scope;
        cmd.drawItemIndex = drawItemIndex;
        m_commands.push_back(cmd);
    }

    void BindVertexBuffer(MeshHandle mesh, uint32_t submeshIndex, uint32_t vertexFlags)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::BindVertexBuffer;
        cmd.mesh = mesh;
        cmd.submeshIndex = submeshIndex;
        cmd.vertexFlags = vertexFlags;
        m_commands.push_back(cmd);
    }

    void BindIndexBuffer(MeshHandle mesh, uint32_t submeshIndex)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::BindIndexBuffer;
        cmd.mesh = mesh;
        cmd.submeshIndex = submeshIndex;
        m_commands.push_back(cmd);
    }

    void Draw(uint32_t vertexCount, uint32_t vertexStart = 0u)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::Draw;
        cmd.vertexCount = vertexCount;
        cmd.vertexStart = vertexStart;
        m_commands.push_back(cmd);
    }

    void DrawIndexed(uint32_t indexCount = 0u, uint32_t startIndex = 0u, int32_t baseVertex = 0)
    {
        GDXPassCommand cmd{};
        cmd.type = GDXPassCommandType::DrawIndexed;
        cmd.indexCount = indexCount;
        cmd.startIndex = startIndex;
        cmd.baseVertex = baseVertex;
        m_commands.push_back(cmd);
    }

    const std::vector<GDXPassCommand>& GetCommands() const noexcept
    {
        return m_commands;
    }

    bool Empty() const noexcept
    {
        return m_commands.empty();
    }

private:
    std::vector<GDXPassCommand> m_commands;
};
