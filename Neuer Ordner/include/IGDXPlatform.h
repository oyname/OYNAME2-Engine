#pragma once

#include <memory>
#include "IGDXWindow.h"
#include "WindowDesc.h"

class IGDXPlatform
{
public:
    virtual ~IGDXPlatform() = default;

    virtual std::unique_ptr<IGDXWindow> CreateWindow(const WindowDesc& desc) = 0;

    // Returns seconds elapsed since first call.
    // Uses std::chrono::steady_clock — the zero point is the first invocation,
    // not program start.  Suitable for frame timing; not for wall-clock time.
    virtual double GetTimeSeconds() const = 0;
};
