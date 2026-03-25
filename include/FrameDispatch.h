#pragma once
// ---------------------------------------------------------------------------
// FrameDispatch — per-Frame-Dispatch-Daten mit Heap-Lifetime
//
// Besitzmodell:
//   FrameDispatch gehört dem Renderer als Member (analog zu m_renderPipeline).
//   Er wird einmal pro Frame über Fill() befüllt und lebt für die gesamte
//   Frame-Dauer — nicht nur für den Callstack von EndFrame.
//
//   Das löst das Stack-Lifetime-Problem der bisherigen lokalen Context-Variablen
//   in EndFrame und erlaubt Execute(&m_jobSystem) statt Execute(nullptr).
//
// Invariante:
//   Fill() muss vor jedem Execute() aufgerufen worden sein.
//   Reset() gibt `resolveShader` und `debugAppend` frei (std::function = Heap).
//
// Kein Wissen über GDXECSRenderer, DX11 oder sonstige Engine-Internals.
// Nur Context-Structs und Funktions-Callbacks.
// ---------------------------------------------------------------------------


#include "RenderViewPrep.h"
#include "RenderPassBuilder.h"
#include "CullGatherSystem.h"
#include "RenderFramePipeline.h"
#include "GDXRenderFrameGraph.h"

struct FrameDispatch
{
    // -- View Preparation (Layer 1) --
    RenderViewPrep::Context      viewPrep{};

    // -- Cull + Gather --
    CullGather::Context          cullGather{};

    // -- Execute-Input Build (Layer 2) --
    RenderPassBuilder::PostProcContext postProc{};
    RenderPassBuilder::DebugAppendFn   debugAppend;     // captures renderer-owned debug state

    // -- FrameGraph Execute (Layer 3) --
    RFG::ExecContext             execCtx{};

    // -- FrameGraph Build --
    GDXRenderFrameGraph::BuildContext  fgBuild{};

    // ---------------------------------------------------------------------------
    // Gibt alle std::function-Captures frei (resolveShader, debugAppend).
    // Zeiger in den Context-Structs bleiben unverändert — sie zeigen auf
    // Renderer-Member die per Frame-Start noch gültig sind.
    // ---------------------------------------------------------------------------
    void Reset()
    {
        viewPrep     = {};
        cullGather   = {};
        postProc     = {};
        debugAppend  = nullptr;
        execCtx      = {};
        fgBuild      = {};
    }

    // ---------------------------------------------------------------------------
    // Validierung — prüft ob alle kritischen Zeiger gesetzt sind.
    // Gibt false zurück und beschreibt das erste fehlende Feld.
    // Geeignet für Tests und Debug-Asserts.
    // ---------------------------------------------------------------------------
    bool IsValid(const char** outReason = nullptr) const
    {
        auto fail = [&](const char* reason) -> bool
        {
            if (outReason) *outReason = reason;
            return false;
        };

        if (!viewPrep.registry)                 return fail("viewPrep.registry");
        if (!viewPrep.rtStore)                  return fail("viewPrep.rtStore");
        if (!viewPrep.backend)                  return fail("viewPrep.backend");

        if (!cullGather.registry)               return fail("cullGather.registry");
        if (!cullGather.culling)                return fail("cullGather.culling");
        if (!cullGather.gather)                 return fail("cullGather.gather");
        if (!cullGather.jobSystem)              return fail("cullGather.jobSystem");
        if (!cullGather.meshStore)              return fail("cullGather.meshStore");
        if (!cullGather.matStore)               return fail("cullGather.matStore");
        if (!cullGather.shaderStore)            return fail("cullGather.shaderStore");
        if (!cullGather.rtStore)                return fail("cullGather.rtStore");
        if (!cullGather.resolveShader)          return fail("cullGather.resolveShader");

        if (!postProc.backend)                  return fail("postProc.backend");
        if (!postProc.postProcessStore)         return fail("postProc.postProcessStore");
        if (!postProc.passOrder)                return fail("postProc.passOrder");
        if (!postProc.rtStore)                  return fail("postProc.rtStore");
        if (!postProc.texStore)                 return fail("postProc.texStore");
        if (!postProc.mainSceneTarget)          return fail("postProc.mainSceneTarget");

        if (!execCtx.backend)                   return fail("execCtx.backend");
        if (!execCtx.registry)                  return fail("execCtx.registry");
        if (!execCtx.meshStore)                 return fail("execCtx.meshStore");
        if (!execCtx.matStore)                  return fail("execCtx.matStore");
        if (!execCtx.shaderStore)               return fail("execCtx.shaderStore");
        if (!execCtx.texStore)                  return fail("execCtx.texStore");
        if (!execCtx.rtStore)                   return fail("execCtx.rtStore");
        if (!execCtx.postProcessStore)          return fail("execCtx.postProcessStore");
        if (!execCtx.postProcessPassOrder)      return fail("execCtx.postProcessPassOrder");

        if (!fgBuild.rtStore)                   return fail("fgBuild.rtStore");

        return true;
    }
};
