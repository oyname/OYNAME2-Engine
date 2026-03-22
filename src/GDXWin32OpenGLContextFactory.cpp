#include "GDXWin32OpenGLContextFactory.h"
#include "IGDXWin32NativeAccess.h"
#include "GDXWin32NativeHandles.h"
#include "IGDXOpenGLContext.h"
#include "Core/Debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

// ---------------------------------------------------------------------------
// WGL extension constants (not in Windows SDK headers)
// ---------------------------------------------------------------------------
#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_FLAGS_ARB            0x2094
#define WGL_CONTEXT_DEBUG_BIT_ARB        0x00000001

#define WGL_DRAW_TO_WINDOW_ARB           0x2001
#define WGL_SUPPORT_OPENGL_ARB           0x2010
#define WGL_DOUBLE_BUFFER_ARB            0x2011
#define WGL_PIXEL_TYPE_ARB               0x2013
#define WGL_TYPE_RGBA_ARB                0x202B
#define WGL_COLOR_BITS_ARB               0x2014
#define WGL_DEPTH_BITS_ARB               0x2022
#define WGL_STENCIL_BITS_ARB             0x2023
#define WGL_ACCELERATION_ARB             0x2003
#define WGL_FULL_ACCELERATION_ARB        0x2027

typedef HGLRC (WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
typedef BOOL  (WINAPI* PFN_wglChoosePixelFormatARB)(HDC, const int*, const float*,
                                                     UINT, int*, UINT*);

// ---------------------------------------------------------------------------
// Win32OpenGLContext — private implementation, not exposed in header
// ---------------------------------------------------------------------------
namespace
{
    class Win32OpenGLContext final : public IGDXOpenGLContext
    {
    public:
        Win32OpenGLContext(HDC hdc, HGLRC hglrc, HWND hwnd)
            : m_hdc(hdc), m_hglrc(hglrc), m_hwnd(hwnd)
        {
        }

        ~Win32OpenGLContext() override
        {
            wglMakeCurrent(nullptr, nullptr);
            if (m_hglrc) { wglDeleteContext(m_hglrc); m_hglrc = nullptr; }
            if (m_hdc && m_hwnd) { ReleaseDC(m_hwnd, m_hdc); m_hdc = nullptr; }
        }

        bool MakeCurrent() override
        {
            if (!wglMakeCurrent(m_hdc, m_hglrc))
            {
                Debug::LogError("gdxwin32openglcontextfactory.cpp: wglMakeCurrent failed");
                return false;
            }
            return true;
        }

        void SwapBuffers() override
        {
            if (m_hdc) ::SwapBuffers(m_hdc);
        }

        void Resize(int /*w*/, int /*h*/) override
        {
            // OpenGL viewport is updated by the renderer via glViewport.
            // The context itself does not need to be resized.
        }

        GDXOpenGLDeviceInfo QueryDeviceInfo() const override
        {
            GDXOpenGLDeviceInfo info;
            const auto* vendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
            const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            const auto* version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
            info.vendor   = vendor   ? vendor   : "unknown";
            info.renderer = renderer ? renderer : "unknown";
            info.version  = version  ? version  : "unknown";
            return info;
        }

    private:
        HDC   m_hdc   = nullptr;
        HGLRC m_hglrc = nullptr;
        HWND  m_hwnd  = nullptr;
    };

    // -----------------------------------------------------------------------
    // Phase 1: dummy window + legacy context -> load ARB extension pointers
    // -----------------------------------------------------------------------
    bool LoadWGLExtensions(PFN_wglCreateContextAttribsARB& outCreate,
                           PFN_wglChoosePixelFormatARB&    outChoose)
    {
        constexpr const wchar_t* k_class = L"GDXWGLDummy";

        WNDCLASSW wc   = {};
        wc.style       = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance   = GetModuleHandleW(nullptr);
        wc.lpszClassName = k_class;
        RegisterClassW(&wc);

        HWND dHwnd = CreateWindowExW(
            0, k_class, L"", WS_OVERLAPPEDWINDOW,
            0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!dHwnd)
        {
            Debug::LogError("gdxwin32openglcontextfactory.cpp: Phase 1 dummy window failed");
            UnregisterClassW(k_class, GetModuleHandleW(nullptr));
            return false;
        }

        HDC dDC = GetDC(dHwnd);

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize      = sizeof(pfd);
        pfd.nVersion   = 1;
        pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;

        const int fmt = ChoosePixelFormat(dDC, &pfd);
        SetPixelFormat(dDC, fmt, &pfd);

        HGLRC dCtx = wglCreateContext(dDC);
        wglMakeCurrent(dDC, dCtx);

        outCreate = reinterpret_cast<PFN_wglCreateContextAttribsARB>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
        outChoose = reinterpret_cast<PFN_wglChoosePixelFormatARB>(
            wglGetProcAddress("wglChoosePixelFormatARB"));

        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(dCtx);
        ReleaseDC(dHwnd, dDC);
        DestroyWindow(dHwnd);
        UnregisterClassW(k_class, GetModuleHandleW(nullptr));

        if (!outCreate || !outChoose)
        {
            Debug::LogError("gdxwin32openglcontextfactory.cpp: "
                            "WGL ARB extensions not available");
            return false;
        }

        Debug::Log("gdxwin32openglcontextfactory.cpp: Phase 1 complete");
        return true;
    }
}

// ---------------------------------------------------------------------------
// GDXWin32OpenGLContextFactory::Create
// Parameter is IGDXWin32NativeAccess& — no dynamic_cast, compile-time safe.
// ---------------------------------------------------------------------------
std::unique_ptr<IGDXOpenGLContext>
GDXWin32OpenGLContextFactory::Create(IGDXWin32NativeAccess& nativeAccess) const
{
    Debug::Log("gdxwin32openglcontextfactory.cpp: Create START");

    GDXWin32NativeHandles handles{};
    if (!nativeAccess.QueryNativeHandles(handles))
    {
        Debug::LogError("gdxwin32openglcontextfactory.cpp: "
                        "QueryNativeHandles failed — window not yet created?");
        return nullptr;
    }

    HWND hwnd = reinterpret_cast<HWND>(handles.hwnd);
    HDC  hdc  = GetDC(hwnd);
    if (!hdc)
    {
        Debug::LogError("gdxwin32openglcontextfactory.cpp: GetDC failed");
        return nullptr;
    }

    // Phase 1: load WGL extensions.
    PFN_wglCreateContextAttribsARB wglCreateContextAttribsARB = nullptr;
    PFN_wglChoosePixelFormatARB    wglChoosePixelFormatARB    = nullptr;

    if (!LoadWGLExtensions(wglCreateContextAttribsARB, wglChoosePixelFormatARB))
    {
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    // Phase 2: set pixel format on the real HDC.
    const int pixelAttribs[] =
    {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
        WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_STENCIL_BITS_ARB,    8,
        0
    };

    int  pixelFormat = 0;
    UINT numFormats  = 0;
    if (!wglChoosePixelFormatARB(hdc, pixelAttribs, nullptr,
                                  1, &pixelFormat, &numFormats)
        || numFormats == 0)
    {
        Debug::LogError("gdxwin32openglcontextfactory.cpp: "
                        "wglChoosePixelFormatARB failed");
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    DescribePixelFormat(hdc, pixelFormat, sizeof(pfd), &pfd);
    if (!SetPixelFormat(hdc, pixelFormat, &pfd))
    {
        Debug::LogError("gdxwin32openglcontextfactory.cpp: SetPixelFormat failed");
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    // Phase 2: create core context.
    const int ctxAttribs[] =
    {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#ifdef _DEBUG
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_DEBUG_BIT_ARB,
#endif
        0
    };

    HGLRC hglrc = wglCreateContextAttribsARB(hdc, nullptr, ctxAttribs);
    if (!hglrc)
    {
        Debug::LogError("gdxwin32openglcontextfactory.cpp: "
                        "wglCreateContextAttribsARB failed — "
                        "OpenGL 3.3 Core not supported by this driver");
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    if (!wglMakeCurrent(hdc, hglrc))
    {
        Debug::LogError("gdxwin32openglcontextfactory.cpp: wglMakeCurrent failed");
        wglDeleteContext(hglrc);
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    Debug::Log("gdxwin32openglcontextfactory.cpp: OpenGL 3.3 Core context ready");
    return std::make_unique<Win32OpenGLContext>(hdc, hglrc, hwnd);
}
