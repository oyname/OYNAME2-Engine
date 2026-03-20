#include "GDXDX11RenderExecutor.h"
#include "GDXVertexFlags.h"
#include "SubmeshData.h"
#include "Debug.h"
#include "GDXTextureSlots.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include "GDXMath.h"
#include "GDXMathHelpers.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------
static ID3D11Buffer* CreateBuffer(ID3D11Device* device,
    const void* data, uint32_t bytes, D3D11_BIND_FLAG bind, bool dynamic = false)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = bytes;
    desc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0u;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = data;

    ID3D11Buffer* buf = nullptr;
    if (FAILED(device->CreateBuffer(&desc, data ? &init : nullptr, &buf)))
        return nullptr;
    return buf;
}

static const char* ResourceStateName(ResourceState s)
{
    switch (s)
    {
    case ResourceState::Unknown:      return "Unknown";
    case ResourceState::ShaderRead:   return "ShaderRead";
    case ResourceState::RenderTarget: return "RenderTarget";
    case ResourceState::DepthWrite:   return "DepthWrite";
    case ResourceState::CopySource:   return "CopySource";
    case ResourceState::CopyDest:     return "CopyDest";
    case ResourceState::Present:      return "Present";
    default:                          return "?";
    }
}

// ===========================================================================
// GDXDX11MeshUploader
// ===========================================================================
bool GDXDX11MeshUploader::Upload(MeshAssetResource& mesh)
{
    for (uint32_t i = 0; i < mesh.SubmeshCount(); ++i)
        if (!UploadSubmesh(mesh.submeshes[i], mesh.gpuBuffers[i])) return false;
    mesh.gpuReleaseCallback = &GDXDX11MeshUploader::Release;
    mesh.gpuReady = true;
    return true;
}

bool GDXDX11MeshUploader::UploadSubmesh(SubmeshData& cpu, GpuMeshBuffer& gpu)
{
    if (cpu.VertexCount() == 0) return false;
    gpu.vertexCount = cpu.VertexCount();

    auto upload = [&](const void* data, uint32_t stride, uint32_t count,
        void*& outBuf, uint32_t& outStride) -> bool
        {
            auto* buf = CreateBuffer(m_device, data, stride * count, D3D11_BIND_VERTEX_BUFFER);
            if (!buf) return false;
            outBuf = buf;
            outStride = stride;
            return true;
        };

    if (!upload(cpu.positions.data(), sizeof(Float3),
        cpu.VertexCount(), gpu.positionBuffer, gpu.stridePosition))
        return false;

    if (cpu.HasNormals())
        upload(cpu.normals.data(), sizeof(Float3),
            cpu.VertexCount(), gpu.normalBuffer, gpu.strideNormal);

    if (!cpu.colors.empty() && cpu.colors.size() == cpu.positions.size())
        upload(cpu.colors.data(), sizeof(Float4),
            cpu.VertexCount(), gpu.colorBuffer, gpu.strideColor);

    if (cpu.HasUV0())
        upload(cpu.uv0.data(), sizeof(Float2),
            cpu.VertexCount(), gpu.uv1Buffer, gpu.strideUV1);

    if (cpu.HasUV1())
        upload(cpu.uv1.data(), sizeof(Float2),
            cpu.VertexCount(), gpu.uv2Buffer, gpu.strideUV2);

    if (cpu.HasTangents())
        upload(cpu.tangents.data(), sizeof(Float4),
            cpu.VertexCount(), gpu.tangentBuffer, gpu.strideTangent);

    if (cpu.HasSkinning())
    {
        upload(cpu.boneIndices.data(), sizeof(UInt4),
            cpu.VertexCount(), gpu.boneIndexBuffer, gpu.strideBoneIndex);
        upload(cpu.boneWeights.data(), sizeof(Float4),
            cpu.VertexCount(), gpu.boneWeightBuffer, gpu.strideBoneWeight);
    }

    if (!cpu.indices.empty())
    {
        auto* ib = CreateBuffer(m_device, cpu.indices.data(),
            static_cast<uint32_t>(cpu.indices.size() * sizeof(uint32_t)),
            D3D11_BIND_INDEX_BUFFER);
        if (!ib) return false;
        gpu.indexBuffer = ib;
        gpu.indexCount = static_cast<uint32_t>(cpu.indices.size());
    }

    gpu.ready = true;
    return true;
}

