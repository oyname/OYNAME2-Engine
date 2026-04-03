#include "RenderSortKeyDebug.h"
#include "RenderSortKey.h"
#include "Core/Debug.h"

namespace
{
    RenderSortKeyDebug::Options g_options{};
}

namespace RenderSortKeyDebug
{
    void SetOptions(const Options& options) noexcept
    {
        g_options = options;
    }

    const Options& GetOptions() noexcept
    {
        return g_options;
    }

    bool IsEnabled() noexcept
    {
        return g_options.enabled;
    }

    void DumpCommand(const RenderCommand& cmd, const char* prefix)
    {
        if (!g_options.enabled)
            return;

        const char* label = prefix ? prefix : "RenderSortKey";
        Debug::Log(label,
                   " entity=", cmd.ownerEntity.value,
                   " pass=", static_cast<uint32_t>(cmd.pass),
                   " sortKey=0x", std::hex, cmd.sortKey, std::dec,
                   " ", RenderSortKey::ToDebugString(cmd.sortKey));
    }

    void DumpQueue(const RenderQueue& queue, const char* queueName, size_t maxCommands)
    {
        if (!g_options.enabled)
            return;

        const char* name = queueName ? queueName : "RenderQueue";
        const size_t limit = maxCommands < queue.commands.size() ? maxCommands : queue.commands.size();
        Debug::Log("SortKeyDump queue=", name,
                   " count=", queue.commands.size(),
                   " dumped=", limit);
        for (size_t i = 0; i < limit; ++i)
        {
            const RenderCommand& cmd = queue.commands[i];
            Debug::Log("  [", i, "] entity=", cmd.ownerEntity.value,
                       " pass=", static_cast<uint32_t>(cmd.pass),
                       " key=0x", std::hex, cmd.sortKey, std::dec,
                       " ", RenderSortKey::ToDebugString(cmd.sortKey));
        }
    }
}

void RenderQueue::DebugDump(const char* queueName, size_t maxCommands) const
{
    RenderSortKeyDebug::DumpQueue(*this, queueName, maxCommands);
}
