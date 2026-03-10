#include "GDXEngine.h"
#include "GDXEventQueue.h"
#include "WindowDesc.h"
#include "GDXWin32Window.h"
#include "Debug.h"

// ---------------------------------------------------------------------------
// Renderer selection — uncomment exactly one.
// ---------------------------------------------------------------------------
#define GDX_RENDERER_DX11
// #define GDX_RENDERER_OPENGL
// #define GDX_RENDERER_NULL
// ---------------------------------------------------------------------------

#if defined(GDX_RENDERER_DX11)
    #include "GDXWin32DX11ContextFactory.h"
    #include "GDXDX11Renderer.h"
#elif defined(GDX_RENDERER_OPENGL)
    #include "GDXWin32OpenGLContextFactory.h"
    #include "GDXOpenGLRenderer.h"
#elif defined(GDX_RENDERER_NULL)
    #include "GDXNullRenderer.h"
#else
    #error "No renderer selected — uncomment one of the GDX_RENDERER_* defines."
#endif

// ---------------------------------------------------------------------------
// Window creation pattern
//
// GDXWin32Window is constructed directly (not via IGDXPlatform::CreateWindow)
// so that we hold both interface views before transferring ownership:
//
//   - As IGDXWindow             -> transferred into GDXEngine via std::move
//   - As IGDXWin32NativeAccess  -> passed to the context factory (DX11 / GL)
//
// windowRaw is only used before the move and never stored afterwards.
// ---------------------------------------------------------------------------

int main()
{
    GDXEventQueue events;

    WindowDesc desc;
    desc.width     = 1280;
    desc.height    = 720;
    desc.title     = "OYNAME2 Engine";
    desc.resizable = true;

    auto windowOwned = std::make_unique<GDXWin32Window>(desc, events);
    if (!windowOwned->Create())
    {
        Debug::LogError("main.cpp: window creation failed");
        return 1;
    }
    GDXWin32Window* windowRaw = windowOwned.get();

// ---------------------------------------------------------------------------
#if defined(GDX_RENDERER_DX11)

    auto adapters = GDXWin32DX11ContextFactory::EnumerateAdapters();
    if (adapters.empty())
    {
        Debug::LogError("main.cpp: no suitable DX11 adapter found");
        return 2;
    }
    const unsigned int adapterIdx =
        GDXWin32DX11ContextFactory::FindBestAdapter(adapters);
    Debug::Log("main.cpp: DX11 adapter ", adapterIdx,
               " [", adapters[adapterIdx].name, "]");

    GDXWin32DX11ContextFactory dx11Factory;
    auto dxContext = dx11Factory.Create(*windowRaw, adapterIdx);
    if (!dxContext)
    {
        Debug::LogError("main.cpp: DX11 context creation failed");
        return 3;
    }
    auto renderer = std::make_unique<GDXDX11Renderer>(std::move(dxContext));

// ---------------------------------------------------------------------------
#elif defined(GDX_RENDERER_OPENGL)

    // No adapter enumeration for OpenGL on Windows — the OS selects the GPU.
    Debug::Log("main.cpp: OpenGL — adapter selected implicitly by OS");

    GDXWin32OpenGLContextFactory glFactory;
    auto glContext = glFactory.Create(*windowRaw);
    if (!glContext)
    {
        Debug::LogError("main.cpp: OpenGL context creation failed");
        return 3;
    }
    auto renderer = std::make_unique<GDXOpenGLRenderer>(std::move(glContext));

// ---------------------------------------------------------------------------
#elif defined(GDX_RENDERER_NULL)

    auto renderer = std::make_unique<GDXNullRenderer>();

#endif
// ---------------------------------------------------------------------------

    GDXEngine engine(std::move(windowOwned), std::move(renderer), events);

    if (!engine.Initialize())
    {
        Debug::LogError("main.cpp: engine initialization failed");
        return 4;
    }

    engine.Run();
    engine.Shutdown();
    return 0;
}
