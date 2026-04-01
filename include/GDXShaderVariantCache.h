#pragma once

// ---------------------------------------------------------------------------
// GDXShaderVariantCache — Shader-Varianten-System.
// ---------------------------------------------------------------------------

#include "ShaderVariant.h"
#include "GDXShaderResource.h"
#include "GDXShaderLayout.h"
#include "GDXVertexFlags.h"
#include "Handle.h"
#include "ResourceStore.h"
#include "SubmeshData.h"
#include "MaterialResource.h"
#include "RenderCommand.h"

#include <unordered_map>
#include <cstddef>
#include <string>

class IGDXRenderBackend;

// ---------------------------------------------------------------------------
// ShaderPathConfig — konfiguriert welche Shader-Dateien + Flags die Engine
// für ihre internen Default-Varianten (Main + Shadow) verwendet.
//
// Defaults entsprechen den Standard-Engine-Shadern.
// Vor Initialize() setzen um eigene Shader als Engine-Default zu verwenden.
//
// Beispiel:
//   ShaderPathConfig cfg;
//   cfg.mainVS    = L"VertexShader_MyGame.hlsl";
//   cfg.mainPS    = L"PixelShader_MyGame.hlsl";
//   cfg.mainFlags = GDX_VERTEX_POSITION | GDX_VERTEX_NORMAL | GDX_VERTEX_TEX1;
//   renderer.GetShaderCache().SetConfig(cfg);
// ---------------------------------------------------------------------------
struct ShaderPathConfig
{
    std::wstring mainVS    = L"VertexShader.hlsl";
    std::wstring mainPS    = L"PixelShader.hlsl";
    uint32_t     mainFlags = GDX_VERTEX_DEFAULT;

    std::wstring shadowVS    = L"ShadowVertexShader.hlsl";
    std::wstring shadowPS    = L"ShadowPixelShader.hlsl";
    uint32_t     shadowFlags = GDX_VERTEX_POSITION;
};

class GDXShaderVariantCache
{
public:
    void Init(IGDXRenderBackend* backend,
              ResourceStore<GDXShaderResource, ShaderTag>* shaderStore);

    void SetConfig(const ShaderPathConfig& config) { m_config = config; }
    const ShaderPathConfig& GetConfig() const { return m_config; }

    ShaderHandle LoadShader(const std::wstring& vsFile,
                            const std::wstring& psFile,
                            uint32_t vertexFlags);
    ShaderHandle LoadShader(const std::wstring& vsFile,
                            const std::wstring& psFile,
                            uint32_t vertexFlags,
                            const GDXShaderLayout& layout);

    ShaderHandle Resolve(RenderPass pass,
                         const SubmeshData& submesh,
                         const MaterialResource& mat);

    size_t DebugVariantCount() const noexcept { return m_cache.size(); }

    void Clear()
    {
        m_cache.clear();
        m_backend     = nullptr;
        m_shaderStore = nullptr;
    }

private:
    ShaderHandle LoadInternal(const std::wstring& vsFile,
                              const std::wstring& psFile,
                              uint32_t vertexFlags,
                              const std::wstring& debugName,
                              const GDXShaderLayout* customLayout = nullptr);

    ShaderVariantKey BuildKey(RenderPass pass,
                              const SubmeshData& submesh,
                              const MaterialResource& mat) const;
    ShaderVariantKey NormalizeKey(const ShaderVariantKey& key) const;
    ShaderHandle     CreateVariant(const ShaderVariantKey& key);

    IGDXRenderBackend*                           m_backend     = nullptr;
    ResourceStore<GDXShaderResource, ShaderTag>* m_shaderStore = nullptr;
    ShaderPathConfig                             m_config{};

    std::unordered_map<ShaderVariantKey, ShaderHandle, ShaderVariantKeyHash> m_cache;

};
