#pragma once

#include <memory>

#include "IGDXRenderer.h"
#include "IGDXDXGIContext.h"

// ---------------------------------------------------------------------------
// Legacy DX11 renderer.
//
// Dieser Typ ist nur noch ein einfacher DX11-Only Renderer für Clear/Present.
// Der eigentliche ECS-Renderpfad läuft nach dem Backend-Split über:
//   - GDXECSRenderer        (Frontend / Orchestrator)
//   - GDXDX11RenderBackend  (DX11-Backend)
//
// Die Klasse bleibt hier bewusst erhalten, damit alter Code bzw. alte
// Projektdateien nicht sofort mit einem fehlenden Include brechen.
// ---------------------------------------------------------------------------
class GDXDX11Renderer final : public IGDXRenderer
{
public:
    explicit GDXDX11Renderer(std::unique_ptr<IGDXDXGIContext> context);
    ~GDXDX11Renderer() override = default;

    bool Initialize() override;
    void BeginFrame() override;
    void EndFrame() override;
    void Resize(int w, int h) override;
    void Shutdown() override;

    GDXDXGIDeviceInfo GetDeviceInfo() const;
    void SetClearColor(float r, float g, float b, float a);
    void SetVSync(bool enabled);

private:
    std::unique_ptr<IGDXDXGIContext> m_context;
    float m_clearColor[4] = { 0.10f, 0.12f, 0.16f, 1.0f };
    bool m_vsync = true;
};
