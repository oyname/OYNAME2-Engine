#pragma once

// ---------------------------------------------------------------------------
// RenderPassBuilder — Render-Planning-Schicht (Execute-Input-Build)
//
// Verantwortlichkeit: aus vorbereiteten ViewPassData-Objekten backend-neutrale
// ExecuteData bauen (Shadow-Pass, Graphics-Pass, Queues, Presentation).
//
// Kein GPU-Zugriff.  Kein Wissen über DX11/OpenGL/DX12.
// Das Backend bekommt fertige PassDescs und vorbereitete Queues — es entscheidet
// selbst nichts mehr über Pass-Struktur oder Queue-Aufteilung.
//
// Wird von GDXECSRenderer::EndFrame im "Build Frame Execute Inputs"-Task gerufen.
// ---------------------------------------------------------------------------

#include "RenderFramePipeline.h"
#include "RenderPassTargetDesc.h"
#include "PostProcessResource.h"
#include "ResourceStore.h"
#include "GDXRenderTargetResource.h"
#include "GDXTextureResource.h"
#include "RenderQueue.h"
#include "RenderViewData.h"

#include <functional>
#include <vector>
#include <unordered_map>

class IGDXRenderBackend;

namespace RenderPassBuilder
{
    // Callback-Typ für Debug-Culling-Overlays — wird von GDXECSRenderer übergeben,
    // weil AppendDebugVisibleSet auf renderer-eigenem Zustand (m_debugCulling) basiert.
    using DebugAppendFn = std::function<void(
        RenderQueue&          queue,
        const VisibleSet&     set,
        const RenderViewData& view,
        RFG::ViewStats*       stats)>;

    // Kontext für PostProcess-Presentation.
    // Bündelt alles was PrepareMainViewPostProcess braucht um den Intermediate-RT
    // zu erzeugen/recyceln und die Presentation-Data zu befüllen.
    struct PostProcContext
    {
        IGDXRenderBackend*                                               backend          = nullptr;
        ResourceStore<PostProcessResource,     PostProcessTag>*          postProcessStore  = nullptr;
        const std::vector<PostProcessHandle>*                            passOrder         = nullptr;
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>*         rtStore           = nullptr;
        ResourceStore<GDXTextureResource,      TextureTag>*              texStore          = nullptr;
        RenderTargetHandle*                                              mainSceneTarget   = nullptr; // in/out
        std::unordered_map<RenderTargetHandle, RenderTargetHandle>*      rttSceneTargets   = nullptr; // source RT per RTT view
    };

    // --- Einzelne Build-Schritte (werden intern von den Frame-Level-Funktionen gerufen) ---

    // Friert execute.frame ein, setzt presentAfterExecute.
    void ConfigureCommonExecuteInputs(RFG::ViewPassData& view, bool presentAfterExecute);

    // Baut execute.shadowPass.desc aus view.prepared.
    void BuildShadowPassExecuteInput(RFG::ViewPassData& view);

    // Baut execute.graphicsPass.desc aus targetDesc und Flags.
    void BuildGraphicsPassExecuteInput(
        RFG::ViewPassData&        view,
        const RenderPassTargetDesc& targetDesc,
        bool                      appendGraphicsVisibleSet,
        bool                      appendShadowVisibleSet);

    // Befüllt execute.opaqueQueue / execute.alphaQueue / execute.shadowQueue.
    // Ruft debugFn für Debug-Overlays auf wenn entsprechend konfiguriert.
    void BuildExecutionQueues(RFG::ViewPassData& view, const DebugAppendFn& debugFn);

    // --- Frame-Level-Einstiegspunkte ---

    // Prüft ob Post-Process aktiv ist, erzeugt/recycelt Intermediate-RT,
    // leitet Graphics-Pass auf Intermediate um.
    // Gibt true zurück wenn Post-Process aktiv — in diesem Fall ist graphicsPass
    // bereits auf das Intermediate-Target gesetzt.
    bool PrepareMainViewPostProcess(RFG::ViewPassData& view, const PostProcContext& ppCtx);

    // Baut alle Execute-Inputs für den Haupt-View (inkl. PostProcess-Check).
    void BuildMainViewExecuteInputs(
        RFG::ViewPassData&      view,
        const PostProcContext&  ppCtx,
        const DebugAppendFn&    debugFn);

    // Baut alle Execute-Inputs für alle RTT-Views.
    void BuildRTTExecuteInputs(
        std::vector<RFG::ViewPassData>&                           views,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        const PostProcContext&                                    ppCtx,
        const DebugAppendFn&                                      debugFn);

    // Baut Execute-Inputs für den gesamten Frame (RTT + Main).
    void BuildFrameExecuteInputs(
        RFG::PipelineData&                                       pipeline,
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>& rtStore,
        const PostProcContext&                                   ppCtx,
        const DebugAppendFn&                                     debugFn);

} // namespace RenderPassBuilder
