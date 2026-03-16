#pragma once

#include "Registry.h"
#include "FrameData.h"
#include "ICommandList.h"
#include "RenderCommand.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXTextureResource.h"
#include "Components.h"
#include "GDXResourceState.h"
#include "GDXPipelineCache.h"

#include <array>
#include <cstdint>
#include <unordered_map>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
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

    bool Upload(MeshAssetResource& mesh);
    static void Release(MeshAssetResource& mesh);

private:
    bool UploadSubmesh(SubmeshData& cpu, GpuMeshBuffer& gpu);

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
    void ExecuteQueue(
        Registry&                                            registry,
        const ICommandList&                                  queue,
        ResourceStore<MeshAssetResource,  MeshTag>&          meshStore,
        ResourceStore<MaterialResource,   MaterialTag>&      matStore,
        ResourceStore<GDXShaderResource,  ShaderTag>&        shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>&       texStore,
        const ResourceBindingSet*                            passBindings = nullptr,
        void* shadowSRV = nullptr);

    void ExecuteShadowQueue(
        Registry&                                   registry,
        const ICommandList&                         queue,
        ResourceStore<MeshAssetResource, MeshTag>&  meshStore,
        ResourceStore<MaterialResource, MaterialTag>& matStore,
        ResourceStore<GDXShaderResource, ShaderTag>& shaderStore,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        const ResourceBindingSet* passBindings = nullptr);

    uint32_t GetDrawCallCount() const { return m_drawCalls; }

    void TransitionTexture(TextureHandle texture,
                           ResourceState expectedBefore,
                           ResourceState after,
                           const char* debugReason = nullptr);
    void ResetTrackedResourceStates();

private:
    void CreateConstantBuffers();


    struct ResolvedLayoutCacheEntry
    {
        uint32_t layoutKey = 0u;
        bool valid = false;
        std::array<uint8_t, 8> cbVS{};
        std::array<uint8_t, 8> cbPS{};
        std::array<uint8_t, static_cast<size_t>(ShaderResourceSemantic::Count)> texPS{};

        ResolvedLayoutCacheEntry()
        {
            cbVS.fill(255u);
            cbPS.fill(255u);
            texPS.fill(255u);
        }
    };

    void InvalidateStateCache();
    const ResolvedLayoutCacheEntry& GetResolvedLayoutCache(const GDXShaderResource& shader, const ResourceBindingSet* passBindings, const ResourceBindingSet& drawBindings);
    uint64_t MakeGraphicsPipelineCacheKey(ShaderHandle shader, const GDXPipelineStateDesc& state) const noexcept;
    void ApplyPipelineState(const RenderCommand& cmd);
    bool BindVertexStreams(const GpuMeshBuffer& gpu, uint32_t vertexFlags);
    void BindSkinningPalette(Registry& registry, const RenderCommand& cmd, const GDXShaderResource& shader);
    void BindFrameConstantsForShader(const GDXShaderResource& shader);
    void BindEntityConstantsForShader(const GDXShaderResource& shader);
    void BindMaterialTextures(
        const RenderCommand& cmd,
        ResourceStore<GDXTextureResource, TextureTag>& texStore,
        TextureHandle defaultWhite,
        TextureHandle defaultNormal,
        TextureHandle defaultORM,
        TextureHandle defaultBlack);
    void BindConstantBufferBinding(const GDXShaderResource& shader,
                                   const ConstantBufferBindingDesc& binding,
                                   ID3D11Buffer* buffer);
    void BindShaderResourceBinding(const GDXShaderResource& shader,
                                   const ShaderResourceBindingDesc& binding,
                                   ResourceStore<GDXTextureResource, TextureTag>& texStore,
                                   TextureHandle defaultWhite,
                                   TextureHandle defaultNormal,
                                   TextureHandle defaultORM,
                                   TextureHandle defaultBlack,
                                   void* shadowSRV);

    ResourceState GetTrackedTextureState(TextureHandle texture) const;
    void SetTrackedTextureState(TextureHandle texture, ResourceState state);
    void ValidateShaderReadState(TextureHandle texture, const char* debugReason);

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    ID3D11Buffer* m_entityCB = nullptr;
    ID3D11Buffer* m_frameCB  = nullptr;
    ID3D11Buffer* m_skinCB   = nullptr;

    ShaderHandle   m_lastShader   = ShaderHandle::Invalid();
    MaterialHandle m_lastMaterial = MaterialHandle::Invalid();

    // Fixed-function states – nicht owned, werden vom Backend gesetzt
    ID3D11RasterizerState*   m_rsCull         = nullptr;
    ID3D11RasterizerState*   m_rsNoCull       = nullptr;
    ID3D11DepthStencilState* m_dsReadWrite    = nullptr;
    ID3D11DepthStencilState* m_dsReadOnly     = nullptr;
    ID3D11BlendState*        m_blendOpaque    = nullptr;
    ID3D11BlendState*        m_blendAlpha     = nullptr;

    uint32_t m_drawCalls = 0u;
    std::unordered_map<TextureHandle, ResourceState> m_textureStates;
    std::unordered_map<uint32_t, ResolvedLayoutCacheEntry> m_layoutCache;
    GDXDX11PipelineCache m_pipelineCache;
    uint64_t m_lastGraphicsPipelineCacheKey = ~0ull;
    uint64_t m_lastBindingSetKey = ~0ull;
    std::array<ID3D11Buffer*, 16> m_lastVSConstantBuffers{};
    std::array<ID3D11Buffer*, 16> m_lastPSConstantBuffers{};
    std::array<void*, 32> m_lastPSShaderResources{};

public:
    // Wird vom Backend nach CreateRenderStates() aufgerufen
    void SetRasterizerStates(ID3D11RasterizerState* cull, ID3D11RasterizerState* noCull,
                             ID3D11DepthStencilState* readWrite, ID3D11DepthStencilState* readOnly,
                             ID3D11BlendState* opaqueBlend, ID3D11BlendState* alphaBlend)
    {
        m_rsCull = cull;
        m_rsNoCull = noCull;
        m_dsReadWrite = readWrite;
        m_dsReadOnly = readOnly;
        m_blendOpaque = opaqueBlend;
        m_blendAlpha = alphaBlend;
    }
    TextureHandle defaultWhiteTex;
    TextureHandle defaultNormalTex;
    TextureHandle defaultORMTex;
    TextureHandle defaultBlackTex;
};