void GDXDX11MeshUploader::Release(MeshAssetResource& mesh)
{
    for (auto& gpu : mesh.gpuBuffers)
    {
        auto sr = [](void*& p) { if (p) { static_cast<ID3D11Buffer*>(p)->Release(); p = nullptr; } };
        sr(gpu.positionBuffer);  sr(gpu.normalBuffer);    sr(gpu.colorBuffer);
        sr(gpu.uv1Buffer);       sr(gpu.uv2Buffer);       sr(gpu.tangentBuffer);
        sr(gpu.boneIndexBuffer); sr(gpu.boneWeightBuffer); sr(gpu.indexBuffer);
        gpu = GpuMeshBuffer{};
    }
}

// ===========================================================================
// GDXDX11RenderExecutor
// ===========================================================================
bool GDXDX11RenderExecutor::Init(const InitParams& p)
{
    m_device = p.device;
    m_context = p.context;
    if (!m_device || !m_context) return false;
    CreateConstantBuffers();
    m_textureStates.clear();
    ResetScopeCaches();
    m_pipelineCache.Clear();
    m_layoutCache.Clear();
    m_lastAppliedPipelineKey = 0u;
    return m_entityCB && m_frameCB;
}

void GDXDX11RenderExecutor::CreateConstantBuffers()
{
    m_entityCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11EntityConstants), D3D11_BIND_CONSTANT_BUFFER, true);
    m_frameCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11FrameConstants), D3D11_BIND_CONSTANT_BUFFER, true);
    m_skinCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11SkinConstants), D3D11_BIND_CONSTANT_BUFFER, true);
}

void GDXDX11RenderExecutor::Shutdown()
{
    if (m_entityCB) { m_entityCB->Release(); m_entityCB = nullptr; }
    if (m_frameCB) { m_frameCB->Release();  m_frameCB = nullptr; }
    if (m_skinCB) { m_skinCB->Release();   m_skinCB = nullptr; }
    m_textureStates.clear();
    ResetScopeCaches();
    m_pipelineCache.Clear();
    m_layoutCache.Clear();
    m_lastAppliedPipelineKey = 0u;
}

void GDXDX11RenderExecutor::UpdateFrameConstants(const FrameData& frame)
{
    if (!m_frameCB) return;

    Dx11FrameConstants fc = {};
    std::memcpy(fc.viewMatrix, &frame.viewMatrix, 64);
    std::memcpy(fc.projMatrix, &frame.projMatrix, 64);
    std::memcpy(fc.viewProjMatrix, &frame.viewProjMatrix, 64);
    std::memcpy(fc.shadowViewProj, &frame.shadowViewProjMatrix, 64);
    fc.cameraPos[0] = frame.cameraPos.x;
    fc.cameraPos[1] = frame.cameraPos.y;
    fc.cameraPos[2] = frame.cameraPos.z;
    fc.cameraPos[3] = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_frameCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &fc, sizeof(fc));
        m_context->Unmap(m_frameCB, 0);
    }
}

void GDXDX11RenderExecutor::BindFrameConstantsForShader(const GDXShaderResource& shader)
{
    if (!m_frameCB)
        return;

    for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
    {
        const auto& cb = shader.layout.constantBuffers[i];
        if (cb.slot != GDXShaderConstantBufferSlot::Frame)
            continue;

        ID3D11Buffer* buffer = m_frameCB;
        if (cb.vsRegister != 255u)
            m_context->VSSetConstantBuffers(cb.vsRegister, 1, &buffer);
        if (cb.psRegister != 255u)
            m_context->PSSetConstantBuffers(cb.psRegister, 1, &buffer);
        return;
    }
}

