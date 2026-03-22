#pragma once

// ---------------------------------------------------------------------------
// GDXDebugCullingRenderer — Debug-Visualisierung für Frustum-Culling.
//
// Extrahiert aus GDXECSRenderer.
// Besitzt Mesh/Material-Handles und Culling-Optionen.
// Rendert AABB-Bounds und Frustum-Kanten als transparente Objekte.
// ---------------------------------------------------------------------------

#include "RenderQueue.h"
#include "RenderViewData.h"
#include "ViewCullingSystem.h"
#include "Handle.h"
#include "ResourceStore.h"
#include "MeshAssetResource.h"
#include "MaterialResource.h"
#include "GDXShaderResource.h"
#include "GDXResourceBinding.h"
#include "GDXResourceState.h"
#include "GDXShaderLayout.h"
#include "GDXTextureSlots.h"
#include "GDXPipelineState.h"
#include "RenderFramePipeline.h"
#include "FrameData.h"
#include "SubmeshData.h"

#include <functional>
#include <vector>
#include <cstdint>

class GDXDebugCullingRenderer
{
public:
    struct RenderContext
    {
        ResourceStore<MeshAssetResource, MeshTag>*       meshStore    = nullptr;
        ResourceStore<MaterialResource,  MaterialTag>*   matStore     = nullptr;
        ResourceStore<GDXShaderResource, ShaderTag>*     shaderStore  = nullptr;
        ShaderHandle                                      defaultShader{};
        uint64_t                                          frameNumber  = 0u;
    };

    struct Options
    {
        bool     enabled                = false;
        bool     drawMainVisibleBounds  = true;
        bool     drawShadowVisibleBounds= true;
        bool     drawRttVisibleBounds   = true;
        bool     drawMainFrustum        = true;
        bool     drawShadowFrustum      = false;
        bool     logStats               = true;
        uint32_t logEveryNFrames        = 60u;
        float    boundsAlpha            = 0.18f;
        float    frustumAlpha           = 0.30f;
    };

    Options options{};

    // Lazy-Init: Mesh + Materialien beim ersten Aufruf erzeugen.
    // Gibt false zurück wenn Ressourcen fehlen.
    bool EnsureResources(
        std::function<MeshHandle(MeshAssetResource)>     uploadMesh,
        std::function<MaterialHandle(MaterialResource)>  createMat,
        ShaderHandle defaultShader);

    void AppendVisibleSet(RenderQueue& queue, const VisibleSet& set,
                          const RenderViewData& view,
                          const RenderContext& ctx,
                          RFG::ViewStats* stats = nullptr);

    void LogStats(const RFG::ViewStats& mainStats,
                  const std::vector<RFG::ViewStats>& rttStats,
                  uint64_t frameNumber) const;

    // Handles für Zugriff aus ECSRenderer (Debug-Mats werden dort noch referenziert)
    MeshHandle     DebugBoxMesh()         const { return m_debugBoxMesh; }
    MaterialHandle MainBoundsMat()        const { return m_mainBoundsMat; }
    MaterialHandle ShadowBoundsMat()      const { return m_shadowBoundsMat; }
    MaterialHandle RttBoundsMat()         const { return m_rttBoundsMat; }
    MaterialHandle MainFrustumMat()       const { return m_mainFrustumMat; }
    MaterialHandle ShadowFrustumMat()     const { return m_shadowFrustumMat; }

private:
    void AppendBounds(RenderQueue& queue, const VisibleRenderCandidate& candidate,
                      MaterialHandle mat, float alpha,
                      const FrameData& frame, const RenderContext& ctx,
                      RFG::ViewStats* stats);

    void AppendFrustum(RenderQueue& queue, const RenderViewData& view,
                       MaterialHandle mat, float alpha,
                       const FrameData& frame, const RenderContext& ctx,
                       RFG::ViewStats* stats);

    MeshHandle     m_debugBoxMesh{};
    MaterialHandle m_mainBoundsMat{};
    MaterialHandle m_shadowBoundsMat{};
    MaterialHandle m_rttBoundsMat{};
    MaterialHandle m_mainFrustumMat{};
    MaterialHandle m_shadowFrustumMat{};
};
