#pragma once

#include "ICommandList.h"
#include "Particles/IGDXParticleRenderer.h"

#include <vector>

class ParticleCommandList final : public ICommandList
{
public:
    void Clear() noexcept;

    void SetContext(const ParticleRenderContext& context) noexcept;
    void Reserve(size_t alphaCount, size_t additiveCount);
    void SubmitAlpha(const ParticleInstance& instance);
    void SubmitAdditive(const ParticleInstance& instance);

    const ParticleRenderSubmission& GetSubmission() const noexcept;
    ParticleRenderSubmission& GetSubmission() noexcept;
    size_t AlphaCount() const noexcept;
    size_t AdditiveCount() const noexcept;

    const std::vector<RenderCommand>& GetCommands() const override;
    const std::vector<RenderBatchRange>& GetBatchRanges() const override;
    size_t Count() const noexcept override;
    bool Empty() const noexcept override;
    const ParticleCommandList* AsParticleCommandList() const noexcept override;

private:
    ParticleRenderSubmission m_submission{};
};
