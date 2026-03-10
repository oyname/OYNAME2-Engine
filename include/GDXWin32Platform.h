#pragma once

#include "IGDXPlatform.h"
#include "GDXEventQueue.h"

class GDXWin32Platform final : public IGDXPlatform
{
public:
    explicit GDXWin32Platform(GDXEventQueue& events);

    std::unique_ptr<IGDXWindow> CreateWindow(const WindowDesc& desc) override;
    double GetTimeSeconds() const override;

private:
    GDXEventQueue& m_events;
};
