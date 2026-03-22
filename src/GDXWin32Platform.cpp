#include "GDXWin32Platform.h"
#include "GDXWin32Window.h"
#include "Core/Debug.h"

#include <chrono>

GDXWin32Platform::GDXWin32Platform(GDXEventQueue& events)
    : m_events(events)
{
}

std::unique_ptr<IGDXWindow> GDXWin32Platform::CreateWindow(const WindowDesc& desc)
{
    auto window = std::make_unique<GDXWin32Window>(desc, m_events);
    if (!window->Create())
    {
        Debug::LogError("gdxwin32platform.cpp: window creation failed");
        return nullptr;
    }
    return window;
}

double GDXWin32Platform::GetTimeSeconds() const
{
    using Clock = std::chrono::steady_clock;
    static const auto s_start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - s_start).count();
}
