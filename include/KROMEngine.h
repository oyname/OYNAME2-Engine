#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include "Events.h"
#include "GDXFrameTimer.h"
#include "Collision/CollisionWorld.h"

class GDXEventQueue;
class IGDXWindow;
class IGDXRenderer;

class KROMEngine
{
public:
    KROMEngine(std::unique_ptr<IGDXWindow> window,
        std::unique_ptr<IGDXRenderer> renderer,
        GDXEventQueue& events);
    ~KROMEngine();

    bool Initialize();
    void Run();
    bool Step();

    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const { return m_frameTimer.GetTotalTime(); }

    using EventFn = std::function<void(const Event&)>;
    void SetEventCallback(EventFn fn) { m_eventCallback = std::move(fn); }

    void Shutdown();

    KROM::CollisionWorld& GetCollisionWorld() { return m_collisionWorld; }

private:
    void ProcessEvents(const std::vector<Event>& events);

private:
    std::unique_ptr<IGDXWindow> m_window;
    std::unique_ptr<IGDXRenderer> m_renderer;
    GDXEventQueue& m_events;
    bool m_quitRequested = false;
    bool m_running = false;
    GDXFrameTimer m_frameTimer;
    float m_deltaTime = 0.0f;
    EventFn m_eventCallback;
    std::string m_baseWindowTitle;
    KROM::CollisionWorld m_collisionWorld;
};
