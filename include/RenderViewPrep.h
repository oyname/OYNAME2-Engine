#pragma once

// ---------------------------------------------------------------------------
// RenderViewPrep — Scene/View-Extraction-Schicht
//
// Verantwortlichkeit: backend-neutrale ViewPassData aus FrameSnapshot und ECS bauen.
// Kein GPU-Zugriff, keine Execute-Logik, keine Backend-Entscheidungen.
//
// Wird von GDXECSRenderer::EndFrame aufgerufen, nachdem der FrameSnapshot
// eingefroren wurde (CaptureFrameSnapshot).
// ---------------------------------------------------------------------------

#include "RenderFramePipeline.h"
#include "FrameData.h"
#include "DebugCamera.h"
#include "RenderComponents.h"
#include "GDXRenderTargetResource.h"
#include "ResourceStore.h"
#include "ECS/Registry.h"

#include <vector>

class IGDXRenderBackend;

namespace RenderViewPrep
{
    // Alles was PrepareMainView und PrepareRTTViews brauchen.
    struct Context
    {
        Registry*                                                registry = nullptr;
        ResourceStore<GDXRenderTargetResource, RenderTargetTag>* rtStore  = nullptr;
        IGDXRenderBackend*                                       backend  = nullptr;
        bool                                                     shadowResourcesAvailable = false;
        float                                                    mainViewClearColor[4]    = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    // ---------------------------------------------------------------------------
    // Baut einen vollständigen ViewPassData-Eintrag für den Haupt-View aus dem
    // eingefrorenen FrameSnapshot.  Debug-Kamera überschreibt ggf. View/Proj.
    // ---------------------------------------------------------------------------
    void PrepareMainView(
        const Context&       ctx,
        const FrameData&     frameSnapshot,
        const DebugCamera&   debugCamera,
        RFG::ViewPassData&   outView);

    // ---------------------------------------------------------------------------
    // Iteriert alle CameraComponent+RenderTargetCameraComponent-Entities,
    // baut pro aktivem RTT-View einen ViewPassData-Eintrag.
    // Ruft backend->ExtractLightData für kamerakorrekte Kaskaden auf (CPU-only).
    // ---------------------------------------------------------------------------
    void PrepareRTTViews(
        const Context&                   ctx,
        const FrameData&                 frameSnapshot,
        std::vector<RFG::ViewPassData>&  outViews);

} // namespace RenderViewPrep
