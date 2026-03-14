#pragma once

#include <cstddef>
#include <vector>

struct RenderCommand;

class ICommandList
{
public:
    virtual ~ICommandList() = default;

    virtual const std::vector<RenderCommand>& GetCommands() const = 0;
    virtual size_t Count() const noexcept = 0;
    virtual bool Empty() const noexcept = 0;
};
