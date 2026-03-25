#pragma once

// ---------------------------------------------------------------------------
// GDXDX11GpuResources.h — DX11-seitige GPU-Objekte.
//
// AUSSCHLIESSLICH im Backend sichtbar. Kein Frontend-Code includet diese Datei.
//
// Muster:
//   Frontend hält GDXTextureResource  (Metadaten, kein GPU-Objekt)
//   Backend  hält DX11TextureGpu      (ID3D11Texture2D* + SRV*)
//   Zugriff: m_gpuRegistry.GetTexture(handle)
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

#include "Handle.h"
#include "GDXTextureResource.h"
#include <unordered_map>
#include <cstdint>

// ---------------------------------------------------------------------------
// Texture
// ---------------------------------------------------------------------------
struct DX11TextureGpu
{
    ID3D11Texture2D*          texture = nullptr;
    ID3D11ShaderResourceView* srv     = nullptr;

    void Release()
    {
        if (srv)     { srv->Release();     srv     = nullptr; }
        if (texture) { texture->Release(); texture = nullptr; }
    }
};

// ---------------------------------------------------------------------------
// RenderTarget
// ---------------------------------------------------------------------------
struct DX11RenderTargetGpu
{
    ID3D11Texture2D*          colorTexture = nullptr;
    ID3D11RenderTargetView*   rtv          = nullptr;
    ID3D11ShaderResourceView* srv          = nullptr;
    ID3D11Texture2D*          depthTexture = nullptr;
    ID3D11DepthStencilView*   dsv          = nullptr;
    ID3D11ShaderResourceView* depthSrv     = nullptr;

    void Release()
    {
        if (srv)          { srv->Release();          srv          = nullptr; }
        if (rtv)          { rtv->Release();          rtv          = nullptr; }
        if (colorTexture) { colorTexture->Release(); colorTexture = nullptr; }
        if (depthSrv)     { depthSrv->Release();     depthSrv     = nullptr; }
        if (dsv)          { dsv->Release();          dsv          = nullptr; }
        if (depthTexture) { depthTexture->Release(); depthTexture = nullptr; }
    }
};

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------
struct DX11ShaderGpu
{
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader*  pixelShader  = nullptr;
    ID3D11InputLayout*  inputLayout  = nullptr;

    bool IsValid() const noexcept
    {
        return vertexShader && pixelShader && inputLayout;
    }

    void Release()
    {
        if (inputLayout)  { inputLayout->Release();  inputLayout  = nullptr; }
        if (pixelShader)  { pixelShader->Release();  pixelShader  = nullptr; }
        if (vertexShader) { vertexShader->Release(); vertexShader = nullptr; }
    }
};

// ---------------------------------------------------------------------------
// Mesh — separate Vertex-Streams (ein Buffer pro Semantik)
// ---------------------------------------------------------------------------
struct DX11MeshGpu
{
    ID3D11Buffer* positionBuffer   = nullptr;
    ID3D11Buffer* normalBuffer     = nullptr;
    ID3D11Buffer* colorBuffer      = nullptr;
    ID3D11Buffer* uv1Buffer        = nullptr;
    ID3D11Buffer* uv2Buffer        = nullptr;
    ID3D11Buffer* tangentBuffer    = nullptr;
    ID3D11Buffer* boneIndexBuffer  = nullptr;
    ID3D11Buffer* boneWeightBuffer = nullptr;
    ID3D11Buffer* indexBuffer      = nullptr;

    uint32_t stridePosition   = 0u;
    uint32_t strideNormal     = 0u;
    uint32_t strideColor      = 0u;
    uint32_t strideUV1        = 0u;
    uint32_t strideUV2        = 0u;
    uint32_t strideTangent    = 0u;
    uint32_t strideBoneIndex  = 0u;
    uint32_t strideBoneWeight = 0u;

    uint32_t vertexCount = 0u;
    uint32_t indexCount  = 0u;
    bool     ready       = false;

