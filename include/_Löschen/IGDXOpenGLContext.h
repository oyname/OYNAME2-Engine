#pragma once

#include <string>

// Device info populated after context creation.
struct GDXOpenGLDeviceInfo
{
    std::string vendor;
    std::string renderer;
    std::string version;
};

// IGDXOpenGLContext owns the WGL/GLX/EGL context for one window surface.
// The renderer receives a fully constructed context — it never touches
// HWND, HDC, or any other platform handle directly.
class IGDXOpenGLContext
{
public:
    virtual ~IGDXOpenGLContext() = default;

    virtual bool                MakeCurrent()           = 0;
    virtual void                SwapBuffers()           = 0;
    virtual void                Resize(int w, int h)    = 0;
    virtual GDXOpenGLDeviceInfo QueryDeviceInfo() const = 0;
};
