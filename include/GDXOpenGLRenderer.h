#pragma once

#include "IGDXRenderer.h"
#include "IGDXOpenGLContext.h"

#include <memory>

// GDXOpenGLRenderer owns an IGDXOpenGLContext and knows nothing about
// HWND, HDC, or WGL.  All platform details are encapsulated in the context.
class GDXOpenGLRenderer final : public IGDXRenderer
{
public:
    explicit GDXOpenGLRenderer(std::unique_ptr<IGDXOpenGLContext> context);

    bool Initialize()         override;
    void BeginFrame()         override;
    void EndFrame()           override;
    void Resize(int w, int h) override;
    void Shutdown()           override;

    GDXOpenGLDeviceInfo GetDeviceInfo() const;

    void SetClearColor(float r, float g, float b, float a = 1.0f);

private:
    std::unique_ptr<IGDXOpenGLContext> m_context;
    float m_clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    int   m_width  = 0;
    int   m_height = 0;
};
