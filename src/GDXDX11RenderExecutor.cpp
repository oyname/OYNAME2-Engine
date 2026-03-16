#include "GDXDX11RenderExecutor.h"
#include "GDXVertexFlags.h"
#include "SubmeshData.h"
#include "Debug.h"
#include "GDXTextureSlots.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include "GDXMath.h"
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
uint64_t GDXDX11RenderExecutor::MakeGraphicsPipelineCacheKey(ShaderHandle shader, const GDXPipelineStateDesc& state) const noexcept
{
    GDXGraphicsPipelineDesc desc{};
    desc.shader = shader;
    desc.state = state;
    desc.colorFormat = 0u;
    desc.depthFormat = 0u;
    return desc.MakeKey();
}

void GDXDX11RenderExecutor::InvalidateStateCache()
{
    m_lastGraphicsPipelineCacheKey = ~0ull;
    m_lastBindingSetKey = ~0ull;
    m_lastShader = ShaderHandle::Invalid();
    m_lastMaterial = MaterialHandle::Invalid();
    m_lastVSConstantBuffers.fill(nullptr);
    m_lastPSConstantBuffers.fill(nullptr);
    m_lastPSShaderResources.fill(nullptr);
}

const GDXDX11RenderExecutor::ResolvedLayoutCacheEntry&
GDXDX11RenderExecutor::GetResolvedLayoutCache(const GDXShaderResource& shader, const ResourceBindingSet* passBindings, const ResourceBindingSet& drawBindings)
{
    uint32_t layoutKey = drawBindings.layoutKey;
    if (layoutKey == 0u && passBindings)
        layoutKey = passBindings->layoutKey;
    if (layoutKey == 0u)
        layoutKey = 2166136261u ^ shader.layout.constantBufferCount ^ (shader.layout.textureBindingCount << 16);

    auto it = m_layoutCache.find(layoutKey);
    if (it != m_layoutCache.end())
        return it->second;

    ResolvedLayoutCacheEntry entry{};
    entry.layoutKey = layoutKey;
    entry.valid = true;
    for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
    {
        const auto& cb = shader.layout.constantBuffers[i];
        const size_t idx = static_cast<size_t>(cb.slot);
        if (idx < entry.cbVS.size())
        {
            entry.cbVS[idx] = cb.vsRegister;
            entry.cbPS[idx] = cb.psRegister;
        }
    }
    for (uint32_t i = 0; i < shader.layout.textureBindingCount; ++i)
    {
        const auto& tex = shader.layout.textureBindings[i];
        const size_t idx = static_cast<size_t>(tex.semantic);
        if (idx < entry.texPS.size())
            entry.texPS[idx] = tex.shaderRegister;
    }

    auto [ins, _] = m_layoutCache.emplace(layoutKey, entry);
    return ins->second;
}
bool GDXDX11RenderExecutor::Init(const InitParams& p)
{
    m_device = p.device;
    m_context = p.context;
    if (!m_device || !m_context) return false;
    CreateConstantBuffers();
    m_textureStates.clear();
    m_layoutCache.clear();
    m_pipelineCache.Clear();
    InvalidateStateCache();
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
    m_layoutCache.clear();
    m_pipelineCache.Clear();
    InvalidateStateCache();
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

        ConstantBufferBindingDesc binding{};
        binding.semantic = GDXShaderConstantBufferSlot::Frame;
        binding.vsRegister = cb.vsRegister;
        binding.psRegister = cb.psRegister;
        binding.enabled = true;
        BindConstantBufferBinding(shader, binding, m_frameCB);
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

        ConstantBufferBindingDesc binding{};
        binding.semantic = GDXShaderConstantBufferSlot::Entity;
        binding.vsRegister = cb.vsRegister;
        binding.psRegister = cb.psRegister;
        binding.enabled = true;
        BindConstantBufferBinding(shader, binding, m_entityCB);
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
// Binding Helpers — semantische Descriptor-/Binding-Auflösung.
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::BindConstantBufferBinding(
    const GDXShaderResource& shader,
    const ConstantBufferBindingDesc& binding,
    ID3D11Buffer* buffer)
{
    if (!buffer || !binding.enabled)
        return;

    uint8_t vsRegister = binding.vsRegister;
    uint8_t psRegister = binding.psRegister;

    if (vsRegister == 255u && psRegister == 255u)
    {
        for (uint32_t i = 0; i < shader.layout.constantBufferCount; ++i)
        {
            const auto& src = shader.layout.constantBuffers[i];
            if (src.slot != binding.semantic)
                continue;
            vsRegister = src.vsRegister;
            psRegister = src.psRegister;
            break;
        }
    }

    if (vsRegister != 255u)
        m_context->VSSetConstantBuffers(vsRegister, 1, &buffer);
    if (psRegister != 255u)
        m_context->PSSetConstantBuffers(psRegister, 1, &buffer);
}

void GDXDX11RenderExecutor::BindShaderResourceBinding(
    const GDXShaderResource& shader,
    const ShaderResourceBindingDesc& binding,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    TextureHandle defaultWhite,
    TextureHandle defaultNormal,
    TextureHandle defaultORM,
    TextureHandle defaultBlack,
    void* shadowSRV)
{
    const auto& layoutCache = GetResolvedLayoutCache(shader, nullptr, ResourceBindingSet{});
    UINT slot = binding.bindingIndex;
    const size_t semanticIndex = static_cast<size_t>(binding.semantic);
    if (slot == 255u && semanticIndex < layoutCache.texPS.size())
        slot = layoutCache.texPS[semanticIndex];

    if (slot == 255u || slot >= m_lastPSShaderResources.size())
        return;

    ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(binding.nativeView);
    TextureHandle texHandle = TextureHandle::Invalid();

    if (!srv)
    {
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
            srv = static_cast<ID3D11ShaderResourceView*>(shadowSRV);
            break;
        default:
            return;
        }

        if (texHandle.IsValid())
        {
            ValidateShaderReadState(texHandle, "BindShaderResourceBinding");
            if (const GDXTextureResource* tex = texStore.Get(texHandle))
                srv = static_cast<ID3D11ShaderResourceView*>(tex->srv);
        }
    }

    void* srvKey = srv;
    if (m_lastPSShaderResources[slot] != srvKey)
    {
        m_context->PSSetShaderResources(slot, 1, &srv);
        m_lastPSShaderResources[slot] = srvKey;
    }
}