void GDXDX11RenderExecutor::BindEntityConstantsForShader(const GDXShaderResource& shader)
{
    if (!m_entityCB)
        return;

    for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
    {
        const auto& cb = shader.layout.constantBuffers[i];
        if (cb.slot != GDXShaderConstantBufferSlot::Entity)
            continue;

        ID3D11Buffer* buffer = m_entityCB;
        if (cb.vsRegister != 255u)
            m_context->VSSetConstantBuffers(cb.vsRegister, 1, &buffer);
        if (cb.psRegister != 255u)
            m_context->PSSetConstantBuffers(cb.psRegister, 1, &buffer);
        return;
    }
}

// ---------------------------------------------------------------------------
// BindVertexStreams — flag-gesteuert (wie OYNAME SurfaceGpuBuffer::Draw)
// ---------------------------------------------------------------------------
bool GDXDX11RenderExecutor::BindVertexStreams(const GpuMeshBuffer& gpu, uint32_t flags)
{
    ID3D11Buffer* buffers[8] = {};
    UINT          strides[8] = {};
    UINT          offsets[8] = {};
    UINT          slot = 0u;

    auto bind = [&](bool needed, void* buf, uint32_t stride) -> bool
        {
            if (!needed) return true;
            if (!buf || stride == 0) return false;
            buffers[slot] = static_cast<ID3D11Buffer*>(buf);
            strides[slot] = stride;
            offsets[slot] = 0u;
            ++slot;
            return true;
        };

    if (!bind(flags & GDX_VERTEX_POSITION, gpu.positionBuffer, gpu.stridePosition))   return false;
    if (!bind(flags & GDX_VERTEX_NORMAL, gpu.normalBuffer, gpu.strideNormal))     return false;
    if (!bind(flags & GDX_VERTEX_COLOR, gpu.colorBuffer, gpu.strideColor))      return false;
    if (!bind(flags & GDX_VERTEX_TEX1, gpu.uv1Buffer, gpu.strideUV1)) return false;
    if (flags & GDX_VERTEX_TEX2)
    {
        void* uv1Buf = gpu.uv2Buffer ? gpu.uv2Buffer : gpu.uv1Buffer;
        uint32_t uv1Str = gpu.uv2Buffer ? gpu.strideUV2 : gpu.strideUV1;
        if (!bind(true, uv1Buf, uv1Str)) return false;
    }
    if (!bind(flags & GDX_VERTEX_TANGENT, gpu.tangentBuffer, gpu.strideTangent))    return false;
    if (!bind(flags & GDX_VERTEX_BONE_INDICES, gpu.boneIndexBuffer, gpu.strideBoneIndex))  return false;
    if (!bind(flags & GDX_VERTEX_BONE_WEIGHTS, gpu.boneWeightBuffer, gpu.strideBoneWeight)) return false;

    if (slot > 0u)
        m_context->IASetVertexBuffers(0, slot, buffers, strides, offsets);

    return true;
}

// ---------------------------------------------------------------------------
// BindTexturesForScope — setzt vorbereitete Textur-Bindings nach Scope.
// Pass/Material werden nur bei Scope-Wechsel erneut gesetzt, Draw pro Draw.
// ShadowMap wird hier nicht gesetzt, weil sie vom Pass-/Shaderpfad kommt.
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::BindTexturesForScope(
    const ResourceBindingSet& bindings,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    TextureHandle defaultWhite,
    TextureHandle defaultNormal,
    TextureHandle defaultORM,
    TextureHandle defaultBlack,
    ResourceBindingScope scope)
{
    for (uint32_t i = 0; i < bindings.textureCount; ++i)
    {
        const auto& binding = bindings.textures[i];
        if (binding.scope != scope)
            continue;

        TextureHandle texHandle = TextureHandle::Invalid();
        switch (binding.semantic)
        {
        case ShaderResourceSemantic::Albedo:
            texHandle = (binding.enabled && binding.texture.IsValid()) ? binding.texture : defaultWhite;
            break;
        case ShaderResourceSemantic::Normal:
            texHandle = (binding.enabled && binding.texture.IsValid()) ? binding.texture : defaultNormal;
            break;
        case ShaderResourceSemantic::ORM:
            texHandle = (binding.enabled && binding.texture.IsValid()) ? binding.texture : defaultORM;
            break;
        case ShaderResourceSemantic::Emissive:
            texHandle = (binding.enabled && binding.texture.IsValid()) ? binding.texture : defaultBlack;
            break;
        case ShaderResourceSemantic::Detail:
            texHandle = (binding.enabled && binding.texture.IsValid()) ? binding.texture : defaultWhite;
            break;
        case ShaderResourceSemantic::ShadowMap:
            continue;
        default:
            continue;
        }

        if (texHandle.IsValid())
            ValidateShaderReadState(texHandle, "BindTexturesForScope layout");

        ID3D11ShaderResourceView* srv = nullptr;
        if (const GDXTextureResource* tex = texStore.Get(texHandle))
            srv = static_cast<ID3D11ShaderResourceView*>(tex->srv);

        const UINT slot = binding.bindingIndex;
        m_context->PSSetShaderResources(slot, 1, &srv);
    }
}

