#pragma once

#include "Handle.h"

#include <cstdint>
#include <string>
#include <vector>

struct PostProcessPassDesc
{
    std::wstring vertexShaderFile;
    std::wstring pixelShaderFile;
    std::wstring debugName;
    uint32_t constantBufferBytes = 0u;
    bool enabled = true;
};

struct PostProcessResource
{
    PostProcessPassDesc desc;
    std::vector<uint8_t> constantData;
    uint32_t constantBufferBytes = 0u;
    bool enabled = true;
    bool cpuDirty = false;
    bool ready = false;
};
