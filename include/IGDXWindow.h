#pragma once

// IGDXWindow is the platform-neutral window abstraction.
// It deliberately carries no native handles (no HWND, no HDC, no Display*).
// Platform-specific backends that need raw handles implement an additional
// interface (e.g. IGDXWin32NativeAccess) alongside this one.
// Renderers never receive an IGDXWindow directly — they receive a fully
// constructed context object (e.g. IGDXOpenGLContext) built by a
// platform-side factory that already has the handles it needs.

class IGDXWindow
{
public:
    virtual ~IGDXWindow() = default;

    virtual void        PollEvents()       = 0;
    virtual bool        ShouldClose() const = 0;
    virtual int         GetWidth()    const = 0;
    virtual int         GetHeight()   const = 0;
    virtual bool        GetBorderless() const = 0;
    virtual const char* GetTitle()    const = 0;
    virtual void        SetTitle(const char* title) = 0;
};
