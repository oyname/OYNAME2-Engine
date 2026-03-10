// All platform-specific GL setup (windows.h, opengl32.lib, etc.) is
// centralised in GDXOpenGLIncludes.h.  This file intentionally contains
// no windows.h or platform-specific includes of its own.
#include "GDXOpenGLIncludes.h"

#include "GDXOpenGLRenderer.h"
#include "Debug.h"

GDXOpenGLRenderer::GDXOpenGLRenderer(std::unique_ptr<IGDXOpenGLContext> context)
    : m_context(std::move(context))
{
}

bool GDXOpenGLRenderer::Initialize()
{
    if (!m_context)
    {
        DBERROR(GDX_SRC_LOC, "context is null");
        return false;
    }

    if (!m_context->MakeCurrent())
    {
        DBERROR(GDX_SRC_LOC, "MakeCurrent failed during Initialize");
        return false;
    }

    const auto info = m_context->QueryDeviceInfo();
    DBLOG(GDX_SRC_LOC, "vendor=",   info.vendor);
    DBLOG(GDX_SRC_LOC, "renderer=", info.renderer);
    DBLOG(GDX_SRC_LOC, "version=",  info.version);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    DBLOG(GDX_SRC_LOC, "initialized");
    return true;
}

void GDXOpenGLRenderer::BeginFrame()
{
    glClearColor(m_clearColor[0], m_clearColor[1],
                 m_clearColor[2], m_clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GDXOpenGLRenderer::EndFrame()
{
    if (m_context) m_context->SwapBuffers();
}

void GDXOpenGLRenderer::Resize(int w, int h)
{
    m_width  = w;
    m_height = h;

    if (m_context)
    {
        // Resize the platform surface first so the new back-buffer dimensions
        // are in place before we call glViewport.
        m_context->Resize(w, h);

        // glViewport requires the GL context to be current.  Make it current
        // explicitly — never assume it is (Rule 24: no implicit global state).
        if (m_context->MakeCurrent())
        {
            glViewport(0, 0, w, h);
        }
        else
        {
            DBERROR(GDX_SRC_LOC, "MakeCurrent failed during Resize — glViewport skipped");
        }
    }

    DBLOG(GDX_SRC_LOC, "resize ", w, "x", h);
}

void GDXOpenGLRenderer::Shutdown()
{
    DBLOG(GDX_SRC_LOC, "shutdown");
    m_context.reset();
}

GDXOpenGLDeviceInfo GDXOpenGLRenderer::GetDeviceInfo() const
{
    if (!m_context) return {};
    return m_context->QueryDeviceInfo();
}

void GDXOpenGLRenderer::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r;
    m_clearColor[1] = g;
    m_clearColor[2] = b;
    m_clearColor[3] = a;
}
