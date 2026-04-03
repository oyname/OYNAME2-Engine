#pragma once

#include <cstddef>
#include <vector>

struct RenderCommand;
struct RenderBatchRange;

class ParticleCommandList;

class ICommandList
{
public:
    virtual ~ICommandList() = default;

    virtual const std::vector<RenderCommand>& GetCommands() const = 0;
    virtual const std::vector<RenderBatchRange>& GetBatchRanges() const = 0;
    virtual size_t Count() const noexcept = 0;
    virtual bool Empty() const noexcept = 0;
    virtual const ParticleCommandList* AsParticleCommandList() const noexcept { return nullptr; }
};
