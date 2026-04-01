#include "GDXDX11RenderExecutor.h"
#include "GDXDX11TextureSemanticMapping.h"
#include "GDXDX11GpuResources.h"
#include "GDXVertexFlags.h"
#include "SubmeshData.h"
#include "Core/Debug.h"
#include "GDXTextureSlots.h"
#include "GDXDX11MaterialSerializer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include "Core/GDXMath.h"
#include "Core/GDXMathOps.h"
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
    case ResourceState::Unknown:         return "Unknown";
    case ResourceState::Common:          return "Common";
    case ResourceState::ShaderRead:      return "ShaderRead";
    case ResourceState::RenderTarget:    return "RenderTarget";
    case ResourceState::DepthWrite:      return "DepthWrite";
    case ResourceState::DepthRead:       return "DepthRead";
    case ResourceState::UnorderedAccess: return "UnorderedAccess";
    case ResourceState::CopySource:      return "CopySource";
    case ResourceState::CopyDest:        return "CopyDest";
    case ResourceState::Present:         return "Present";
    default:                          return "?";
    }
}

// ===========================================================================
// GDXDX11MeshUploader
// ===========================================================================
bool GDXDX11MeshUploader::Upload(MeshHandle handle, MeshAssetResource& mesh, GDXDX11GpuRegistry& registry)
{
    for (uint32_t i = 0; i < mesh.SubmeshCount(); ++i)
    {
        DX11MeshGpu gpu;
        if (!UploadSubmesh(mesh.submeshes[i], gpu)) return false;
        registry.SetMesh(handle, i, std::move(gpu));
    }
    mesh.gpuReady = true;
    return true;
}

bool GDXDX11MeshUploader::UploadSubmesh(SubmeshData& cpu, DX11MeshGpu& gpu)
{
    if (cpu.VertexCount() == 0) return false;
    gpu.vertexCount = cpu.VertexCount();

    auto upload = [&](const void* data, uint32_t stride, uint32_t count,
        ID3D11Buffer*& outBuf, uint32_t& outStride) -> bool
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
    (void)mesh;
    // GPU buffers are owned by GDXDX11GpuRegistry and released there.
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
    ResetCommandBindings();
    m_pipelineCache.Clear();
    m_layoutCache.Clear();
    m_lastAppliedPipelineKey = 0u;
    return m_entityCB && m_frameCB && m_cascadeCB && m_shadowPassInfoCB;
}

void GDXDX11RenderExecutor::CreateConstantBuffers()
{
    m_entityCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11EntityConstants), D3D11_BIND_CONSTANT_BUFFER, true);
    m_frameCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11FrameConstants), D3D11_BIND_CONSTANT_BUFFER, true);
    m_skinCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11SkinConstants), D3D11_BIND_CONSTANT_BUFFER, true);
    m_cascadeCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11CascadeConstants), D3D11_BIND_CONSTANT_BUFFER, true);
    m_shadowPassInfoCB = CreateBuffer(m_device, nullptr,
        sizeof(Dx11ShadowPassInfoConstants), D3D11_BIND_CONSTANT_BUFFER, true);
}

void GDXDX11RenderExecutor::Shutdown()
{
    if (m_entityCB) { m_entityCB->Release(); m_entityCB = nullptr; }
    if (m_frameCB) { m_frameCB->Release();  m_frameCB = nullptr; }
    if (m_skinCB) { m_skinCB->Release();   m_skinCB = nullptr; }
    if (m_cascadeCB) { m_cascadeCB->Release(); m_cascadeCB = nullptr; }
    if (m_shadowPassInfoCB) { m_shadowPassInfoCB->Release(); m_shadowPassInfoCB = nullptr; }
    m_textureStates.clear();
    ResetScopeCaches();
    ResetCommandBindings();
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

void GDXDX11RenderExecutor::UpdateCascadeConstants(const FrameData& frame)
{
    if (!m_cascadeCB) return;

    Dx11CascadeConstants cc = {};
    cc.cascadeCount = frame.shadowCascadeCount;

    for (uint32_t c = 0; c < frame.shadowCascadeCount && c < 4u; ++c)
    {
        std::memcpy(cc.cascadeViewProj[c], &frame.shadowCascadeViewProj[c], 64);
        cc.cascadeSplits[c] = frame.shadowCascadeSplits[c];
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_cascadeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &cc, sizeof(cc));
        m_context->Unmap(m_cascadeCB, 0);
    }

    // Main pass: Cascade-CB bleibt fuer den Pixel Shader auf b5 gebunden.
    m_context->PSSetConstantBuffers(5, 1, &m_cascadeCB);
}

