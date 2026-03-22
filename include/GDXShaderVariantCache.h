#pragma once

// ---------------------------------------------------------------------------
// GDXShaderVariantCache — Shader-Varianten-System.
//
// Extrahiert aus GDXECSRenderer.
//
// Verantwortlichkeiten:
//   - Shader laden (LoadShaderInternal)
//   - Default-Shader registrieren (LoadDefaults)
//   - Varianten-Keys bauen und normalisieren
//   - Varianten cachen und auf Anfrage erzeugen
// ---------------------------------------------------------------------------

#include "ShaderVariant.h"
#include "GDXShaderResource.h"
#include "GDXShaderLayout.h"
#include "Handle.h"
#include "ResourceStore.h"
#include "SubmeshData.h"
#include "MaterialResource.h"
#include "RenderCommand.h"

#include <unordered_map>
#include <string>

class IGDXRenderBackend;

class GDXShaderVariantCache
{
public:
    // Backend + ShaderStore werden nicht besessen — nur referenziert.
    void Init(IGDXRenderBackend* backend,
              ResourceStore<GDXShaderResource, ShaderTag>* shaderStore);

    // Standard-Shader laden (Main + Shadow). Gibt false zurück wenn Main fehlt.
    bool LoadDefaults();

    // Manuell geladene Shader (aus vsFile/psFile).
    ShaderHandle LoadShader(const std::wstring& vsFile,
                            const std::wstring& psFile,
                            uint32_t vertexFlags);
    ShaderHandle LoadShader(const std::wstring& vsFile,
                            const std::wstring& psFile,
                            uint32_t vertexFlags,
                            const GDXShaderLayout& layout);

    // Variante per Pass+Submesh+Material auflösen — cached oder neu erzeugt.
    ShaderHandle Resolve(RenderPass pass,
                         const SubmeshData& submesh,
                         const MaterialResource& mat);

    ShaderHandle DefaultShader() const { return m_defaultShader; }
    ShaderHandle ShadowShader()  const { return m_shadowShader;  }

    // Leert den Varianten-Cache und nullt Backend/Store-Zeiger.
    // Muss vor backend->Shutdown() aufgerufen werden — danach sind
    // die gecachten ShaderHandles stale und dürfen nicht mehr aufgelöst werden.
    void Clear()
    {
        m_cache.clear();
        m_backend     = nullptr;
        m_shaderStore = nullptr;
        m_defaultShader = ShaderHandle::Invalid();
        m_shadowShader  = ShaderHandle::Invalid();
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

    IGDXRenderBackend*                          m_backend    = nullptr;
    ResourceStore<GDXShaderResource, ShaderTag>* m_shaderStore = nullptr;

    std::unordered_map<ShaderVariantKey, ShaderHandle, ShaderVariantKeyHash> m_cache;

    ShaderHandle m_defaultShader;
    ShaderHandle m_shadowShader;
};
