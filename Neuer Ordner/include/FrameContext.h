#pragma once

#include "FrameData.h"

#include <array>
#include <cstdint>

static constexpr uint32_t GDXMaxFramesInFlight = 2u;

struct FrameSubmitResult
{
    uint64_t submissionId = 0ull;
    uint64_t completionValue = 0ull;
    bool submitted = false;
    bool completed = false;
};

struct FrameContext
{
    uint32_t frameIndex = 0u;
    uint64_t frameNumber = 0ull;
    bool inFlight = false;
    bool submitted = false;
    FrameData* frameData = nullptr;
    FrameSubmitResult submitResult{};

    void Begin(uint32_t newFrameIndex, uint64_t newFrameNumber, FrameData* data)
    {
        frameIndex = newFrameIndex;
        frameNumber = newFrameNumber;
        inFlight = true;
        submitted = false;
        frameData = data;
        submitResult = {};
    }

    void MarkSubmitted(uint64_t submissionValue, uint64_t completionTargetValue = 0ull)
    {
        if (completionTargetValue == 0ull)
            completionTargetValue = submissionValue;

        submitted = true;
        inFlight = true;
        submitResult.submitted = true;
        submitResult.completed = false;
        submitResult.submissionId = submissionValue;
        submitResult.completionValue = completionTargetValue;
    }

    void MarkCompleted(uint64_t completedValue)
    {
        if (submitResult.submitted && completedValue >= submitResult.completionValue)
        {
            submitResult.completed = true;
            inFlight = false;
        }
    }

    bool NeedsCompletionWait() const
    {
        return submitResult.submitted && !submitResult.completed;
    }

    uint64_t GetRequiredCompletionValue() const
    {
        return submitResult.completionValue;
    }

    void ResetSubmissionTracking()
    {
        inFlight = false;
        submitted = false;
        submitResult = {};
    }
};

using FrameContextRing = std::array<FrameContext, GDXMaxFramesInFlight>;