    void Release()
    {
        auto sr = [](ID3D11Buffer*& p) { if (p) { p->Release(); p = nullptr; } };
        sr(positionBuffer);  sr(normalBuffer);   sr(colorBuffer);
        sr(uv1Buffer);       sr(uv2Buffer);      sr(tangentBuffer);
        sr(boneIndexBuffer); sr(boneWeightBuffer); sr(indexBuffer);
    }
};

// ---------------------------------------------------------------------------
// Material — nur der cbuffer
// ---------------------------------------------------------------------------
struct DX11MaterialGpu
{
    ID3D11Buffer* constantBuffer = nullptr;

    void Release()
    {
        if (constantBuffer) { constantBuffer->Release(); constantBuffer = nullptr; }
    }
};

// ---------------------------------------------------------------------------
// PostProcess
// ---------------------------------------------------------------------------
struct DX11PostProcessGpu
{
    ID3D11VertexShader*       vertexShader   = nullptr;
    ID3D11PixelShader*        pixelShader    = nullptr;
    ID3D11Buffer*             constantBuffer = nullptr;

    void Release()
    {
        if (constantBuffer) { constantBuffer->Release(); constantBuffer = nullptr; }
        if (pixelShader)    { pixelShader->Release();    pixelShader    = nullptr; }
        if (vertexShader)   { vertexShader->Release();   vertexShader   = nullptr; }
    }
};

// PostProcess ping-pong surface (intermediate render target for chain)
struct DX11PostProcessSurfaceGpu
{
    ID3D11Texture2D*          texture = nullptr;
    ID3D11RenderTargetView*   rtv     = nullptr;
    ID3D11ShaderResourceView* srv     = nullptr;
    uint32_t         width  = 0u;
    uint32_t         height = 0u;
    GDXTextureFormat format = GDXTextureFormat::Unknown;

    bool IsReady() const noexcept { return rtv != nullptr && srv != nullptr; }

    void Release()
    {
        if (srv)     { srv->Release();     srv     = nullptr; }
        if (rtv)     { rtv->Release();     rtv     = nullptr; }
        if (texture) { texture->Release(); texture = nullptr; }
        width = height = 0u;
    }
};

// ---------------------------------------------------------------------------
// GDXDX11GpuRegistry — zentrales GPU-Ressourcen-Verzeichnis im Backend.
//
// Handle → GPU-Objekt Lookup. Ownership liegt hier.
// Alles was Released wird, wird aus der Map entfernt.
// ---------------------------------------------------------------------------
class GDXDX11GpuRegistry
{
public:
    // --- Textures ---
    DX11TextureGpu* GetTexture(TextureHandle h)
    {
        auto it = m_textures.find(h);
        return it != m_textures.end() ? &it->second : nullptr;
    }
    void SetTexture(TextureHandle h, DX11TextureGpu gpu)
    {
        m_textures[h] = gpu;
    }
    void ReleaseTexture(TextureHandle h)
    {
        auto it = m_textures.find(h);
        if (it != m_textures.end()) { it->second.Release(); m_textures.erase(it); }
    }
    void ReleaseAllTextures()
    {
        for (auto& [h, gpu] : m_textures) gpu.Release();
        m_textures.clear();
    }

    // --- RenderTargets ---
    DX11RenderTargetGpu* GetRenderTarget(RenderTargetHandle h)
    {
        auto it = m_renderTargets.find(h);
        return it != m_renderTargets.end() ? &it->second : nullptr;
    }
    void SetRenderTarget(RenderTargetHandle h, DX11RenderTargetGpu gpu)
    {
        m_renderTargets[h] = gpu;
    }
    void ReleaseRenderTarget(RenderTargetHandle h)
    {
        auto it = m_renderTargets.find(h);
        if (it != m_renderTargets.end()) { it->second.Release(); m_renderTargets.erase(it); }
    }
    void ReleaseAllRenderTargets()
    {
        for (auto& [h, gpu] : m_renderTargets) gpu.Release();
        m_renderTargets.clear();
    }

