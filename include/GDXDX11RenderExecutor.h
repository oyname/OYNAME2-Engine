#pragma once

#include "ECS/Registry.h"
#include "GDXDX11GpuResources.h"
#include "FrameData.h"
#include "ICommandList.h"
#include "RenderCommand.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "Components.h"
#include "RenderComponents.h"
#include "GDXResourceState.h"
#include "GDXPipelineCache.h"
#include "GDXRecordedCommand.h"

#include <cstdint>
#include <unordered_map>

class GDXDX11GpuRegistry;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;

struct alignas(16) Dx11EntityConstants
{
    float worldMatrix[16];
    float worldInverseTranspose[16];
};
static_assert(sizeof(Dx11EntityConstants) == 128);

struct alignas(16) Dx11FrameConstants
{
    float viewMatrix[16];
    float projMatrix[16];
    float viewProjMatrix[16];
    float cameraPos[4];
    float shadowViewProj[16];
};
static_assert(sizeof(Dx11FrameConstants) == 272);

// CSM-Kaskaden-Daten (b5, PS-only).
// Muss mit CascadeConstants in PixelShader.hlsl übereinstimmen.
struct alignas(16) Dx11CascadeConstants
{
    float    cascadeViewProj[4][16];  // 4 Kaskaden-Matrizen
    float    cascadeSplits[4];        // View-Space far-depths pro Kaskade
    uint32_t cascadeCount;
    float    _pad[3];
};
static_assert(sizeof(Dx11CascadeConstants) == 4 * 64 + 32);

struct alignas(16) Dx11ShadowPassInfoConstants
{
    uint32_t currentCascade;
    float    _pad[3];
};
static_assert(sizeof(Dx11ShadowPassInfoConstants) == 16);

struct alignas(16) Dx11SkinConstants
{
    float boneMatrices[SkinComponent::MaxBones][16];
};
static_assert(sizeof(Dx11SkinConstants) == SkinComponent::MaxBones * 64);

class GDXDX11MeshUploader
{
public:
    GDXDX11MeshUploader(ID3D11Device* device, ID3D11DeviceContext* context)
        : m_device(device), m_context(context) {}

    bool Upload(MeshHandle handle, MeshAssetResource& mesh, GDXDX11GpuRegistry& registry);
    static void Release(MeshAssetResource& mesh);

private:
    bool UploadSubmesh(SubmeshData& cpu, DX11MeshGpu& gpu);

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
};

class GDXDX11RenderExecutor
{
public:
    struct InitParams
    {
        ID3D11Device*        device  = nullptr;
        ID3D11DeviceContext* context = nullptr;
    };

    bool Init(const InitParams& p);
    void Shutdown();

    void UpdateFrameConstants(const FrameData& frame);
    void UpdateCascadeConstants(const FrameData& frame);
    void UpdateShadowPassInfo(uint32_t currentCascade);
    void ExecuteQueue(
        Registry&                                            registry,
        const ICommandList&                                  queue,
        ResourceStore<MeshAssetResource,  MeshTag>&          meshStore,
        ResourceStore<MaterialResource,   MaterialTag>&      matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>&        shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>&       texStore,
        GDXDX11GpuRegistry&                                  gpuRegistry,
        ID3D11ShaderResourceView*                            shadowSRV = nullptr);

    void ExecuteShadowQueue(
        Registry&                                   registry,
        const ICommandList&                         queue,
        ResourceStore<MeshAssetResource, MeshTag>&  meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        GDXDX11GpuRegistry&                           gpuRegistry);

    uint32_t GetDrawCallCount() const { return m_drawCalls; }

    void TransitionTexture(TextureHandle texture,
                           ResourceState expectedBefore,
                           ResourceState after,
                           const char* debugReason = nullptr);
    void ResetTrackedResourceStates();
    void ForgetTextureState(TextureHandle texture);
    size_t DebugTrackedTextureStateCount() const;
    size_t DebugPipelineCacheSize() const noexcept;
    size_t DebugLayoutCacheSize() const noexcept;
    void BindResolvedBindingGroup(
        const GDXPipelineLayoutDesc& pipelineLayout,
        const GDXRecordedBindingGroupData& bindings,
        const std::array<ID3D11ShaderResourceView*, GDXRecordedBindingGroupData::MaxTextureBindings>& explicitSrvs,
        ID3D11Buffer* externalConstantBuffer = nullptr);
    bool ValidateBindingGroupForLayout(
        const GDXPipelineLayoutDesc& pipelineLayout,
        const GDXRecordedBindingGroupData& bindings) const noexcept;

