#include "RenderViewPrep.h"
#include "IGDXRenderBackend.h"
#include "CameraSystem.h"
#include "RenderPassTargetDesc.h"

// ---------------------------------------------------------------------------
// Lokale Hilfsfunktionen — nur innerhalb dieser Übersetzungseinheit sichtbar.
// ---------------------------------------------------------------------------
namespace
{
    bool BuildFrameDataFromWorldAndCamera(
        const WorldTransformComponent& wt,
        const CameraComponent&         cam,
        FrameData&                     frame)
    {
        CameraSystem::BuildFrameData(wt, cam, frame);
        return true;
    }

    bool BuildFrameDataFromCameraEntityWithOverride(
        Registry&             registry,
        EntityID              cameraEntity,
        const CameraComponent& cameraOverride,
        FrameData&             frame)
    {
        const auto* wt = registry.Get<WorldTransformComponent>(cameraEntity);
        if (!wt) return false;
        return BuildFrameDataFromWorldAndCamera(*wt, cameraOverride, frame);
    }
} // anonymous namespace

// ---------------------------------------------------------------------------
namespace RenderViewPrep
{

void PrepareMainView(
    const Context&     ctx,
    const FrameData&   frameSnapshot,
    const DebugCamera& debugCamera,
    RFG::ViewPassData& outView)
{
    outView.Reset();

    outView.prepared.frame   = frameSnapshot;
    outView.realCameraFrame  = frameSnapshot; // echte Kamera — immer unverändert

    // Debug-Kamera überschreibt View/Proj des Main-Views wenn aktiviert.
    // Shadow, RTT und Gameplay laufen weiter mit der echten frameSnapshot-Kamera.
    if (debugCamera.IsEnabled())
        debugCamera.OverrideFrameData(outView.prepared.frame);

    outView.prepared.graphicsView = {};
    outView.prepared.graphicsView.type                    = RenderViewType::Main;
    outView.prepared.graphicsView.frame                   = outView.prepared.frame;
    outView.prepared.graphicsView.renderTarget            = RenderTargetHandle{};
    outView.prepared.graphicsView.forbiddenShaderReadTexture = TextureHandle{};
    outView.prepared.graphicsView.gatherOpaque            = true;
    outView.prepared.graphicsView.gatherTransparent       = true;
    outView.prepared.graphicsView.gatherShadows           = false;
    outView.prepared.graphicsView.skipSelfReferentialDraws = false;
    outView.prepared.graphicsView.visibilityLayerMask     = outView.prepared.frame.cullMask;
    outView.prepared.graphicsView.shadowCasterLayerMask   = outView.prepared.frame.shadowCasterMask;

    outView.prepared.gatherOptions = {};
    outView.prepared.gatherOptions.gatherOpaque              = true;
    outView.prepared.gatherOptions.gatherTransparent         = true;
    outView.prepared.gatherOptions.gatherShadows             = false;
    outView.prepared.gatherOptions.skipSelfReferentialDraws  = false;
    outView.prepared.gatherOptions.forbiddenShaderReadTexture = TextureHandle{};
    outView.prepared.gatherOptions.visibilityLayerMask       = outView.prepared.frame.cullMask;
    outView.prepared.gatherOptions.shadowCasterLayerMask     = outView.prepared.frame.shadowCasterMask;

    outView.prepared.clearDesc              = {};
    outView.prepared.clearDesc.clearColor[0] = ctx.mainViewClearColor[0];
    outView.prepared.clearDesc.clearColor[1] = ctx.mainViewClearColor[1];
    outView.prepared.clearDesc.clearColor[2] = ctx.mainViewClearColor[2];
    outView.prepared.clearDesc.clearColor[3] = ctx.mainViewClearColor[3];
    outView.prepared.graphicsTargetDesc = RenderPassTargetDesc::Backbuffer(
        outView.prepared.frame.viewportWidth,
        outView.prepared.frame.viewportHeight);

    outView.prepared.shadowEnabled =
        outView.prepared.frame.hasShadowPass &&
        ctx.shadowResourcesAvailable;

    outView.prepared.shadowView = {};
    if (outView.prepared.shadowEnabled)
    {
        outView.prepared.shadowView                   = outView.prepared.graphicsView;
        outView.prepared.shadowView.type              = RenderViewType::Shadow;
        outView.prepared.shadowView.gatherOpaque      = false;
        outView.prepared.shadowView.gatherTransparent = false;
        outView.prepared.shadowView.gatherShadows     = true;
    }
}

void PrepareRTTViews(
    const Context&                  ctx,
    const FrameData&                frameSnapshot,
    std::vector<RFG::ViewPassData>& outViews)
{
    outViews.clear();

    if (!ctx.registry || !ctx.rtStore) return;

    ctx.registry->View<CameraComponent, RenderTargetCameraComponent>(
        [&](EntityID entity, CameraComponent& cam, RenderTargetCameraComponent& rtCam)
        {
            if (!ctx.backend || !rtCam.enabled || !rtCam.target.IsValid())
                return;

            GDXRenderTargetResource* rt = ctx.rtStore->Get(rtCam.target);
            if (!rt || !rt->ready)
                return;

            CameraComponent cameraForView = cam;
            if (rtCam.autoAspectFromTarget && rt->height > 0u)
                cameraForView.aspectRatio = static_cast<float>(rt->width) / static_cast<float>(rt->height);

            RFG::ViewPassData preparedView{};
            preparedView.prepared.frame                = frameSnapshot;
            preparedView.prepared.frame.viewportWidth  = static_cast<float>(rt->width);
            preparedView.prepared.frame.viewportHeight = static_cast<float>(rt->height);

            const bool built = BuildFrameDataFromCameraEntityWithOverride(
                *ctx.registry, entity, cameraForView, preparedView.prepared.frame);
            if (!built) return;

            // Kaskaden-Splits für das RTT-Frustum neu berechnen.
            // Der Snapshot enthält die Kaskaden der Main-Kamera — für die RTT-Kamera
            // müssen sie auf Basis des RTT-Frustums (viewProjMatrix) neu berechnet werden.
            if (preparedView.prepared.frame.hasShadowPass)
                ctx.backend->ExtractLightData(*ctx.registry, preparedView.prepared.frame);

            preparedView.prepared.graphicsView = {};
            preparedView.prepared.graphicsView.type                     = RenderViewType::RenderTarget;
            // frame NACH ExtractLightData zuweisen — enthält jetzt RTT-Kaskaden
            preparedView.prepared.graphicsView.frame                    = preparedView.prepared.frame;
            preparedView.prepared.graphicsView.renderTarget             = rtCam.target;
            preparedView.prepared.graphicsView.forbiddenShaderReadTexture = rt->exposedTexture;
            preparedView.prepared.graphicsView.gatherOpaque             = rtCam.renderOpaque;
            preparedView.prepared.graphicsView.gatherTransparent        = rtCam.renderTransparent;
            preparedView.prepared.graphicsView.gatherShadows            = false;
            preparedView.prepared.graphicsView.skipSelfReferentialDraws = rtCam.skipSelfReferentialDraws;
            preparedView.prepared.graphicsView.visibilityLayerMask      = preparedView.prepared.frame.cullMask;
            preparedView.prepared.graphicsView.shadowCasterLayerMask    = preparedView.prepared.frame.shadowCasterMask;

            preparedView.prepared.gatherOptions = {};
            preparedView.prepared.gatherOptions.gatherOpaque              = rtCam.renderOpaque;
            preparedView.prepared.gatherOptions.gatherTransparent         = rtCam.renderTransparent;
            preparedView.prepared.gatherOptions.gatherShadows             = false;
            preparedView.prepared.gatherOptions.skipSelfReferentialDraws  = rtCam.skipSelfReferentialDraws;
            preparedView.prepared.gatherOptions.forbiddenShaderReadTexture = rt->exposedTexture;
            preparedView.prepared.gatherOptions.visibilityLayerMask       = preparedView.prepared.frame.cullMask;
            preparedView.prepared.gatherOptions.shadowCasterLayerMask     = preparedView.prepared.frame.shadowCasterMask;

            preparedView.prepared.clearDesc         = rtCam.clear;
            preparedView.prepared.graphicsTargetDesc = RenderPassTargetDesc::Offscreen(
                rtCam.target,
                rtCam.clear,
                static_cast<float>(rt->width),
                static_cast<float>(rt->height),
                rt->debugName);

            preparedView.prepared.shadowEnabled =
                preparedView.prepared.frame.hasShadowPass &&
                rtCam.renderShadows &&
                ctx.shadowResourcesAvailable;

            preparedView.prepared.shadowView = {};
            if (preparedView.prepared.shadowEnabled)
            {
                preparedView.prepared.shadowView                    = preparedView.prepared.graphicsView;
                preparedView.prepared.shadowView.type               = RenderViewType::Shadow;
                preparedView.prepared.shadowView.gatherOpaque       = false;
                preparedView.prepared.shadowView.gatherTransparent  = false;
                preparedView.prepared.shadowView.gatherShadows      = true;
                // Shadow-Caster für RTT nicht per Frustum cullen —
                // der Licht-Frustum deckt die RTT-Szene möglicherweise nicht vollständig ab.
                preparedView.prepared.shadowView.enableFrustumCulling = false;
            }

            outViews.push_back(std::move(preparedView));
        });
}

} // namespace RenderViewPrep