    // --- Shaders ---
    DX11ShaderGpu* GetShader(ShaderHandle h)
    {
        auto it = m_shaders.find(h);
        return it != m_shaders.end() ? &it->second : nullptr;
    }
    void SetShader(ShaderHandle h, DX11ShaderGpu gpu) { m_shaders[h] = gpu; }
    void ReleaseShader(ShaderHandle h)
    {
        auto it = m_shaders.find(h);
        if (it != m_shaders.end()) { it->second.Release(); m_shaders.erase(it); }
    }
    void ReleaseAllShaders()
    {
        for (auto& [h, gpu] : m_shaders) gpu.Release();
        m_shaders.clear();
    }

    // --- Meshes (key = MeshHandle.value * 1000 + submeshIndex) ---
    DX11MeshGpu* GetMesh(MeshHandle h, uint32_t submesh = 0u)
    {
        auto it = m_meshes.find(MeshKey(h, submesh));
        return it != m_meshes.end() ? &it->second : nullptr;
    }
    void SetMesh(MeshHandle h, uint32_t submesh, DX11MeshGpu gpu)
    {
        m_meshes[MeshKey(h, submesh)] = gpu;
    }
    void ReleaseMesh(MeshHandle h, uint32_t submeshCount)
    {
        for (uint32_t i = 0; i < submeshCount; ++i)
        {
            auto it = m_meshes.find(MeshKey(h, i));
            if (it != m_meshes.end()) { it->second.Release(); m_meshes.erase(it); }
        }
    }
    void ReleaseAllMeshes()
    {
        for (auto& [k, gpu] : m_meshes) gpu.Release();
        m_meshes.clear();
    }

    // --- Materials ---
    DX11MaterialGpu* GetMaterial(MaterialHandle h)
    {
        auto it = m_materials.find(h);
        return it != m_materials.end() ? &it->second : nullptr;
    }
    void SetMaterial(MaterialHandle h, DX11MaterialGpu gpu) { m_materials[h] = gpu; }
    void ReleaseMaterial(MaterialHandle h)
    {
        auto it = m_materials.find(h);
        if (it != m_materials.end()) { it->second.Release(); m_materials.erase(it); }
    }
    void ReleaseAllMaterials()
    {
        for (auto& [h, gpu] : m_materials) gpu.Release();
        m_materials.clear();
    }

    // --- PostProcess passes ---
    DX11PostProcessGpu* GetPostProcess(PostProcessHandle h)
    {
        auto it = m_postProcess.find(h);
        return it != m_postProcess.end() ? &it->second : nullptr;
    }
    void SetPostProcess(PostProcessHandle h, DX11PostProcessGpu gpu) { m_postProcess[h] = gpu; }
    void ReleasePostProcess(PostProcessHandle h)
    {
        auto it = m_postProcess.find(h);
        if (it != m_postProcess.end()) { it->second.Release(); m_postProcess.erase(it); }
    }
    void ReleaseAllPostProcess()
    {
        for (auto& [h, gpu] : m_postProcess) gpu.Release();
        m_postProcess.clear();
    }

    void ReleaseAll()
    {
        ReleaseAllTextures();
        ReleaseAllRenderTargets();
        ReleaseAllShaders();
        ReleaseAllMeshes();
        ReleaseAllMaterials();
        ReleaseAllPostProcess();
    }

private:
    static uint64_t MeshKey(MeshHandle h, uint32_t submesh)
    {
        return (static_cast<uint64_t>(h.value) << 16u) | static_cast<uint64_t>(submesh);
    }

    std::unordered_map<TextureHandle,      DX11TextureGpu>      m_textures;
    std::unordered_map<RenderTargetHandle, DX11RenderTargetGpu> m_renderTargets;
    std::unordered_map<ShaderHandle,       DX11ShaderGpu>       m_shaders;
    std::unordered_map<uint64_t,           DX11MeshGpu>         m_meshes;
    std::unordered_map<MaterialHandle,     DX11MaterialGpu>     m_materials;
    std::unordered_map<PostProcessHandle,  DX11PostProcessGpu>  m_postProcess;
};
