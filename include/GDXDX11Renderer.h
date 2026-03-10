#pragma once

#include "IGDXRenderer.h"
#include "IGDXDXGIContext.h"

#include <memory>

// GDXDX11Renderer owns an IGDXDXGIContext and knows nothing about HWND,
// DXGI factories, or COM.  All platform details are in the context.
class GDXDX11Renderer final : public IGDXRenderer
{
public:
    explicit GDXDX11Renderer(std::unique_ptr<IGDXDXGIContext> context);

    bool Initialize()         override;
    void BeginFrame()         override;
    void EndFrame()           override;
    void Resize(int w, int h) override;
    void Shutdown()           override;

    GDXDXGIDeviceInfo GetDeviceInfo() const;

    void  SetClearColor(float r, float g, float b, float a = 1.0f);
    void  SetVSync(bool enabled);

private:
    std::unique_ptr<IGDXDXGIContext> m_context;
    float m_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool  m_vsync          = true;
};
