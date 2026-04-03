#include "ParticleCommandList.h"
#include "RenderCommand.h"

namespace
{
    const std::vector<RenderCommand> kEmptyParticleCommands{};
    const std::vector<RenderBatchRange> kEmptyParticleBatches{};
}

void ParticleCommandList::Clear() noexcept
{
    m_submission.Clear();
    m_submission.context = {};
}

void ParticleCommandList::SetContext(const ParticleRenderContext& context) noexcept
{
    m_submission.context = context;
}

void ParticleCommandList::Reserve(size_t alphaCount, size_t additiveCount)
{
    m_submission.alphaInstances.reserve(alphaCount);
    m_submission.additiveInstances.reserve(additiveCount);
}

void ParticleCommandList::SubmitAlpha(const ParticleInstance& instance)
{
    m_submission.alphaInstances.push_back(instance);
}

void ParticleCommandList::SubmitAdditive(const ParticleInstance& instance)
{
    m_submission.additiveInstances.push_back(instance);
}

const ParticleRenderSubmission& ParticleCommandList::GetSubmission() const noexcept
{
    return m_submission;
}

ParticleRenderSubmission& ParticleCommandList::GetSubmission() noexcept
{
    return m_submission;
}

size_t ParticleCommandList::AlphaCount() const noexcept
{
    return m_submission.alphaInstances.size();
}

size_t ParticleCommandList::AdditiveCount() const noexcept
{
    return m_submission.additiveInstances.size();
}

const std::vector<RenderCommand>& ParticleCommandList::GetCommands() const
{
    return kEmptyParticleCommands;
}

size_t ParticleCommandList::Count() const noexcept
{
    return static_cast<size_t>(m_submission.InstanceCount());
}

bool ParticleCommandList::Empty() const noexcept
{
    return m_submission.Empty();
}

const ParticleCommandList* ParticleCommandList::AsParticleCommandList() const noexcept
{
    return this;
}

const std::vector<RenderBatchRange>& ParticleCommandList::GetBatchRanges() const
{
    return kEmptyParticleBatches;
}
