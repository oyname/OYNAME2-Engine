#include "KROMEngine.h"
#include "Core/Debug.h"
#include "GDXInput.h"
#include "GDXEventQueue.h"
#include "IGDXWindow.h"
#include "IGDXRenderer.h"

#include <variant>
#include <string>
#include <sstream>

template<class... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

KROMEngine::KROMEngine(std::unique_ptr<IGDXWindow>   window,
    std::unique_ptr<IGDXRenderer> renderer,
    GDXEventQueue& events)
    : m_window(std::move(window))
    , m_renderer(std::move(renderer))
    , m_events(events)
{
    if (m_window)
        m_baseWindowTitle = m_window->GetTitle();
}


KROMEngine::~KROMEngine() = default;
bool KROMEngine::Initialize()
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

    m_frameTimer.Reset();
    m_deltaTime = 0.0f;

    DBLOG(GDX_SRC_LOC, "initialized successfully");
    m_running = true;
    return true;
}

void KROMEngine::Run()
{
    while (Step()) {}
}

bool KROMEngine::Step()
{
    if (!m_running || m_window->ShouldClose() || m_quitRequested)
        return false;

    GDXInput::BeginFrame();
    m_window->PollEvents();

    m_deltaTime = m_frameTimer.Tick();

    ProcessEvents(m_events.SnapshotAndClear());
    if (!m_running) return false;

    const int w = m_window->GetWidth();
    const int h = m_window->GetHeight();
    if (w <= 0 || h <= 0)
        return true;   // minimiert — überspringen, aber weiterlaufen

    m_renderer->BeginFrame();
    m_renderer->Tick(m_deltaTime);
    m_renderer->EndFrame();

    const std::string debugTitle = m_renderer ? m_renderer->GetWindowDebugTitle() : std::string{};
    if (!debugTitle.empty())
    {
        std::ostringstream oss;
        oss << m_baseWindowTitle << " | " << debugTitle;
        m_window->SetTitle(oss.str().c_str());
    }

    return true;
}

void KROMEngine::Shutdown()
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


void KROMEngine::ProcessEvents(const std::vector<Event>& events)
{
    for (const Event& e : events)
    {
        GDXInput::OnEvent(e);

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
            [this](const KeyPressedEvent& ev)
            {
                if (ev.key == Key::Escape)
                {
                    DBLOG(GDX_SRC_LOC, "Escape pressed - quitting");
                    m_running = false;
                }
            },
            [](const KeyReleasedEvent&) {}
            },
            e
        );

        if (m_eventCallback)
            m_eventCallback(e);
    }
}