void GDXDX11RenderExecutor::BindConstantBuffersForScope(
    const ResourceBindingSet& bindings,
    const RenderCommand& cmd,
    ResourceBindingScope scope,
    bool applyReceiveShadowOverride)
{
    for (uint32_t i = 0; i < bindings.constantBufferCount; ++i)
    {
        const auto& binding = bindings.constantBuffers[i];
        if (!binding.enabled || !binding.buffer || binding.scope != scope)
            continue;

        ID3D11Buffer* cb = static_cast<ID3D11Buffer*>(binding.buffer);

        if (binding.semantic == GDXShaderConstantBufferSlot::Material)
        {
            MaterialData drawData = cmd.materialData;
            if (applyReceiveShadowOverride)
            {
                const bool materialReceive = (drawData.receiveShadows > 0.5f);
                drawData.receiveShadows = (materialReceive && cmd.receiveShadows) ? 1.0f : 0.0f;
            }

            D3D11_MAPPED_SUBRESOURCE m = {};
            if (SUCCEEDED(m_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
            {
                std::memcpy(m.pData, &drawData, sizeof(MaterialData));
                m_context->Unmap(cb, 0);
            }
        }

        if (binding.vsRegister != 255u)
            m_context->VSSetConstantBuffers(binding.vsRegister, 1, &cb);
        if (binding.psRegister != 255u)
            m_context->PSSetConstantBuffers(binding.psRegister, 1, &cb);
    }
}



void GDXDX11RenderExecutor::ApplyScopedBindings(
    const RenderCommand& cmd,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    bool applyReceiveShadowOverride)
{
    const ResourceBindingSet& bindings = cmd.GetEffectiveBindings();

    auto applyScope = [&](ResourceBindingScope scope, bool receiveShadowOverride)
    {
        if (!cmd.HasBindingsForScope(scope))
        {
            m_bindingCache.Invalidate(scope);
            return;
        }

        const uint64_t scopeKey = cmd.GetEffectiveBindingsKeyForScope(scope);
        if (!m_bindingCache.ShouldApply(scope, scopeKey))
            return;

        BindTexturesForScope(bindings, texStore, defaultWhiteTex, defaultNormalTex, defaultORMTex, defaultBlackTex, scope);
        BindConstantBuffersForScope(bindings, cmd, scope, receiveShadowOverride);
        m_bindingCache.MarkApplied(scope, scopeKey);
    };

    applyScope(ResourceBindingScope::Pass, false);
    applyScope(ResourceBindingScope::Material, false);
    applyScope(ResourceBindingScope::Draw, applyReceiveShadowOverride);
}

void GDXDX11RenderExecutor::ResetScopeCaches()
{
    m_bindingCache.Reset();
}

const GDXShaderLayout& GDXDX11RenderExecutor::GetCachedShaderLayout(ShaderHandle shaderHandle, const GDXShaderResource& shader)
{
    return m_layoutCache.GetOrCreate(shaderHandle, shader).layout;
}

// ---------------------------------------------------------------------------
// BindSkinningPalette — VS b4, identity fallback wenn keine Skin-Daten vorhanden.
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::BindSkinningPalette(
    Registry& registry,
    const RenderCommand& cmd,
    const GDXShaderResource& shader)
{
    if (!shader.supportsSkinning || !m_skinCB)
        return;

    Dx11SkinConstants sc = {};
    for (uint32_t i = 0; i < SkinComponent::MaxBones; ++i)
    {
        Float4x4 ident = GIDX::Identity4x4();
        std::memcpy(sc.boneMatrices[i], &ident, 64);
    }

    if (cmd.ownerEntity != NULL_ENTITY)
    {
        auto* skin = registry.Get<SkinComponent>(cmd.ownerEntity);
        if (skin && skin->enabled)
        {
            const uint32_t count = static_cast<uint32_t>(
                skin->finalBoneMatrices.size() < SkinComponent::MaxBones
                ? skin->finalBoneMatrices.size()
                : SkinComponent::MaxBones);

            for (uint32_t i = 0; i < count; ++i)
                std::memcpy(sc.boneMatrices[i], &skin->finalBoneMatrices[i], 64);
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_skinCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &sc, sizeof(sc));
        m_context->Unmap(m_skinCB, 0);
    }

    const ResourceBindingSet& bindings = cmd.GetEffectiveBindings();
    if (const ConstantBufferBindingDesc* binding = bindings.FindConstantBufferBinding(GDXShaderConstantBufferSlot::Skin))
    {
        if (binding->enabled && binding->vsRegister != 255u)
            m_context->VSSetConstantBuffers(binding->vsRegister, 1, &m_skinCB);
    }
}

void GDXDX11RenderExecutor::ApplyPipelineState(const RenderCommand& cmd)
{
    GDXGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.shader = cmd.shader;
    pipelineDesc.state = cmd.GetEffectivePipelineState();

    const auto& cached = m_pipelineCache.GetOrCreate(pipelineDesc);
    if (cached.stateKey.value == m_lastAppliedPipelineKey)
        return;

    const GDXPipelineStateDesc& state = cached.desc.state;

    ID3D11RasterizerState* rs = (state.cullMode == GDXCullMode::None) ? m_rsNoCull : m_rsCull;
    if (rs)
        m_context->RSSetState(rs);

    ID3D11DepthStencilState* ds = (state.depthMode == GDXDepthMode::ReadOnly) ? m_dsReadOnly : m_dsReadWrite;
    if (!state.depthTestEnabled)
        ds = m_dsReadOnly;
    if (ds)
        m_context->OMSetDepthStencilState(ds, 0u);

    const float bf[4] = { 0,0,0,0 };
    ID3D11BlendState* blend = (state.blendMode == GDXBlendMode::AlphaBlend) ? m_blendAlpha : m_blendOpaque;
    if (blend)
        m_context->OMSetBlendState(blend, bf, 0xFFFFFFFFu);

    m_lastAppliedPipelineKey = cached.stateKey.value;
}

void GDXDX11RenderExecutor::ApplyPrimitiveTopology(const RenderCommand& cmd)
{
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    switch (cmd.GetEffectivePipelineState().topology)
    {
    case GDXPrimitiveTopology::TriangleList:
    default:
        topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
    }

    m_context->IASetPrimitiveTopology(topology);
}

ResourceState GDXDX11RenderExecutor::GetTrackedTextureState(TextureHandle texture) const
{
    const auto it = m_textureStates.find(texture);
    return (it != m_textureStates.end()) ? it->second : ResourceState::Unknown;
}

void GDXDX11RenderExecutor::SetTrackedTextureState(TextureHandle texture, ResourceState state)
{
    if (!texture.IsValid())
        return;
    m_textureStates[texture] = state;
}

void GDXDX11RenderExecutor::ValidateShaderReadState(TextureHandle texture, const char* debugReason)
{
    if (!texture.IsValid())
        return;

    const ResourceState current = GetTrackedTextureState(texture);
    if (current != ResourceState::Unknown && current != ResourceState::ShaderRead)
    {
        DBWARN(GDX_SRC_LOC, "Texture state mismatch before shader read (", debugReason ? debugReason : "", "): expected ShaderRead, tracked=", ResourceStateName(current));
    }

    // Validierung darf den getrackten State nicht heimlich umbiegen.
}

void GDXDX11RenderExecutor::TransitionTexture(TextureHandle texture, ResourceState expectedBefore, ResourceState after, const char* debugReason)
{
    if (!texture.IsValid())
        return;

    const ResourceState current = GetTrackedTextureState(texture);
    if (current != ResourceState::Unknown && expectedBefore != ResourceState::Unknown && current != expectedBefore)
    {
        DBWARN(GDX_SRC_LOC, "Texture transition mismatch (", debugReason ? debugReason : "", "): tracked=", ResourceStateName(current), ", expectedBefore=", ResourceStateName(expectedBefore), ", after=", ResourceStateName(after));
    }

    if (current == after)
    {
        DBWARN(GDX_SRC_LOC, "Redundant texture transition (", debugReason ? debugReason : "", "): state=", ResourceStateName(after));
    }

    SetTrackedTextureState(texture, after);
}

void GDXDX11RenderExecutor::ResetTrackedResourceStates()
{
    m_textureStates.clear();
}

// ---------------------------------------------------------------------------
// ExecuteShadowQueue — Depth-Only Shadow Pass.
// Kein Material-Check, kein Textur-Bind, nur World-Matrix + Position-Stream.
// Löst den Kernfehler: ExecuteQueue() übersprang alle Shadow-Draws weil
// cmd.material == Invalid() → mat == nullptr → continue.
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::ExecuteShadowQueue(
    Registry& registry,
    const ICommandList& queue,
    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore)
{
    m_drawCalls = 0u;
    m_lastShader = ShaderHandle::Invalid();
    m_lastAppliedPipelineKey = 0u;
    ResetScopeCaches();

    const auto& commands = queue.GetCommands();
    for (const RenderCommand& cmd : commands)
    {
        MeshAssetResource* mesh = meshStore.Get(cmd.mesh);
        GDXShaderResource* shader = shaderStore.Get(cmd.shader);

        if (!mesh || !shader || !shader->IsValid())      continue;
        if (cmd.submeshIndex >= mesh->gpuBuffers.size())         continue;

        const GpuMeshBuffer& gpu = mesh->gpuBuffers[cmd.submeshIndex];
        if (!gpu.ready || !gpu.positionBuffer)                   continue;

        if (cmd.shader != m_lastShader)
        {
            const GDXShaderLayout& layout = GetCachedShaderLayout(cmd.shader, *shader);
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader->vertexShader), nullptr, 0);
            m_context->PSSetShader(static_cast<ID3D11PixelShader*>(shader->pixelShader), nullptr, 0);
            m_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(shader->inputLayout));
            BindFrameConstantsForShader(*shader);
            for (uint32_t i = 0; i < layout.textureBindingCount; ++i)
            {
                if (layout.textureBindings[i].semantic == GDXShaderTextureSemantic::ShadowMap)
                {
                    ID3D11ShaderResourceView* nullShadow = nullptr;
                    const UINT slot = layout.textureBindings[i].shaderRegister;
                    m_context->PSSetShaderResources(slot, 1, &nullShadow);
                    break;
                }
            }
            m_lastShader = cmd.shader;
            m_lastAppliedPipelineKey = 0u;
            ResetScopeCaches();
        }

        BindSkinningPalette(registry, cmd, *shader);

        ApplyScopedBindings(cmd, texStore, false);

        {
            Dx11EntityConstants ec = {};
            std::memcpy(ec.worldMatrix, &cmd.worldMatrix, 64);

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_context->Map(m_entityCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &ec, sizeof(ec));
                m_context->Unmap(m_entityCB, 0);
            }
            BindEntityConstantsForShader(*shader);
        }

        ApplyPipelineState(cmd);
        ApplyPrimitiveTopology(cmd);

        if (!BindVertexStreams(gpu, shader->vertexFlags)) continue;

        if (gpu.indexBuffer)
        {
            m_context->IASetIndexBuffer(static_cast<ID3D11Buffer*>(gpu.indexBuffer), DXGI_FORMAT_R32_UINT, 0u);
            m_context->DrawIndexed(gpu.indexCount, 0u, 0);
        }
        else
        {
            m_context->Draw(gpu.vertexCount, 0u);
        }
        ++m_drawCalls;
    }
}

