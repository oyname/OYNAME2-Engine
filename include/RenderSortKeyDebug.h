#pragma once

#include "RenderQueue.h"

#include <cstddef>

namespace RenderSortKeyDebug
{
    struct Options
    {
        bool enabled = false;
        size_t maxCommandsPerQueue = 16u;
    };

    void SetOptions(const Options& options) noexcept;
    const Options& GetOptions() noexcept;
    bool IsEnabled() noexcept;

    void DumpCommand(const RenderCommand& cmd, const char* prefix = nullptr);
    void DumpQueue(const RenderQueue& queue, const char* queueName, size_t maxCommands = 16u);
}
