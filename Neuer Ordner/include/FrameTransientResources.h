#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct FrameUploadAllocation
{
    size_t offset = 0;
    size_t size = 0;
    bool valid = false;
};

struct FrameUploadArena
{
    size_t capacityBytes = 64u * 1024u;
    size_t usedBytes = 0u;

    void Reset() noexcept { usedBytes = 0u; }

    FrameUploadAllocation Allocate(size_t size, size_t alignment = 16u) noexcept
    {
        const size_t mask = alignment > 0u ? (alignment - 1u) : 0u;
        const size_t alignedOffset = alignment > 0u ? ((usedBytes + mask) & ~mask) : usedBytes;
        if (alignedOffset + size > capacityBytes)
            return {};

        FrameUploadAllocation out{};
        out.offset = alignedOffset;
        out.size = size;
        out.valid = true;
        usedBytes = alignedOffset + size;
        return out;
    }
};

struct DeferredReleaseEntry
{
    void* nativeObject = nullptr;
    uint64_t completionValue = 0ull;
};

struct DeferredReleaseQueue
{
    std::vector<DeferredReleaseEntry> entries;

    void Reset() { entries.clear(); }
    void Enqueue(void* nativeObject, uint64_t completionValue)
    {
        if (!nativeObject)
            return;
        entries.push_back({ nativeObject, completionValue });
    }
};

struct FrameTransientResources
{
    FrameUploadArena uploadArena{};
    DeferredReleaseQueue deferredReleases{};

    void BeginFrame()
    {
        uploadArena.Reset();
        deferredReleases.Reset();
    }
};
