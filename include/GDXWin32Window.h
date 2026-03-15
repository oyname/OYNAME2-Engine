#pragma once

#include "IGDXWindow.h"
#include "WindowDesc.h"
#include "GDXEventQueue.h"
#include "IGDXWin32NativeAccess.h"

// GDXWin32Window implements both the platform-neutral IGDXWindow and the
// Win32-specific IGDXWin32NativeAccess.  Callers that need raw handles
// use the second interface; the engine loop only ever sees IGDXWindow.

class GDXWin32Window final : public IGDXWindow, public IGDXWin32NativeAccess
{
public:
    GDXWin32Window(const WindowDesc& desc, GDXEventQueue& events);
    ~GDXWin32Window() override;

    // Must be called once after construction.  Returns false on failure.
    bool Create();

    // IGDXWindow
    void        PollEvents()        override;
    bool        ShouldClose() const override;
    int         GetWidth()    const override;
    int         GetHeight()   const override;
    bool        GetBorderless() const override;
    const char* GetTitle()    const override;

    // IGDXWin32NativeAccess
    bool QueryNativeHandles(GDXWin32NativeHandles& outHandles) const override;
    bool IsBorderless() const override { return m_borderless; }

private:
    static long long __stdcall StaticWndProc(
        HWND__* hwnd,
        unsigned int       msg,
        unsigned long long wparam,
        long long          lparam);

    long long WndProc(
        unsigned int       msg,
        unsigned long long wparam,
        long long          lparam);

    void OnResize(int w, int h);
    static bool RegisterClassOnce();

private:
    WindowDesc         m_desc;
    GDXEventQueue& m_events;
    GDXWin32NativeHandles m_handles;

    bool m_shouldClose = false;
    int  m_width = 0;
    int  m_height = 0;
    bool m_borderless = true;
};