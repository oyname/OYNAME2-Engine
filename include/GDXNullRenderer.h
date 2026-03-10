#pragma once

#include "IGDXRenderer.h"

// GDXNullRenderer is a no-op backend for CI, headless testing,
// or bringup work that does not yet need a real GPU backend.
class GDXNullRenderer final : public IGDXRenderer
{
public:
    bool Initialize()         override { return true; }
    void BeginFrame()         override {}
    void EndFrame()           override {}
    void Resize(int, int)     override {}
    void Shutdown()           override {}
};