void GDXDX11RenderExecutor::UpdateShadowPassInfo(uint32_t currentCascade)
{
    if (!m_shadowPassInfoCB) return;

    Dx11ShadowPassInfoConstants sp = {};
    sp.currentCascade = currentCascade;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_context->Map(m_shadowPassInfoCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &sp, sizeof(sp));
        m_context->Unmap(m_shadowPassInfoCB, 0);
    }

    m_context->VSSetConstantBuffers(2, 1, &m_shadowPassInfoCB);
    m_context->VSSetConstantBuffers(5, 1, &m_cascadeCB);
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
        const GDXDX11StageRegisterPair regs = GDXDX11RegistersForConstantBufferBinding(
            cb.bindingGroup, cb.slot, cb.layoutBindingIndex, cb.visibility);
        if (regs.vs != 255u)
            m_context->VSSetConstantBuffers(regs.vs, 1, &buffer);
        if (regs.ps != 255u)
            m_context->PSSetConstantBuffers(regs.ps, 1, &buffer);
        break;
    }

    // Cascade-CB (b5 PS) immer mitbinden wenn vorhanden — kein eigener Layout-Eintrag nötig.
    if (m_cascadeCB)
        m_context->PSSetConstantBuffers(5, 1, &m_cascadeCB);
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
        const GDXDX11StageRegisterPair regs = GDXDX11RegistersForConstantBufferBinding(
            cb.bindingGroup, cb.slot, cb.layoutBindingIndex, cb.visibility);
        if (regs.vs != 255u)
            m_context->VSSetConstantBuffers(regs.vs, 1, &buffer);
        if (regs.ps != 255u)
            m_context->PSSetConstantBuffers(regs.ps, 1, &buffer);
        return;
    }
}


bool GDXDX11RenderExecutor::BindPipelineCommand(
    ShaderHandle shaderHandle,
    ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
    GDXDX11GpuRegistry& gpuRegistry,
    ID3D11ShaderResourceView* shadowSRV,
    bool shadowPass)
{
    GDXShaderResource* shader = shaderStore.Get(shaderHandle);
    if (!shader || !shader->IsValid())
        return false;

    DX11ShaderGpu* sg = gpuRegistry.GetShader(shaderHandle);
    if (!sg)
        return false;

    if (shaderHandle != m_boundShaderHandle)
    {
        const GDXShaderLayout& layout = GetCachedShaderLayout(shaderHandle, *shader);
        const GDXPipelineLayoutDesc& pipelineLayout = GetCachedPipelineLayout(shaderHandle, *shader);

        m_context->VSSetShader(sg->vertexShader, nullptr, 0u);
        m_context->PSSetShader(sg->pixelShader, nullptr, 0u);
        m_context->IASetInputLayout(sg->inputLayout);
        BindFrameConstantsForShader(*shader);
        BindEntityConstantsForShader(*shader);

        for (uint32_t i = 0; i < layout.textureBindingCount; ++i)
        {
            if (layout.textureBindings[i].semantic != GDXShaderTextureSemantic::ShadowMap)
                continue;

            ID3D11ShaderResourceView* boundShadow = shadowPass ? nullptr : shadowSRV;
            const GDXShaderTextureBinding* resolvedShadow = pipelineLayout.FindTextureBinding(
                layout.textureBindings[i].bindingGroup,
                layout.textureBindings[i].layoutBindingIndex);
            if (resolvedShadow)
            {
                const UINT slot = GDXDX11PixelShaderRegisterForTextureBinding(*resolvedShadow);
                m_context->PSSetShaderResources(slot, 1u, &boundShadow);
            }
            break;
        }

        m_boundShaderHandle = shaderHandle;
    }

    m_lastShader = shaderHandle;
    return true;
}

bool GDXDX11RenderExecutor::BindVertexBufferCommand(
    MeshHandle mesh,
    uint32_t submeshIndex,
    uint32_t vertexFlags,
    GDXDX11GpuRegistry& gpuRegistry)
{
    m_boundMeshGpu = gpuRegistry.GetMesh(mesh, submeshIndex);
    if (!m_boundMeshGpu || !m_boundMeshGpu->ready)
        return false;

    return BindVertexStreams(*m_boundMeshGpu, vertexFlags);
}

bool GDXDX11RenderExecutor::BindIndexBufferCommand(
    MeshHandle mesh,
    uint32_t submeshIndex,
    GDXDX11GpuRegistry& gpuRegistry)
{
    if (!m_boundMeshGpu)
        m_boundMeshGpu = gpuRegistry.GetMesh(mesh, submeshIndex);

    if (!m_boundMeshGpu || !m_boundMeshGpu->ready)
        return false;

    if (!m_boundMeshGpu->indexBuffer)
        return false;

    m_context->IASetIndexBuffer(m_boundMeshGpu->indexBuffer, DXGI_FORMAT_R32_UINT, 0u);
    return true;
}

void GDXDX11RenderExecutor::DrawCommand(uint32_t vertexCount, uint32_t vertexStart)
{
    if (!m_context || vertexCount == 0u)
        return;

    m_context->Draw(vertexCount, vertexStart);
    ++m_drawCalls;
}

