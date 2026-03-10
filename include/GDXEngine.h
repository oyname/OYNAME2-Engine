#pragma once

#include <memory>
#include <vector>
#include "GDXEventQueue.h"
#include "IGDXWindow.h"
#include "IGDXRenderer.h"

class GDXEngine
{
public:
    // GDXEngine takes ownership of the window and renderer.
    // The event queue is owned externally so platform code (WndProc) can push
    // events into it before the engine reads them each frame.
    GDXEngine(std::unique_ptr<IGDXWindow>   window,
              std::unique_ptr<IGDXRenderer> renderer,
              GDXEventQueue&                events);

    bool Initialize();
    void Run();

    // Idempotent — safe to call multiple times.  The second and subsequent
    // calls are no-ops so callers do not need to track whether shutdown has
    // already occurred.
    void Shutdown();

private:
    void ProcessEvents(const std::vector<Event>& events);

private:
    std::unique_ptr<IGDXWindow>   m_window;
    std::unique_ptr<IGDXRenderer> m_renderer;
    GDXEventQueue&                m_events;
    bool                          m_running = false;
};