// ---------------------------------------------------------------------------
// BindMaterialTextures — material-/draw-spezifische SRV-Bindings.
// Pass-Bindings (z. B. ShadowMap) kommen separat.
// ---------------------------------------------------------------------------
void GDXDX11RenderExecutor::BindMaterialTextures(
    const RenderCommand& cmd,
    ResourceStore<GDXTextureResource, TextureTag>& texStore,
    TextureHandle defaultWhite,
    TextureHandle defaultNormal,
    TextureHandle defaultORM,
    TextureHandle defaultBlack)
{
    (void)cmd;
    (void)texStore;
    (void)defaultWhite;
    (void)defaultNormal;
    (void)defaultORM;
    (void)defaultBlack;
}

// ---------------------------------------------------------------------------
// BindSkinningPalette — layout-gesteuertes Skin-CB-Binding.
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

    if (const ConstantBufferBindingDesc* binding = cmd.resourceBindings.FindConstantBufferBinding(GDXShaderConstantBufferSlot::Skin))
    {
        if (binding->enabled)
            BindConstantBufferBinding(shader, *binding, m_skinCB);
    }
}

void GDXDX11RenderExecutor::ApplyPipelineState(const RenderCommand& cmd)
{
    const auto& cached = m_pipelineCache.GetOrCreate(GDXGraphicsPipelineDesc{ cmd.shader, cmd.pipelineState, 0u, 0u });
    const uint64_t pipelineCacheKey = cached.desc.MakeKey();
    if (m_lastGraphicsPipelineCacheKey == pipelineCacheKey)
        return;

    const GDXPipelineStateDesc& state = cached.desc.state;

    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    switch (state.topology)
    {
    case GDXPrimitiveTopology::TriangleList:
    default:
        topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
    }
    m_context->IASetPrimitiveTopology(topology);

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

    m_lastGraphicsPipelineCacheKey = pipelineCacheKey;
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
    const ResourceBindingSet* passBindings)
{
    m_drawCalls = 0u;
    m_lastShader = ShaderHandle::Invalid();
    m_lastMaterial = MaterialHandle::Invalid();

    void* nullShadowSRV = nullptr;

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
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader->vertexShader), nullptr, 0);
            m_context->PSSetShader(static_cast<ID3D11PixelShader*>(shader->pixelShader), nullptr, 0);
            m_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(shader->inputLayout));
            BindFrameConstantsForShader(*shader);

            if (passBindings)
            {
                for (uint32_t i = 0; i < passBindings->constantBufferCount; ++i)
                {
                    const auto& binding = passBindings->constantBuffers[i];
                    if (!binding.enabled || binding.scope != GDXBindingScope::Pass)
                        continue;
                    ID3D11Buffer* buffer = (binding.semantic == GDXShaderConstantBufferSlot::Frame) ? m_frameCB : static_cast<ID3D11Buffer*>(binding.buffer);
                    BindConstantBufferBinding(*shader, binding, buffer);
                }
                for (uint32_t i = 0; i < passBindings->textureCount; ++i)
                {
                    const auto& binding = passBindings->textures[i];
                    if (!binding.enabled || binding.scope != GDXBindingScope::Pass)
                        continue;
                    BindShaderResourceBinding(*shader, binding, texStore,
                        defaultWhiteTex, defaultNormalTex, defaultORMTex, defaultBlackTex,
                        nullShadowSRV);
                }
            }

            m_lastShader = cmd.shader;
            m_lastMaterial = MaterialHandle::Invalid();
        }

        BindSkinningPalette(registry, cmd, *shader);

        if (cmd.material != m_lastMaterial)
        {
            if (const ConstantBufferBindingDesc* materialBinding = cmd.resourceBindings.FindConstantBufferBinding(GDXShaderConstantBufferSlot::Material))
            {
                if (materialBinding->enabled && materialBinding->buffer)
                {
                    auto* cb = static_cast<ID3D11Buffer*>(materialBinding->buffer);
                    D3D11_MAPPED_SUBRESOURCE m = {};
                    if (SUCCEEDED(m_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
                    {
                        std::memcpy(m.pData, &cmd.materialData, sizeof(MaterialData));
                        m_context->Unmap(cb, 0);
                    }
                    BindConstantBufferBinding(*shader, *materialBinding, cb);
                }
            }

            for (uint32_t i = 0; i < cmd.resourceBindings.textureCount; ++i)
            {
                const auto& binding = cmd.resourceBindings.textures[i];
                if (!binding.enabled || binding.scope == GDXBindingScope::Pass)
                    continue;
                BindShaderResourceBinding(*shader, binding, texStore,
                    defaultWhiteTex, defaultNormalTex, defaultORMTex, defaultBlackTex,
                    nullptr);
            }
            m_lastMaterial = cmd.material;
        }

        {
            Dx11EntityConstants ec = {};
            std::memcpy(ec.worldMatrix, &cmd.worldMatrix, 64);

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_context->Map(m_entityCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                std::memcpy(mapped.pData, &ec, sizeof(ec));
                m_context->Unmap(m_entityCB, 0);
            }
            if (const ConstantBufferBindingDesc* entityBinding = cmd.resourceBindings.FindConstantBufferBinding(GDXShaderConstantBufferSlot::Entity))
                BindConstantBufferBinding(*shader, *entityBinding, m_entityCB);
        }

        ApplyPipelineState(cmd);

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
    const ResourceBindingSet* passBindings,
    void* shadowSRV)
{
    m_drawCalls = 0u;
    InvalidateStateCache();

    void* globalShadowSRV = shadowSRV;

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
            m_context->VSSetShader(static_cast<ID3D11VertexShader*>(shader->vertexShader), nullptr, 0);
            m_context->PSSetShader(static_cast<ID3D11PixelShader*> (shader->pixelShader), nullptr, 0);
            m_context->IASetInputLayout(static_cast<ID3D11InputLayout*>(shader->inputLayout));
            BindFrameConstantsForShader(*shader);

            if (passBindings)
            {
                for (uint32_t i = 0; i < passBindings->constantBufferCount; ++i)
                {
                    const auto& binding = passBindings->constantBuffers[i];
                    if (!binding.enabled || binding.scope != GDXBindingScope::Pass)
                        continue;
                    ID3D11Buffer* buffer = (binding.semantic == GDXShaderConstantBufferSlot::Frame) ? m_frameCB : static_cast<ID3D11Buffer*>(binding.buffer);
                    BindConstantBufferBinding(*shader, binding, buffer);
                }
                for (uint32_t i = 0; i < passBindings->textureCount; ++i)
                {
                    const auto& binding = passBindings->textures[i];
                    if (!binding.enabled || binding.scope != GDXBindingScope::Pass)
                        continue;
                    BindShaderResourceBinding(*shader, binding, texStore,
                        defaultWhiteTex, defaultNormalTex, defaultORMTex, defaultBlackTex,
                        globalShadowSRV);
                }
            }

            m_lastShader = cmd.shader;
            m_lastMaterial = MaterialHandle::Invalid();
        }

        BindSkinningPalette(registry, cmd, *shader);

        // --- Material + Texturen -------------------------------------------
        if (cmd.material != m_lastMaterial)
        {
            for (uint32_t i = 0; i < cmd.resourceBindings.textureCount; ++i)
            {
                const auto& binding = cmd.resourceBindings.textures[i];
                if (!binding.enabled || binding.scope == GDXBindingScope::Pass)
                    continue;
                BindShaderResourceBinding(*shader, binding, texStore,
                    defaultWhiteTex, defaultNormalTex, defaultORMTex, defaultBlackTex,
                    nullptr);
            }
            m_lastMaterial = cmd.material;
        }

        // cbuffer b2: pro Draw aktualisieren, damit receiveShadows pro Entity wirkt
        if (const ConstantBufferBindingDesc* materialBinding =
            cmd.resourceBindings.FindConstantBufferBinding(GDXShaderConstantBufferSlot::Material))
        {
            if (materialBinding->enabled && materialBinding->buffer)
            {
                auto* cb = static_cast<ID3D11Buffer*>(materialBinding->buffer);

                MaterialData drawData = cmd.materialData;
                const bool materialReceive = (drawData.receiveShadows > 0.5f);
                drawData.receiveShadows = (materialReceive && cmd.receiveShadows) ? 1.0f : 0.0f;

                D3D11_MAPPED_SUBRESOURCE m = {};
                if (SUCCEEDED(m_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
                {
                    std::memcpy(m.pData, &drawData, sizeof(MaterialData));
                    m_context->Unmap(cb, 0);
                }

                BindConstantBufferBinding(*shader, *materialBinding, cb);
            }
        }

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
            if (const ConstantBufferBindingDesc* entityBinding = cmd.resourceBindings.FindConstantBufferBinding(GDXShaderConstantBufferSlot::Entity))
                BindConstantBufferBinding(*shader, *entityBinding, m_entityCB);
        }

        ApplyPipelineState(cmd);

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
