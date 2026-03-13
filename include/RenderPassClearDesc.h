#pragma once

#include <cstdint>

struct RenderPassClearDesc
{
    bool clearColorEnabled = true;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    bool clearDepthEnabled = true;
    float clearDepthValue = 1.0f;

    bool clearStencilEnabled = false;
    uint8_t clearStencilValue = 0u;
};