    bool BindPipelineCommand(
        ShaderHandle shaderHandle,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        GDXDX11GpuRegistry& gpuRegistry,
        ID3D11ShaderResourceView* shadowSRV = nullptr,
        bool shadowPass = false);
    bool BindVertexBufferCommand(
        MeshHandle mesh,
        uint32_t submeshIndex,
        uint32_t vertexFlags,
        GDXDX11GpuRegistry& gpuRegistry);
    bool BindIndexBufferCommand(
        MeshHandle mesh,
        uint32_t submeshIndex,
        GDXDX11GpuRegistry& gpuRegistry);
    void DrawCommand(uint32_t vertexCount, uint32_t vertexStart = 0u);
    void DrawIndexedCommand(uint32_t indexCount = 0u, uint32_t startIndex = 0u, int32_t baseVertex = 0);
    void ResetCommandBindings();

private:
    void CreateConstantBuffers();
    void ApplyPipelineState(const GDXRecordedDrawItem& item);
    void ApplyPrimitiveTopology(const GDXRecordedDrawItem& item);
    bool BindVertexStreams(const DX11MeshGpu& gpu, uint32_t vertexFlags);
    void BindSkinningPalette(Registry& registry, const GDXRecordedDrawItem& item, const GDXShaderResource& shader);
    void BindFrameConstantsForShader(const GDXShaderResource& shader);
    void BindEntityConstantsForShader(const GDXShaderResource& shader);
    ID3D11ShaderResourceView* ResolveTextureSRVForBinding(
        const GDXRecordedTextureBinding& binding,
        GDXDX11GpuRegistry& gpuRegistry,
        ID3D11ShaderResourceView* shadowSRV,
        TextureHandle defaultWhite,
        TextureHandle defaultNormal,
        TextureHandle defaultORM,
        TextureHandle defaultBlack);
    ID3D11Buffer* ResolveConstantBufferForBinding(
        const GDXRecordedConstantBufferBinding& binding,
        const GDXRecordedDrawItem& item,
        GDXDX11GpuRegistry& gpuRegistry,
        bool applyReceiveShadowOverride);
    void BindTextureBinding(
        const GDXPipelineLayoutDesc& pipelineLayout,
        const GDXRecordedTextureBinding& binding,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        GDXDX11GpuRegistry& gpuRegistry,
        ID3D11ShaderResourceView* shadowSRV,
        TextureHandle defaultWhite,
        TextureHandle defaultNormal,
        TextureHandle defaultORM,
        TextureHandle defaultBlack);
    void BindConstantBufferBinding(
        const GDXPipelineLayoutDesc& pipelineLayout,
        const GDXRecordedConstantBufferBinding& binding,
        const GDXRecordedDrawItem& item,
        GDXDX11GpuRegistry& gpuRegistry,
        bool applyReceiveShadowOverride);
    void BindBindingGroup(
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
        bool applyReceiveShadowOverride);
    void ApplyBindingsForGroup(
        const GDXPipelineLayoutDesc& pipelineLayout,
        const GDXRecordedDrawItem& item,
        const GDXRecordedBindingGroupData& groupData,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        GDXDX11GpuRegistry& gpuRegistry,
        ResourceBindingScope scope,
        bool applyReceiveShadowOverride);
    void BindExplicitPassResources(
        const GDXPipelineLayoutDesc& pipelineLayout,
        const GDXRecordedDrawItem& item,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        GDXDX11GpuRegistry& gpuRegistry,
        ID3D11ShaderResourceView* shadowSRV);
    void BuildRecordedStreamFromQueue(const ICommandList& queue, GDXRecordedCommandStream& outStream);
    void ExecuteRecordedStream(
        Registry& registry,
        const GDXRecordedCommandStream& stream,
        ResourceStore<MeshAssetResource,  MeshTag>& meshStore,
        ResourceStore<MaterialResource,   MaterialTag>& matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>& shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        GDXDX11GpuRegistry& gpuRegistry,
        ID3D11ShaderResourceView* shadowSRV,
        bool shadowPass);
    void ResetScopeCaches();
    const GDXShaderLayout& GetCachedShaderLayout(ShaderHandle shaderHandle, const GDXShaderResource& shader);
    const GDXPipelineLayoutDesc& GetCachedPipelineLayout(ShaderHandle shaderHandle, const GDXShaderResource& shader);

    ResourceState GetTrackedTextureState(TextureHandle texture) const;
    void SetTrackedTextureState(TextureHandle texture, ResourceState state);
    void ValidateShaderReadState(TextureHandle texture, const char* debugReason);

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ID3D11Buffer* m_entityCB  = nullptr;
    ID3D11Buffer* m_frameCB   = nullptr;
    ID3D11Buffer* m_skinCB          = nullptr;
    ID3D11Buffer* m_cascadeCB       = nullptr;
    ID3D11Buffer* m_shadowPassInfoCB = nullptr;

    ShaderHandle   m_lastShader   = ShaderHandle::Invalid();
    uint32_t       m_lastAppliedPipelineKey = 0u;
    GDXDX11PipelineCache m_pipelineCache{};
    GDXDX11ShaderLayoutCache m_layoutCache{};
    GDXDX11BindingCache m_bindingCache{};

    // Fixed-function states – nicht owned, werden vom Backend gesetzt
    ID3D11RasterizerState*   m_rsCull         = nullptr;
    ID3D11RasterizerState*   m_rsNoCull       = nullptr;
    ID3D11DepthStencilState* m_dsReadWrite    = nullptr;
    ID3D11DepthStencilState* m_dsReadOnly     = nullptr;
    ID3D11DepthStencilState* m_dsNoTest       = nullptr;
    ID3D11BlendState*        m_blendOpaque    = nullptr;
    ID3D11BlendState*        m_blendAlpha     = nullptr;

    uint32_t m_drawCalls       = 0u;
    std::unordered_map<TextureHandle, ResourceState> m_textureStates;
    DX11MeshGpu* m_boundMeshGpu = nullptr;
    ShaderHandle m_boundShaderHandle = ShaderHandle::Invalid();

public:
    // Wird vom Backend nach CreateRenderStates() aufgerufen
    void SetRasterizerStates(ID3D11RasterizerState* cull, ID3D11RasterizerState* noCull,
                             ID3D11DepthStencilState* readWrite, ID3D11DepthStencilState* readOnly,
                             ID3D11DepthStencilState* noTest,
                             ID3D11BlendState* opaqueBlend, ID3D11BlendState* alphaBlend)
    {
        m_rsCull = cull;
        m_rsNoCull = noCull;
        m_dsReadWrite = readWrite;
        m_dsReadOnly = readOnly;
        m_dsNoTest = noTest;
        m_blendOpaque = opaqueBlend;
        m_blendAlpha = alphaBlend;
    }
    TextureHandle defaultWhiteTex;
    TextureHandle defaultNormalTex;
    TextureHandle defaultORMTex;
    TextureHandle defaultBlackTex;
};