// ---------------------------------------------------------------------------
// ExecuteQueue
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::ExecuteQueue(
    Registry& registry,
    const ICommandList& queue,
    ResourceStore<MeshAssetResource, MeshTag>& meshStore,
    ResourceStore<MaterialResource, MaterialTag>& matStore,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    void* shadowSRV)
{
    m_drawCalls = 0u;
    m_lastShader = ShaderHandle::Invalid();
    m_lastAppliedPipelineKey = 0u;
    ResetScopeCaches();

    ID3D11ShaderResourceView* globalShadowSRV = static_cast<ID3D11ShaderResourceView*>(shadowSRV);

    const auto& commands = queue.GetCommands();
    for (const RenderCommand& cmd : commands)
    {
        MeshAssetResource* mesh = meshStore.Get(cmd.mesh);
        GDXShaderResource* shader = shaderStore.Get(cmd.shader);

        if (!mesh || !shader || !shader->IsValid()) continue;
        if (cmd.submeshIndex >= mesh->gpuBuffers.size())    continue;

        const GpuMeshBuffer& gpu = mesh->gpuBuffers[cmd.submeshIndex];
        if (!gpu.ready || !gpu.positionBuffer)              continue;

        // --- Shader (State-Batching) ----------------------------------------
        if (cmd.shader != m_lastShader)
        {
            const GDXShaderLayout& layout = GetCachedShaderLayout(cmd.shader, *shader);
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader->vertexShader), nullptr, 0);
            m_context->PSSetShader(static_cast<ID3D11PixelShader*> (shader->pixelShader), nullptr, 0);
            m_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(shader->inputLayout));
            BindFrameConstantsForShader(*shader);
            for (uint32_t i = 0; i < layout.textureBindingCount; ++i)
            {
                if (layout.textureBindings[i].semantic == GDXShaderTextureSemantic::ShadowMap)
                {
                    const UINT slot = layout.textureBindings[i].shaderRegister;
                    m_context->PSSetShaderResources(slot, 1, &globalShadowSRV);
                    break;
                }
            }
            m_lastShader = cmd.shader;
            m_lastAppliedPipelineKey = 0u;
            ResetScopeCaches();
        }

        BindSkinningPalette(registry, cmd, *shader);

        // --- Scoped Bindings -----------------------------------------------
        ApplyScopedBindings(cmd, texStore, true);

        // --- Entity cbuffer b0 ----------------------------------------------
        {
            Dx11EntityConstants ec = {};
            std::memcpy(ec.worldMatrix, &cmd.worldMatrix, 64);

            DirectX::XMMATRIX w = GDXMathHelpers::LoadFloat4x4(cmd.worldMatrix);
            DirectX::XMMATRIX wIT = DirectX::XMMatrixTranspose(
                DirectX::XMMatrixInverse(nullptr, w));
            GIDX::Float4x4 witF;
            GDXMathHelpers::StoreFloat4x4(witF, wIT);
            std::memcpy(ec.worldInverseTranspose, &witF, 64);

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_context->Map(m_entityCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &ec, sizeof(ec));
                m_context->Unmap(m_entityCB, 0);
            }
            BindEntityConstantsForShader(*shader);
        }

        ApplyPipelineState(cmd);
        ApplyPrimitiveTopology(cmd);

        // --- Vertex Streams --------------------------------------------------
        if (!BindVertexStreams(gpu, shader->vertexFlags)) continue;

        // --- Draw -----------------------------------------------------------
        if (gpu.indexBuffer)
        {
            m_context->IASetIndexBuffer(
                static_cast<ID3D11Buffer*>(gpu.indexBuffer), DXGI_FORMAT_R32_UINT, 0u);
            m_context->DrawIndexed(gpu.indexCount, 0u, 0);
        }
        else
        {
            m_context->Draw(gpu.vertexCount, 0u);
        }
        ++m_drawCalls;
    }
}
