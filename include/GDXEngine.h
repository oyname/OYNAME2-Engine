#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include "GDXEventQueue.h"
#include "GDXFrameTimer.h"
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
        GDXEventQueue& events);

    bool Initialize();
    void Run();
    bool Step();   // ein Frame — false = beenden

    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const { return m_frameTimer.GetTotalTime(); }

    using EventFn = std::function<void(const Event&)>;
    void SetEventCallback(EventFn fn) { m_eventCallback = std::move(fn); }

    // Idempotent — safe to call multiple times.  The second and subsequent
    // calls are no-ops so callers do not need to track whether shutdown has
    // already occurred.
    void Shutdown();

private:
    void ProcessEvents(const std::vector<Event>& events);

private:
    std::unique_ptr<IGDXWindow>   m_window;
    std::unique_ptr<IGDXRenderer> m_renderer;
    GDXEventQueue& m_events;
    bool                          m_quitRequested = false;
    bool                          m_running = false;
    GDXFrameTimer                 m_frameTimer;
    float                         m_deltaTime = 0.0f;
    EventFn                       m_eventCallback;
    std::string                   m_baseWindowTitle;
};