void GDXDX11RenderExecutor::DrawIndexedCommand(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
    if (!m_context || !m_boundMeshGpu)
        return;

    const uint32_t finalIndexCount = (indexCount != 0u) ? indexCount : m_boundMeshGpu->indexCount;
    if (finalIndexCount == 0u || !m_boundMeshGpu->indexBuffer)
        return;

    m_context->DrawIndexed(finalIndexCount, startIndex, baseVertex);
    ++m_drawCalls;
}

void GDXDX11RenderExecutor::ResetCommandBindings()
{
    m_boundMeshGpu = nullptr;
    m_boundShaderHandle = ShaderHandle::Invalid();
}

// ---------------------------------------------------------------------------
// BindVertexStreams — flag-gesteuert (wie OYNAME SurfaceGpuBuffer::Draw)
// ---------------------------------------------------------------------------
bool GDXDX11RenderExecutor::BindVertexStreams(const DX11MeshGpu& gpu, uint32_t flags)
{
    ID3D11Buffer* buffers[8] = {};
    UINT          strides[8] = {};
    UINT          offsets[8] = {};
    UINT          slot = 0u;

    auto bind = [&](bool needed, ID3D11Buffer* buf, uint32_t stride) -> bool
        {
            if (!needed) return true;
            if (!buf || stride == 0) return false;
            buffers[slot] = buf;
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
        ID3D11Buffer* uv1Buf = gpu.uv2Buffer ? gpu.uv2Buffer : gpu.uv1Buffer;
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
// BindTexturesForGroup — setzt vorbereitete semantische Textur-Bindings der aufgezeichneten Binding-Group.
// Pass/Material werden nur bei Scope-Wechsel erneut gesetzt, Draw pro Draw.
// ShadowMap wird hier nicht gesetzt, weil sie vom Pass-/Shaderpfad kommt.
// ---------------------------------------------------------------------------
ID3D11ShaderResourceView* GDXDX11RenderExecutor::ResolveTextureSRVForBinding(
    const GDXRecordedTextureBinding& binding,
    GDXDX11GpuRegistry& gpuRegistry,
    ID3D11ShaderResourceView* shadowSRV,
    TextureHandle defaultWhite,
    TextureHandle defaultNormal,
    TextureHandle defaultORM,
    TextureHandle defaultBlack)
{
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
        return shadowSRV;
    default:
        return nullptr;
    }

    if (texHandle.IsValid())
        ValidateShaderReadState(texHandle, "ResolveTextureSRVForBinding");

    if (DX11TextureGpu* gpu = gpuRegistry.GetTexture(texHandle))
        return gpu->srv;

    return nullptr;
}

ID3D11Buffer* GDXDX11RenderExecutor::ResolveConstantBufferForBinding(
    const GDXRecordedConstantBufferBinding& binding,
    const GDXRecordedDrawItem& item,
    GDXDX11GpuRegistry& gpuRegistry,
    bool applyReceiveShadowOverride)
{
    ID3D11Buffer* cb = nullptr;
    switch (binding.semantic)
    {
    case GDXShaderConstantBufferSlot::Frame:
        cb = m_frameCB;
        break;
    case GDXShaderConstantBufferSlot::Entity:
        cb = m_entityCB;
        break;
    case GDXShaderConstantBufferSlot::Skin:
        cb = m_skinCB;
        break;
    case GDXShaderConstantBufferSlot::Material:
        if (binding.materialHandle.IsValid())
        {
            if (DX11MaterialGpu* matGpu = gpuRegistry.GetMaterial(binding.materialHandle))
                cb = matGpu->constantBuffer;
        }
        break;
    default:
        break;
    }

    if (binding.semantic == GDXShaderConstantBufferSlot::Material && cb)
    {
        MaterialCBuffer drawData = ToGPU(item.materialParams, item.materialRenderPolicy, item.materialTextureLayers);
        if (applyReceiveShadowOverride)
        {
            constexpr uint32_t kReceiveShadowsBit = (1u << 12);
            const bool materialReceive = (drawData.gFlags & kReceiveShadowsBit) != 0u;
            if (materialReceive && !item.receiveShadows)
                drawData.gFlags &= ~kReceiveShadowsBit;
        }

        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(m_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            std::memcpy(m.pData, &drawData, sizeof(MaterialCBuffer));
            m_context->Unmap(cb, 0);
        }
    }

    return cb;
}

void GDXDX11RenderExecutor::BindTextureBinding(
    const GDXPipelineLayoutDesc& pipelineLayout,
    const GDXRecordedTextureBinding& binding,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    GDXDX11GpuRegistry& gpuRegistry,
    ID3D11ShaderResourceView* shadowSRV,
    TextureHandle defaultWhite,
    TextureHandle defaultNormal,
    TextureHandle defaultORM,
    TextureHandle defaultBlack)
{
    (void)texStore;
    const GDXShaderTextureBinding* resolvedBinding = pipelineLayout.FindTextureBinding(binding.bindingGroup, binding.bindingIndex);
    if (!resolvedBinding)
        return;

    ID3D11ShaderResourceView* srv = ResolveTextureSRVForBinding(
        binding, gpuRegistry, shadowSRV, defaultWhite, defaultNormal, defaultORM, defaultBlack);

    const UINT slot = GDXDX11PixelShaderRegisterForTextureBinding(*resolvedBinding);
    if (HasStageVisibility(resolvedBinding->visibility, GDXShaderStageVisibility::Pixel))
        m_context->PSSetShaderResources(slot, 1, &srv);
}

void GDXDX11RenderExecutor::BindConstantBufferBinding(
    const GDXPipelineLayoutDesc& pipelineLayout,
    const GDXRecordedConstantBufferBinding& binding,
    const GDXRecordedDrawItem& item,
    GDXDX11GpuRegistry& gpuRegistry,
    bool applyReceiveShadowOverride)
{
    if (!binding.enabled)
        return;

    const GDXShaderConstantBufferBinding* resolvedBinding =
        pipelineLayout.FindConstantBufferBinding(binding.bindingGroup, binding.bindingIndex);
    if (!resolvedBinding)
        return;

    ID3D11Buffer* cb = ResolveConstantBufferForBinding(binding, item, gpuRegistry, applyReceiveShadowOverride);

    const GDXDX11StageRegisterPair regs = GDXDX11RegistersForConstantBufferBinding(
        resolvedBinding->bindingGroup, resolvedBinding->slot, resolvedBinding->layoutBindingIndex, resolvedBinding->visibility);
    if (regs.vs != 255u && HasStageVisibility(resolvedBinding->visibility, GDXShaderStageVisibility::Vertex))
        m_context->VSSetConstantBuffers(regs.vs, 1, &cb);
    if (regs.ps != 255u && HasStageVisibility(resolvedBinding->visibility, GDXShaderStageVisibility::Pixel))
        m_context->PSSetConstantBuffers(regs.ps, 1, &cb);
}

void GDXDX11RenderExecutor::BindBindingGroup(
    const GDXPipelineLayoutDesc& pipelineLayout,
    const GDXRecordedDrawItem& item,
    const GDXRecordedBindingGroupData& bindings,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    GDXDX11GpuRegistry& gpuRegistry,
    ID3D11ShaderResourceView* shadowSRV,
    TextureHandle defaultWhite,
    TextureHandle defaultNormal,
    TextureHandle defaultORM,
    TextureHandle defaultBlack,
    bool applyReceiveShadowOverride)
{
    for (uint32_t i = 0; i < bindings.textureCount; ++i)
        BindTextureBinding(pipelineLayout, bindings.textures[i], texStore, gpuRegistry, shadowSRV, defaultWhite, defaultNormal, defaultORM, defaultBlack);

    for (uint32_t i = 0; i < bindings.constantBufferCount; ++i)
        BindConstantBufferBinding(pipelineLayout, bindings.constantBuffers[i], item, gpuRegistry, applyReceiveShadowOverride);
}

void GDXDX11RenderExecutor::ApplyBindingsForGroup(
    const GDXPipelineLayoutDesc& pipelineLayout,
    const GDXRecordedDrawItem& item,
    const GDXRecordedBindingGroupData& groupData,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    GDXDX11GpuRegistry& gpuRegistry,
    ResourceBindingScope scope,
    bool applyReceiveShadowOverride)
{
    if (!groupData.HasAnyEnabledBinding())
    {
        m_bindingCache.Invalidate(scope);
        return;
    }

    const uint64_t scopeKey = item.GetBindingsKeyForScope(scope);
    if (scopeKey != 0ull && !m_bindingCache.ShouldApply(scope, scopeKey))
        return;

    BindBindingGroup(pipelineLayout, item, groupData, texStore, gpuRegistry, nullptr,
        defaultWhiteTex, defaultNormalTex, defaultORMTex, defaultBlackTex, applyReceiveShadowOverride);

    if (scopeKey != 0ull)
        m_bindingCache.MarkApplied(scope, scopeKey);
}

bool GDXDX11RenderExecutor::ValidateBindingGroupForLayout(
    const GDXPipelineLayoutDesc& pipelineLayout,
    const GDXRecordedBindingGroupData& bindings) const noexcept
{
    if (!pipelineLayout.IsValid())
        return false;

    const GDXBindingGroupLayoutDesc* groupLayout = pipelineLayout.FindGroupLayout(bindings.bindingGroup);
    if (!groupLayout)
        return false;

    if (bindings.scope != GDXBindingScopeFromGroup(bindings.bindingGroup))
        return false;

    for (uint32_t i = 0; i < bindings.textureCount; ++i)
    {
        const auto& binding = bindings.textures[i];
        if (binding.bindingGroup != bindings.bindingGroup)
            return false;
        if (GDXBindingScopeFromGroup(binding.bindingGroup) != bindings.scope)
            return false;
        if (!binding.enabled)
        {
            if (binding.required && groupLayout->IsBindingRequired(binding.bindingIndex, GDXBoundResourceClass::Texture))
                return false;
            continue;
        }

        const GDXShaderTextureBinding* resolved = pipelineLayout.FindTextureBinding(binding.bindingGroup, binding.bindingIndex);
        if (!resolved)
            return false;
        if (!groupLayout->HasBinding(binding.bindingIndex, GDXBoundResourceClass::Texture))
            return false;
        if (resolved->resourceClass != binding.resourceClass || resolved->resourceClass != GDXBoundResourceClass::Texture)
            return false;
        if (resolved->visibility == GDXShaderStageVisibility::None)
            return false;
    }

    for (uint32_t i = 0; i < bindings.constantBufferCount; ++i)
    {
        const auto& binding = bindings.constantBuffers[i];
        if (binding.bindingGroup != bindings.bindingGroup)
            return false;
        if (GDXBindingScopeFromGroup(binding.bindingGroup) != bindings.scope)
            return false;
        if (!binding.enabled)
        {
            if (binding.required && groupLayout->IsBindingRequired(binding.bindingIndex, GDXBoundResourceClass::ConstantBuffer))
                return false;
            continue;
        }

        const GDXShaderConstantBufferBinding* resolved = pipelineLayout.FindConstantBufferBinding(binding.bindingGroup, binding.bindingIndex);
        if (!resolved)
            return false;
        if (!groupLayout->HasBinding(binding.bindingIndex, GDXBoundResourceClass::ConstantBuffer))
            return false;
        if (resolved->resourceClass != binding.resourceClass || resolved->resourceClass != GDXBoundResourceClass::ConstantBuffer)
            return false;
        if (resolved->visibility == GDXShaderStageVisibility::None)
            return false;
    }

    return true;
}

void GDXDX11RenderExecutor::BindResolvedBindingGroup(
    const GDXPipelineLayoutDesc& pipelineLayout,
    const GDXRecordedBindingGroupData& bindings,
    const std::array<ID3D11ShaderResourceView*, GDXRecordedBindingGroupData::MaxTextureBindings>& explicitSrvs,
    ID3D11Buffer* externalConstantBuffer)
{
    if (!ValidateBindingGroupForLayout(pipelineLayout, bindings))
        return;

    for (uint32_t i = 0; i < bindings.textureCount; ++i)
    {
        const GDXRecordedTextureBinding& binding = bindings.textures[i];
        const GDXShaderTextureBinding* resolvedBinding =
            pipelineLayout.FindTextureBinding(binding.bindingGroup, binding.bindingIndex);
        if (!resolvedBinding)
            continue;

        ID3D11ShaderResourceView* srv = nullptr;
        if (binding.bindingIndex < explicitSrvs.size())
            srv = explicitSrvs[binding.bindingIndex];

        const UINT slot = GDXDX11PixelShaderRegisterForTextureBinding(*resolvedBinding);
        if (HasStageVisibility(resolvedBinding->visibility, GDXShaderStageVisibility::Pixel))
            m_context->PSSetShaderResources(slot, 1u, &srv);
    }

    for (uint32_t i = 0; i < bindings.constantBufferCount; ++i)
    {
        const GDXRecordedConstantBufferBinding& binding = bindings.constantBuffers[i];
        const GDXShaderConstantBufferBinding* resolvedBinding =
            pipelineLayout.FindConstantBufferBinding(binding.bindingGroup, binding.bindingIndex);
        if (!resolvedBinding)
            continue;

        ID3D11Buffer* cb = externalConstantBuffer;
        const GDXDX11StageRegisterPair regs = GDXDX11RegistersForConstantBufferBinding(
            resolvedBinding->bindingGroup, resolvedBinding->slot, resolvedBinding->layoutBindingIndex, resolvedBinding->visibility);
        if (regs.vs != 255u && HasStageVisibility(resolvedBinding->visibility, GDXShaderStageVisibility::Vertex))
            m_context->VSSetConstantBuffers(regs.vs, 1u, &cb);
        if (regs.ps != 255u && HasStageVisibility(resolvedBinding->visibility, GDXShaderStageVisibility::Pixel))
            m_context->PSSetConstantBuffers(regs.ps, 1u, &cb);
    }
}

void GDXDX11RenderExecutor::BindExplicitPassResources(
    const GDXPipelineLayoutDesc& pipelineLayout,
    const GDXRecordedDrawItem& item,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    GDXDX11GpuRegistry& gpuRegistry,
    ID3D11ShaderResourceView* shadowSRV)
{
    const uint64_t scopeKey = item.passBindingsKey;
    if (scopeKey != 0ull && !m_bindingCache.ShouldApply(ResourceBindingScope::Pass, scopeKey))
        return;

    BindBindingGroup(pipelineLayout, item, item.passBindings, texStore, gpuRegistry, shadowSRV,
        defaultWhiteTex, defaultNormalTex, defaultORMTex, defaultBlackTex, false);

    if (scopeKey != 0ull)
        m_bindingCache.MarkApplied(ResourceBindingScope::Pass, scopeKey);
}

void GDXDX11RenderExecutor::BuildRecordedStreamFromQueue(const ICommandList& queue, GDXRecordedCommandStream& outStream)
{
    outStream.Clear();

    const auto& commands = queue.GetCommands();
    outStream.drawItems.reserve(commands.size());
    outStream.commands.reserve(commands.size() * 5u);

    for (const RenderCommand& cmd : commands)
    {
        GDXRecordedDrawItem item = GDXRecordedDrawItem::FromRenderCommand(cmd);
        item.passBuildDesc = BuildDescriptorSetBuildDesc(item.resourceBindings, ResourceBindingScope::Pass);
        item.materialBuildDesc = BuildDescriptorSetBuildDesc(item.resourceBindings, ResourceBindingScope::Material);
        item.drawBuildDesc = BuildDescriptorSetBuildDesc(item.resourceBindings, ResourceBindingScope::Draw);

        item.passBindings = BuildRecordedBindingGroupData(item.passBuildDesc);
        item.materialBindings = BuildRecordedBindingGroupData(item.materialBuildDesc);
        item.drawBindings = BuildRecordedBindingGroupData(item.drawBuildDesc);

        if (!GDXValidateDescriptorSetBuildDesc(item.passBuildDesc) || !GDXValidateBindingGroupData(item.passBindings))
            Debug::LogWarning(GDX_SRC_LOC, "Recorded pass binding group/layout data is inconsistent.");
        if (!GDXValidateDescriptorSetBuildDesc(item.materialBuildDesc) || !GDXValidateBindingGroupData(item.materialBindings))
            Debug::LogWarning(GDX_SRC_LOC, "Recorded material binding group/layout data is inconsistent.");
        if (!GDXValidateDescriptorSetBuildDesc(item.drawBuildDesc) || !GDXValidateBindingGroupData(item.drawBindings))
            Debug::LogWarning(GDX_SRC_LOC, "Recorded draw binding group/layout data is inconsistent.");

        const uint32_t drawIndex = outStream.AddDrawItem(item);
        outStream.AddOp(GDXRecordedOpType::SetPipeline, drawIndex);
        outStream.AddOp(GDXRecordedOpType::BindPassResources, drawIndex);
        outStream.AddOp(GDXRecordedOpType::BindMaterialResources, drawIndex);
        outStream.AddOp(GDXRecordedOpType::BindDrawResources, drawIndex);
        outStream.AddOp(GDXRecordedOpType::DrawMesh, drawIndex);
    }
}

void GDXDX11RenderExecutor::ExecuteRecordedStream(
    Registry& registry,
    const GDXRecordedCommandStream& stream,
    ResourceStore<MeshAssetResource,  MeshTag>& meshStore,
    ResourceStore<MaterialResource,   MaterialTag>& matStore,
    ResourceStore<GDXShaderResource,  ShaderTag>& shaderStore,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    GDXDX11GpuRegistry& gpuRegistry,
    ID3D11ShaderResourceView* shadowSRV,
    bool shadowPass)
{
    (void)matStore;

    m_drawCalls = 0u;
    m_lastShader = ShaderHandle::Invalid();
    m_lastAppliedPipelineKey = 0u;
    ResetScopeCaches();

    const GDXRecordedDrawItem* currentItem = nullptr;
    const GDXShaderResource* currentShader = nullptr;
    DX11MeshGpu* currentMeshGpu = nullptr;

    auto bindShaderState = [&](const GDXRecordedDrawItem& item) -> bool
    {
        GDXShaderResource* shader = shaderStore.Get(item.shader);
        if (!shader || !shader->IsValid())
            return false;

        currentShader = shader;
        if (item.shader != m_lastShader)
        {
            const GDXShaderLayout& layout = GetCachedShaderLayout(item.shader, *shader);
            const GDXPipelineLayoutDesc& pipelineLayout = GetCachedPipelineLayout(item.shader, *shader);
            if (DX11ShaderGpu* sg = gpuRegistry.GetShader(item.shader))
            {
                m_context->VSSetShader(sg->vertexShader, nullptr, 0);
                m_context->PSSetShader(sg->pixelShader, nullptr, 0);
                m_context->IASetInputLayout(sg->inputLayout);
            }
            BindFrameConstantsForShader(*shader);
            for (uint32_t i = 0; i < layout.textureBindingCount; ++i)
            {
                if (layout.textureBindings[i].semantic == GDXShaderTextureSemantic::ShadowMap)
                {
                    ID3D11ShaderResourceView* boundShadow = shadowPass ? nullptr : shadowSRV;
                    const GDXShaderTextureBinding* resolvedShadow = pipelineLayout.FindTextureBinding(layout.textureBindings[i].bindingGroup,
                                                                                                       layout.textureBindings[i].layoutBindingIndex);
                    if (resolvedShadow)
                    {
                        const UINT slot = GDXDX11PixelShaderRegisterForTextureBinding(*resolvedShadow);
                        m_context->PSSetShaderResources(slot, 1, &boundShadow);
                    }
                    break;
                }
            }
            m_lastShader = item.shader;
            m_lastAppliedPipelineKey = 0u;
            ResetScopeCaches();
        }

        currentMeshGpu = gpuRegistry.GetMesh(item.mesh, item.submeshIndex);
        currentItem = &item;
        return currentMeshGpu && currentMeshGpu->ready && currentMeshGpu->positionBuffer;
    };

    for (const GDXRecordedResourceTransition& t : stream.preTransitions)
    {
        if (t.kind == GDXRecordedResourceKind::Texture)
            TransitionTexture(t.texture, t.before, t.after, "RecordedPreTransition");
    }

    for (const GDXRecordedCommand& cmd : stream.commands)
    {
        if (cmd.type == GDXRecordedOpType::Transition)
        {
            if (cmd.transition.kind == GDXRecordedResourceKind::Texture)
                TransitionTexture(cmd.transition.texture, cmd.transition.before, cmd.transition.after, "RecordedTransition");
            continue;
        }

        if (cmd.drawItemIndex >= stream.drawItems.size())
            continue;

        const GDXRecordedDrawItem& item = stream.drawItems[cmd.drawItemIndex];

        switch (cmd.type)
        {
        case GDXRecordedOpType::SetPipeline:
            if (!bindShaderState(item))
            {
                currentItem = nullptr;
                currentShader = nullptr;
                currentMeshGpu = nullptr;
                break;
            }
            BindSkinningPalette(registry, item, *currentShader);
            ApplyPipelineState(item);
            ApplyPrimitiveTopology(item);
            break;

        case GDXRecordedOpType::BindPassResources:
            if (currentItem == &item && currentShader)
                BindExplicitPassResources(GetCachedPipelineLayout(item.shader, *currentShader), item, texStore, gpuRegistry, shadowPass ? nullptr : shadowSRV);
            break;

        case GDXRecordedOpType::BindMaterialResources:
            if (currentItem == &item && currentShader)
                ApplyBindingsForGroup(GetCachedPipelineLayout(item.shader, *currentShader), item, item.materialBindings, texStore, gpuRegistry, ResourceBindingScope::Material, false);
            break;

        case GDXRecordedOpType::BindDrawResources:
            if (currentItem == &item && currentShader)
            {
                ApplyBindingsForGroup(GetCachedPipelineLayout(item.shader, *currentShader), item, item.drawBindings, texStore, gpuRegistry, ResourceBindingScope::Draw, !shadowPass);

                Dx11EntityConstants ec = {};
                std::memcpy(ec.worldMatrix, &item.worldMatrix, 64);

                DirectX::XMMATRIX w = GDXMathHelpers::LoadMatrix4(item.worldMatrix);
                DirectX::XMMATRIX wIT = shadowPass
                    ? DirectX::XMMatrixIdentity()
                    : DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, w));
                Matrix4 witF;
                GDXMathHelpers::StoreMatrix4(witF, wIT);
                std::memcpy(ec.worldInverseTranspose, &witF, 64);

                D3D11_MAPPED_SUBRESOURCE mapped = {};
                if (SUCCEEDED(m_context->Map(m_entityCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                {
                    std::memcpy(mapped.pData, &ec, sizeof(ec));
                    m_context->Unmap(m_entityCB, 0);
                }
                BindEntityConstantsForShader(*currentShader);
            }
            break;

        case GDXRecordedOpType::DrawMesh:
            if (currentItem != &item || !currentShader || !currentMeshGpu)
                break;
            if (!BindVertexStreams(*currentMeshGpu, currentShader->vertexFlags))
                break;
            if (currentMeshGpu->indexBuffer)
            {
                m_context->IASetIndexBuffer(currentMeshGpu->indexBuffer, DXGI_FORMAT_R32_UINT, 0u);
                m_context->DrawIndexed(currentMeshGpu->indexCount, 0u, 0);
            }
            else
            {
                m_context->Draw(currentMeshGpu->vertexCount, 0u);
            }
            ++m_drawCalls;
            break;
        }
    }

    for (const GDXRecordedResourceTransition& t : stream.postTransitions)
    {
        if (t.kind == GDXRecordedResourceKind::Texture)
            TransitionTexture(t.texture, t.before, t.after, "RecordedPostTransition");
    }
}

void GDXDX11RenderExecutor::ResetScopeCaches()
{
    m_bindingCache.Reset();
}

const GDXShaderLayout& GDXDX11RenderExecutor::GetCachedShaderLayout(ShaderHandle shaderHandle, const GDXShaderResource& shader)
{
    return m_layoutCache.GetOrCreate(shaderHandle, shader).layout;
}

const GDXPipelineLayoutDesc& GDXDX11RenderExecutor::GetCachedPipelineLayout(ShaderHandle shaderHandle, const GDXShaderResource& shader)
{
    return m_layoutCache.GetOrCreate(shaderHandle, shader).pipelineLayout;
}

// ---------------------------------------------------------------------------
// BindSkinningPalette — immer direkt auf VS b4 binden (Engine-Invariante).
// Kein Binding-Lookup — der ist fehleranfällig wenn der Skinned-Shader über
// CreateShader ohne Layout-Skin-Eintrag geladen wird.
void GDXDX11RenderExecutor::BindSkinningPalette(
    Registry& registry,
    const GDXRecordedDrawItem& item,
    const GDXShaderResource& shader)
{
    if (!shader.supportsSkinning || !m_skinCB)
        return;

    Dx11SkinConstants sc = {};
    for (uint32_t i = 0; i < SkinComponent::MaxBones; ++i)
    {
        Matrix4 ident = Matrix4::Identity();
        std::memcpy(sc.boneMatrices[i], &ident, 64);
    }

    if (item.ownerEntity != NULL_ENTITY)
    {
        auto* skin = registry.Get<SkinComponent>(item.ownerEntity);
        if (skin && skin->enabled)
        {
            const uint32_t count = static_cast<uint32_t>(
                (std::min)(skin->finalBoneMatrices.size(),
                static_cast<size_t>(SkinComponent::MaxBones)));

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

    // Direkt auf b4 — kein Lookup, kein Ausfallrisiko.
    m_context->VSSetConstantBuffers(4u, 1, &m_skinCB);
}

void GDXDX11RenderExecutor::ApplyPipelineState(const GDXRecordedDrawItem& item)
{
    GDXGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.shader = item.shader;
    pipelineDesc.state = item.pipelineState;

    const auto& cached = m_pipelineCache.GetOrCreate(pipelineDesc);
    if (cached.stateKey.value == m_lastAppliedPipelineKey)
        return;

    const GDXPipelineStateDesc& state = cached.desc.state;

    ID3D11RasterizerState* rs = (state.cullMode == GDXCullMode::None) ? m_rsNoCull : m_rsCull;
    if (rs)
        m_context->RSSetState(rs);

    ID3D11DepthStencilState* ds = m_dsReadWrite;
    if (!state.depthTestEnabled)
        ds = m_dsNoTest;
    else if (state.depthMode == GDXDepthMode::ReadOnly)
        ds = m_dsReadOnly;
    if (ds)
        m_context->OMSetDepthStencilState(ds, 0u);

    const float bf[4] = { 0,0,0,0 };
    ID3D11BlendState* blend = (state.blendMode == GDXBlendMode::AlphaBlend) ? m_blendAlpha : m_blendOpaque;
    if (blend)
        m_context->OMSetBlendState(blend, bf, 0xFFFFFFFFu);

    m_lastAppliedPipelineKey = cached.stateKey.value;
}

void GDXDX11RenderExecutor::ApplyPrimitiveTopology(const GDXRecordedDrawItem& item)
{
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    switch (item.pipelineState.topology)
    {
    case GDXPrimitiveTopology::TriangleList:
    default:
        topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
    case GDXPrimitiveTopology::LineList:
        topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
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
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    GDXDX11GpuRegistry& gpuRegistry)
{
    GDXRecordedCommandStream stream{};
    BuildRecordedStreamFromQueue(queue, stream);
    ExecuteRecordedStream(registry, stream, meshStore, matStore, shaderStore, texStore, gpuRegistry, nullptr, true);
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
    GDXDX11GpuRegistry& gpuRegistry,
    ID3D11ShaderResourceView* shadowSRV)
{
    GDXRecordedCommandStream stream{};
    BuildRecordedStreamFromQueue(queue, stream);
    ExecuteRecordedStream(registry, stream, meshStore, matStore, shaderStore, texStore, gpuRegistry, shadowSRV, false);
}


void GDXDX11RenderExecutor::ForgetTextureState(TextureHandle texture)
{
    if (!texture.IsValid())
        return;
    m_textureStates.erase(texture);
}

size_t GDXDX11RenderExecutor::DebugTrackedTextureStateCount() const
{
    return m_textureStates.size();
}

size_t GDXDX11RenderExecutor::DebugPipelineCacheSize() const noexcept
{
    return m_pipelineCache.Size();
}

size_t GDXDX11RenderExecutor::DebugLayoutCacheSize() const noexcept
{
    return m_layoutCache.Size();
}
