#pragma once

#include <string>

class IGDXRenderer
{
public:
    virtual ~IGDXRenderer() = default;

    virtual bool Initialize() = 0;
    virtual void BeginFrame() = 0;
    virtual void Tick(float dt) = 0;
    virtual void EndFrame() = 0;
    virtual void Resize(int w, int h) = 0;
    virtual void Shutdown() = 0;

    virtual std::string GetWindowDebugTitle() const { return {}; }
};
