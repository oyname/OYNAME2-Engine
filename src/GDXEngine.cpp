#include "GDXEngine.h"
#include "Debug.h"

#include <variant>
#include <chrono>

template<class... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

GDXEngine::GDXEngine(std::unique_ptr<IGDXWindow>   window,
    std::unique_ptr<IGDXRenderer> renderer,
    GDXEventQueue& events)
    : m_window(std::move(window))
    , m_renderer(std::move(renderer))
    , m_events(events)
{
}

bool GDXEngine::Initialize()
{
    if (!m_window || !m_renderer)
    {
        DBERROR(GDX_SRC_LOC, "window or renderer is null");
        return false;
    }

    if (!m_renderer->Initialize())
    {
        DBERROR(GDX_SRC_LOC, "renderer Initialize failed");
        return false;
    }

    m_renderer->Resize(m_window->GetWidth(), m_window->GetHeight());

    DBLOG(GDX_SRC_LOC, "initialized successfully");
    m_running = true;
    return true;
}

void GDXEngine::Run()
{
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<float>;  // seconds as float

    auto lastTime = Clock::now();

    while (m_running && !m_window->ShouldClose())
    {
        // -----------------------------------------------------------------
        // Pump the OS message queue so Win32 dispatches WM_SIZE, WM_CLOSE,
        // WM_KEYDOWN etc. into GDXEventQueue via WndProc.  Without this call
        // the window becomes unresponsive and no events ever arrive.
        // -----------------------------------------------------------------
        m_window->PollEvents();

        // -----------------------------------------------------------------
        // Delta-time
        // -----------------------------------------------------------------
        const auto  now = Clock::now();
        const float deltaTime = std::chrono::duration_cast<Duration>(now - lastTime).count();
        lastTime = now;

        // -----------------------------------------------------------------
        // Events — atomically move all queued events out of the queue and
        // process them.  SnapshotAndClear() holds the mutex for the entire
        // swap so a Push() from WndProc cannot slip in between the read and
        // the clear and silently lose an event (the old Snapshot()+Clear()
        // two-step had exactly that race).
        // -----------------------------------------------------------------
        ProcessEvents(m_events.SnapshotAndClear());

        if (!m_running) break;   // QuitEvent may have set this during ProcessEvents

        // -----------------------------------------------------------------
        // Skip rendering when the window is minimized (0 x 0).
        // Issuing GL/DX calls with zero-size render targets is undefined
        // behaviour on most drivers and wastes CPU on invisible frames.
        // -----------------------------------------------------------------
        const int w = m_window->GetWidth();
        const int h = m_window->GetHeight();

        if (w <= 0 || h <= 0)
            continue;

        // -----------------------------------------------------------------
        // Render
        // -----------------------------------------------------------------
        m_renderer->BeginFrame();
        // TODO: dispatch scene/game tick here with deltaTime
        (void)deltaTime;
        m_renderer->EndFrame();
    }
}

void GDXEngine::Shutdown()
{
    if (m_running)
    {
        m_running = false;

        if (m_renderer)
            m_renderer->Shutdown();

        DBLOG(GDX_SRC_LOC, "shutdown complete");
    }
    // Calling Shutdown() again after the first time is a no-op (idempotent).
}

void GDXEngine::ProcessEvents(const std::vector<Event>& events)
{
    for (const Event& e : events)
    {
        std::visit(
            Overloaded{
                [this](const QuitEvent&)
                {
                    DBLOG(GDX_SRC_LOC, "QuitEvent received");
                    m_running = false;
                },
                [this](const WindowResizedEvent& ev)
                {
                // Guard: do not forward minimized (0x0) resize to the renderer.
                if (ev.width <= 0 || ev.height <= 0)
                {
                    DBLOG(GDX_SRC_LOC, "window minimized — resize skipped");
                    return;
                }
                DBLOG(GDX_SRC_LOC, "resize ", ev.width, "x", ev.height);
                m_renderer->Resize(ev.width, ev.height);
            },
            [](const KeyPressedEvent& ev)
            {
                if (ev.key == Key::Escape)
                    DBLOG(GDX_SRC_LOC, "Escape pressed");
            },
            [](const KeyReleasedEvent&) {}
            },
            e
        );
    }
}
